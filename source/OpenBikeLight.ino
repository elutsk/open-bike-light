//	Rear Bike Light firmware
//	Target:    ATtiny13A, hardware schematic revision circuit2
//	Toolchain: Arduino IDE 2.3.6 + MicroCore 2.5.2
//	Settings:  Clock 9.6 MHz internal, BOD 2.7V, EEPROM retained, No bootloader
//	Fuses:     lfuse 0x3A, hfuse 0xFB (written by MicroCore "Burn Bootloader")
//	Upload:    Sketch > Upload Using Programmer
//	See README.md for building, flashing, calibration and the user manual.
//
//	SAFETY: never power the board from the programmer, VCC is the Li-ion cell.
//	SAFETY: never program the RSTDISBL or WDTON fuses.

// Size-oriented compiler options: these optimisations duplicate code paths,
// which costs more flash than the ATtiny13A has to spare.
#pragma GCC optimize ("-fno-tree-dominator-opts", "-fno-thread-jumps", "-fno-tree-vrp", "-fno-tree-pre", "-fno-gcse")

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

// ============================================================================
//	PER-BUILD PARAMETERS - check these for every build
// ============================================================================

// Forward voltage of the main LED (D3) at its working current, mV.
// Typical red LED: 2100..2400. Measure it if you can.
#define LED_VF_MV			2300

// Battery voltage (under load) at which the LED reaches full brightness
// at 100% PWM, mV. R9 is chosen so that this happens at 25% battery charge.
// Above this voltage the firmware lowers the PWM duty to keep brightness
// constant, below it brightness falls slowly. Normally equal to BAT_25_MV.
#define LED_VFULL_MV		3720

// ============================================================================
//	BATTERY THRESHOLDS, mV (typical Li-ion discharge curve)
// ============================================================================

#define BAT_75_MV			3970	// 75..100%: 4 flashes
#define BAT_50_MV			3850	// 50..75%:  3 flashes
#define BAT_25_MV			3720	// 25..50%:  2 flashes, below: 1 flash at 50%
#define BAT_10_MV			3650	// below: eco mode only
#define BAT_CUTOFF_MV		3100	// below: light off (0%)

// ============================================================================
//	ADC REFERENCE CALIBRATION
// ============================================================================

// The internal 1.1V reference differs from chip to chip (+-10%), so every
// board needs a calibration factor in EEPROM (see README). Without it the
// nominal 1100 mV is assumed. Values outside 950..1250 mV are ignored.
#define VREF_DEFAULT_MV		1100
#define VREF_MIN_MV			950
#define VREF_MAX_MV			1250

// ============================================================================
//	BUTTON TIMING, ms
// ============================================================================

#define BTN_DEBOUNCE_MS		50		// input must be stable this long
#define BTN_DP_WINDOW_MS	300		// max gap after first release for a double press
#define BTN_LP_MS			1000	// hold time for a long press

// ============================================================================
//	INTERNALS - no need to change below this line
// ============================================================================

#define ledPin				PB0		// OC0A, LED MOSFET gate (R8, R10)
#define freePin				PB1		// not connected, internal pull-up on
#define divPin				PB2		// battery divider enable (R11)
#define butPin				PB3		// button SW1 to GND, internal pull-up
#define batPin				PB4		// ADC2, divider midpoint (R7, C4)

#define TICK_MS				10
#define MS2T(ms)			((ms) / TICK_MS)
// Timer0 overflows at F_CPU / 8 / 256 = 4687.5 Hz, 47 overflows = 10.03 ms
#define TICK_OVF			((F_CPU / 2048 * TICK_MS + 500) / 1000)
#define BAT_CHECK_TICKS		MS2T(1000)	// battery check interval while the light is on

// ADC: internal 1.1V reference, channel ADC2 (PB4), prescaler 64 = 150 kHz
#define ADMUX_BAT			((1 << REFS0) | (1 << MUX1))
#define ADCSRA_ON			((1 << ADEN) | (1 << ADPS2) | (1 << ADPS1))

// Timer0: fast PWM, TOP 0xFF, prescaler 8. LED on = OC0A connected.
#define TCCR0A_OFF			((1 << WGM01) | (1 << WGM00))
#define TCCR0A_ON			(TCCR0A_OFF | (1 << COM0A1))

// Divider R11 = 1M, R7 = 270k: Vbat = Vadc * 1270 / 270.
// Four ADC samples are summed (0..4092): Vbat_mV = sum * scale / 16384.
// scale = Vref_mV * 1270 * 16384 / (270 * 4096) = Vref_mV * 508 / 27
// (20696 for the nominal 1100 mV). Calibration: scale = Vbat_mV * 16384 / sum.
#define VREF_SCALE(mv)		((uint16_t)((uint32_t)(mv) * 508 / 27))

// Battery voltage is handled in 8 mV steps: mv8 = mV / 8 (16 bit), and for
// threshold compares as one byte: V8 = mV / 8 - 375 (3000..5040 mV).
#define V8(mv)				((uint8_t)((mv) / 8 - 375))

// Duty for full brightness = 255 * (Vfull - Vf) / (Vbat - Vf), in 8 mV units
#define DUTY_NUM			((uint16_t)((uint32_t)(LED_VFULL_MV - LED_VF_MV) * 255 / 8))

// EEPROM map, 16-bit values are little-endian (low byte first)
#define EE_SCALE			0	// uint16, calibration factor, written via ISP
#define EE_ADCSUM			2	// uint16, raw ADC sum of the last battery check
#define EE_MODE				4	// uint8, saved light mode

#if (LED_VFULL_MV - LED_VF_MV) > 2056 || LED_VFULL_MV <= LED_VF_MV
#error "LED_VFULL_MV - LED_VF_MV must be 1..2056 mV"
#endif
#if BAT_CUTOFF_MV < 3008 || BAT_75_MV > 5000
#error "Battery thresholds must be within 3008..5000 mV"
#endif
#if MS2T(BTN_LP_MS) > 250 || MS2T(BTN_DP_WINDOW_MS) > 250
#error "Button times must be below 2500 ms"
#endif

// Light patterns. Rows 0..4 are the light modes, rows 5..8 the battery
// indication and the "battery empty" flash. A pattern with a pause after
// the group (gap > 0) repeats forever, with gap = 0 it plays once.
// Block 1 = modes 0..2 (flashing), block 2 = modes 3..4 (steady).
#define MODE_COUNT			5
#define BLOCK2_FIRST		3
#define PAT_BAT1			5	// 1 flash at 50%: below 25%, also "battery empty"

#define HALF				0x80	// flag in "count": 50% intensity

struct Pattern {
	uint8_t on;			// flash length, ticks; 0 = steady light
	uint8_t off;		// pause between flashes of a group, ticks
	uint8_t count;		// flashes per group, plus HALF for 50% intensity
	uint8_t gap;		// pause after the group, ticks
};

const Pattern patTable[] PROGMEM = {
	{ MS2T(50),  0,         1,        MS2T(1000) },	// 0 eco
	{ MS2T(50),  MS2T(100), 3,        MS2T(600)  },	// 1 normal
	{ MS2T(200), 0,         1,        MS2T(400)  },	// 2 hi-vis
	{ 0,         0,         HALF,     0          },	// 3 ride, 50%
	{ 0,         0,         0,        0          },	// 4 beam, 100%
	{ MS2T(50),  MS2T(300), 1 | HALF, 0          },	// 5 battery below 25%, or empty
	{ MS2T(50),  MS2T(300), 2,        0          },	// 6 battery 25..50%
	{ MS2T(50),  MS2T(300), 3,        0          },	// 7 battery 50..75%
	{ MS2T(50),  MS2T(300), 4,        0          },	// 8 battery 75..100%
};

#define PAT_NONE			0xFF

enum { EV_NONE, EV_SP, EV_DP, EV_LP };

// ---------------------------------------------------------------- startup

// Runs before main(): clear the reset flags and stop the watchdog.
// After a watchdog reset the WDT stays enabled at 16 ms, so this must
// happen first. Any reset (power-on, brown-out, watchdog, external) ends
// in the "off" state: pins are high-Z and R10 keeps the LED off.
void earlyInit(void) __attribute__((naked, used, section(".init3")));
void earlyInit(void) {
	MCUSR = 0;								// WDRF must be cleared first
	WDTCR = (1 << WDCE) | (1 << WDE);		// timed sequence, interrupts are
	WDTCR = 0;								// still disabled after reset
}

// Timer0 overflow: only wakes the MCU from idle sleep; the main loop
// counts the wake-ups to make 10 ms ticks
EMPTY_INTERRUPT(TIM0_OVF_vect);

// Button pin change: only used to wake the MCU from power-down
EMPTY_INTERRUPT(PCINT0_vect);

// ---------------------------------------------------------------- EEPROM

uint8_t eeRead(uint8_t addr) {
	while (EECR & (1 << EEPE));
	EEAR = addr;
	EECR |= (1 << EERE);
	return EEDR;
}

// Writes only if the value differs (saves EEPROM wear and time)
void eeWrite(uint8_t addr, uint8_t data) {
	if (eeRead(addr) == data) return;
	EEDR = data;
	cli();						// EEPE must follow EEMPE within 4 cycles
	EECR |= (1 << EEMPE);
	EECR |= (1 << EEPE);
	sei();
}

// ---------------------------------------------------------------- hardware

void ledOn() {
	TCCR0A = TCCR0A_ON;
}

void ledOff() {
	TCCR0A = TCCR0A_OFF;		// OC0A disconnected, PB0 driven low
}

// Raw battery reading: sum of 4 conversions, 0..4092
uint16_t adcBattery() {
	uint16_t sum = 0;
	for (uint8_t i = 0; i < 5; i++) {
		ADCSRA |= (1 << ADSC);
		while (ADCSRA & (1 << ADSC));
		if (i) sum += ADC;		// first conversion is discarded
	}
	return sum;
}

// Awake state: divider on (settles while the button is held), ADC on,
// watchdog 2 s, idle sleep between ticks (Timer0 and PWM keep running)
void wakeUp() {
	PORTB |= (1 << divPin);
	ADCSRA = ADCSRA_ON;
	TCCR0B = (1 << CS01);		// Timer0 prescaler 8: PWM 4.69 kHz, ticks
	wdt_enable(WDTO_2S);
	MCUCR = (1 << SE);			// sleep mode: idle, sleep stays enabled
}

// Deep sleep until the button is pressed. Sleep current: everything off,
// divider ends both at GND, only the button pull-up stays (open switch).
void powerDown() {
	ledOff();
	PORTB &= ~(1 << divPin);	// divider off
	ADCSRA = 0;					// ADC and internal reference off
	TCCR0B = 0;					// Timer0 stopped (it halts in power-down anyway)
	GIMSK = (1 << PCIE);		// button pin change wakes the MCU
	MCUCR = (1 << SE) | (1 << SM1);		// sleep mode: power-down
	cli();
	WDTCR = (1 << WDCE) | (1 << WDE);	// watchdog off (timed sequence)
	WDTCR = 0;
	BODCR = (1 << BODS) | (1 << BODSE);	// BOD off while sleeping (timed
	BODCR = (1 << BODS);				// sequence, sleep within 3 cycles)
	sei();
	sleep_cpu();
	GIMSK = 0;
	wakeUp();
}

// ---------------------------------------------------------------- main

// All state lives in local variables of setup(), so the compiler can keep
// it in registers. This saves several hundred bytes of flash compared with
// global variables. loop() is not used.
void setup() {
	DDRB = (1 << ledPin) | (1 << divPin);
	PORTB = (1 << butPin) | (1 << freePin);
	DIDR0 = (1 << ADC2D);			// PB4 is analog only
	ACSR = (1 << ACD);				// analog comparator off
	ADMUX = ADMUX_BAT;
	TIMSK0 = (1 << TOIE0);
	PCMSK = (1 << PCINT3);

	uint16_t scale = eeRead(EE_SCALE) | (eeRead(EE_SCALE + 1) << 8);
	if (scale < VREF_SCALE(VREF_MIN_MV) || scale > VREF_SCALE(VREF_MAX_MV)) {
		scale = VREF_SCALE(VREF_DEFAULT_MV);
	}

	uint8_t ovfCount = 0;		// timer overflows, TICK_OVF make one tick
	// button
	uint8_t btnStable = 0;		// debounced state, 1 = pressed
	uint8_t btnCnt = 0;			// debounce counter
	uint8_t btnTime = 0;		// ticks since the last debounced change
	uint8_t clicks = 0;			// presses in the current gesture
	// light
	uint8_t lightOn = 0;
	uint8_t userMode = 0;		// mode chosen by the user, 0..4
	uint8_t lowBat = 0;			// below 10%: eco only, latched until off
	uint8_t measTime = 0;		// ticks since the last battery check
	uint8_t dutyFull = 0;		// PWM duty for 100% intensity
	// pattern player
	Pattern pat = {};
	uint8_t plTimer = 0;		// ticks left in the current step, 0 = stopped
	uint8_t plIdx = 0;			// flash number inside the group

	wakeUp();

	for (;;) {
		sleep_cpu();				// idle until the next timer overflow
		if (++ovfCount < TICK_OVF) continue;
		ovfCount = 0;
		wdt_reset();

		// ------------------------------------------------ button
		// SP: one press shorter than BTN_LP_MS. With the light on it is
		//     reported after BTN_DP_WINDOW_MS without a second press,
		//     with the light off at once (DP has no function there).
		// DP: second press starts within BTN_DP_WINDOW_MS after the first
		//     release, reported on the second release.
		// LP: held for BTN_LP_MS (also on the second press), reported
		//     while still held; the release is then ignored.
		uint8_t ev = EV_NONE;
		uint8_t raw = !(PINB & (1 << butPin));
		if (raw != btnStable) {
			if (++btnCnt >= MS2T(BTN_DEBOUNCE_MS)) {
				btnStable = raw;
				btnCnt = 0;
				btnTime = 0;
				if (raw) clicks++;
			}
		} else {
			btnCnt = 0;
		}
		btnTime++;		// may wrap after a long press, clicks is 0 then
		if (btnStable) {
			if (btnTime == MS2T(BTN_LP_MS) && clicks) {
				ev = EV_LP;
				clicks = 0;
			}
		} else if (clicks) {
			if (clicks >= 2) ev = EV_DP;
			else if (!lightOn || btnTime >= MS2T(BTN_DP_WINDOW_MS)) ev = EV_SP;
			if (ev) clicks = 0;
		}

		// ------------------------------------------------ battery
		// Light off: measured at rest, on SP and LP.
		// Light on: about once per second, under load: in steady modes at
		// any time, in flashing modes in the last tick of a flash.
		uint8_t ledIsOn = TCCR0A & (1 << COM0A1);
		measTime++;		// may wrap while off, harmless
		uint8_t meas;
		if (lightOn) {
			meas = measTime >= BAT_CHECK_TICKS && ledIsOn
				&& (!pat.on || plTimer == 1);
		} else {
			meas = (ev == EV_SP || ev == EV_LP);
		}
		uint16_t raw16 = 0;				// raw ADC sum, kept for calibration
		uint8_t vbat = 0;				// battery voltage as V8(mV)
		if (meas) {
			measTime = 0;
			raw16 = adcBattery();
			uint16_t sum = raw16;
			// mv8 = sum * scale >> 17, shift-and-add (smaller than a 32-bit
			// multiply); scale < 32768 keeps the 16-bit accumulator safe
			uint16_t mv8 = 0;
			for (uint8_t i = 0; i < 12; i++) {
				if (sum & 1) mv8 += scale;
				sum >>= 1;
				mv8 >>= 1;
			}
			mv8 >>= 5;
			// below 3000 mV reads as 0; above 5040 mV is impossible for Li-ion
			if (mv8 > 375) vbat = mv8 - 375;
			// brightness compensation: lower duty above LED_VFULL_MV
			if (mv8 <= LED_VFULL_MV / 8) dutyFull = 255;
			else dutyFull = DUTY_NUM / (mv8 - LED_VF_MV / 8);
		}

		// ------------------------------------------------ events
		uint8_t row = PAT_NONE;
		if (!lightOn) {
			if (ev == EV_SP) {
				// battery indication
				eeWrite(EE_ADCSUM, raw16);			// for calibration, see README
				eeWrite(EE_ADCSUM + 1, raw16 >> 8);
				row = PAT_BAT1;
				if (vbat >= V8(BAT_25_MV)) row++;
				if (vbat >= V8(BAT_50_MV)) row++;
				if (vbat >= V8(BAT_75_MV)) row++;
			} else if (ev == EV_LP) {
				if (vbat < V8(BAT_CUTOFF_MV)) {
					row = PAT_BAT1;					// empty: one flash at 50%
				} else {
					userMode = eeRead(EE_MODE);
					if (userMode >= MODE_COUNT) userMode = 0;
					lowBat = (vbat < V8(BAT_10_MV));
					lightOn = 1;
					row = lowBat ? 0 : userMode;	// below 10%: eco only
				}
			}
		} else {
			if (ev == EV_LP) {
				eeWrite(EE_MODE, userMode);
				lightOn = 0;
			} else if (!lowBat) {
				if (ev == EV_SP) {
					// next mode inside the block
					userMode++;
					if (userMode == BLOCK2_FIRST) userMode = 0;
					else if (userMode == MODE_COUNT) userMode = BLOCK2_FIRST;
					row = userMode;
				} else if (ev == EV_DP) {
					// other block, first mode
					userMode = (userMode < BLOCK2_FIRST) ? BLOCK2_FIRST : 0;
					row = userMode;
				}
			}
			if (meas) {
				if (vbat < V8(BAT_CUTOFF_MV)) {
					lightOn = 0;					// empty: off, no flash
				} else if (!lowBat && vbat < V8(BAT_10_MV)) {
					lowBat = 1;
					row = 0;
				}
			}
			if (!lightOn) {
				plTimer = 0;
				ledOff();
			}
		}

		// ------------------------------------------------ pattern player
		if (row != PAT_NONE) {
			memcpy_P(&pat, &patTable[row], sizeof(pat));
			plIdx = 0;
			ledOff();
			if (pat.on) {
				plTimer = 1;				// first flash starts on the next tick
			} else {
				plTimer = 0;				// steady light
				ledOn();
			}
		} else if (plTimer && !--plTimer) {
			if (ledIsOn) {
				ledOff();
				if (++plIdx < (pat.count & ~HALF)) {
					plTimer = pat.off;
				} else if (pat.gap) {
					plIdx = 0;
					plTimer = pat.gap;
				}							// else: one-shot pattern finished
			} else {
				ledOn();
				plTimer = pat.on;
			}
		}
		// dutyFull is never below 132 (Vbat <= 5 V), so 50% is never 0
		OCR0A = (pat.count & HALF) ? (dutyFull >> 1) : dutyFull;

		// ------------------------------------------------ sleep
		if (!(lightOn | plTimer | btnStable | btnCnt | clicks)) {
			powerDown();			// off, nothing playing, button released
		}
	}
}

void loop() {
}
