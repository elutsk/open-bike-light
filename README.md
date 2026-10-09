# Open Bike Light - firmware for ATtiny13A

Firmware for the cycling club tail light, hardware schematic revision `circuit2`.
This document has two parts:

1. **User manual** - how to use the light.
2. **Builder's guide** - how to set up the software, flash, calibrate and test a board.

---

# Part 1. User manual

## 1.1 What it is

A tail bicycle light with one red LED, one button and a built-in Li-ion battery
that is charged over USB-C. The light has 5 light modes in 2 groups ("blocks"),
remembers the last used mode, shows the battery level, and protects the battery
from deep discharge on its own.

## 1.2 The button

There is only one button. It knows three kinds of presses:

| Press | Name | How to do it |
|---|---|---|
| Short press | SP | Press and release in less than 1 second. |
| Double press | DP | Two short presses quickly, the second one starting less than 0.3 s after you let go of the first. |
| Long press | LP | Hold the button for 1 second. The light reacts while you are still holding it; you can let go after that. |

With the light on, a short press takes effect about 0.3 s after you release the
button (the light waits to see whether a second press follows).

## 1.3 Light off

| Press | What happens |
|---|---|
| SP | Shows the battery level (see 1.5). |
| DP | Nothing special (it is seen as two short presses: the battery level is shown twice). |
| LP | Switches the light on in the mode that was used last time. The very first time it starts in Block 1, mode 1 (Eco). |

When switching on, the battery is checked first:

1. Battery below 10%: the light starts in Eco mode and stays in Eco (see 1.6).
2. Battery empty: the LED gives **one short dim flash** and the light stays off.
   This means the light works but the battery must be charged.

## 1.4 Light on

| Press | What happens |
|---|---|
| SP | Next mode inside the current block (after the last one it starts again from the first). |
| DP | Switches to the other block, always starting with its first mode. |
| LP | Remembers the current block and mode, and switches the light off. |

### Block 1 - flashing (best for daytime and to be seen)

| Mode | Name | Pattern |
|---|---|---|
| 1 | Eco | One short flash (50 ms) every second. Uses the least battery. |
| 2 | Normal | A burst of three short flashes, then a 0.6 s pause. |
| 3 | Hi-vis | Long flash 0.2 s, pause 0.4 s. |

### Block 2 - steady light (for group rides, so you do not blind the rider behind)

| Mode | Name | Brightness |
|---|---|---|
| 1 | Ride | 50% |
| 2 | Beam | 100% |

Brightness stays the same while the battery goes from full down to about 25%.
Below 25% the light slowly becomes dimmer.

## 1.5 Battery level

With the light **off**, make a short press. The main LED shows:

| What you see | Battery |
|---|---|
| 4 bright flashes | 75..100% |
| 3 bright flashes | 50..75% |
| 2 bright flashes | 25..50% |
| 1 dim flash | below 25% - charge soon |

The levels follow the real discharge curve of a Li-ion cell, not a straight
voltage scale, so "half" really means about half of the riding time is left.
The reading is most accurate when the light has been off for a minute.

## 1.6 Low battery and empty battery

1. **Below 10%** the light switches to Eco mode by itself and does not let you
   choose other modes until it is switched off. This is on purpose: Eco gives
   the longest possible time to get home with some light. If the light ignores
   your presses, this is the reason. Your favourite mode is still remembered.
2. **Empty** (the battery reaches the safe minimum) the light switches itself
   off without any warning flash. Charge the battery.

## 1.7 Charging

1. Plug a USB-C cable into the light. Any USB charger or computer port will do,
   including USB-C to USB-C cables.
2. One small indicator LED shows "charging", the other shows "full". With no
   battery connected both may blink; this is normal.
3. A full charge of an empty battery takes roughly 3 hours with the default
   charge current (175 mA).
4. The light may be used while charging, but then the charger may not
   recognise that the battery is full. For a proper full charge, switch the
   light off.

## 1.8 Storage

When off, the light uses almost no power and can stay in a bag for months.
For long storage, charge it to about half (2 or 3 flashes) and check it every
few months.

---

# Part 2. Builder's guide

## 2.1 Safety rules - read first

1. **Never power the board from the programmer.** VCC is connected directly to
   the Li-ion cell. 5 V from a programmer goes straight into the battery.
   On a USBasp, remove the "target power" jumper. Keep a dedicated club ISP
   cable with the VCC wire cut and labelled. Programmers that only *sense*
   the target voltage (AVRISP mkII, Atmel-ICE) are safe as they are.
2. **Never program the RSTDISBL or WDTON fuses.** RSTDISBL disables ISP
   (only a high-voltage programmer can recover the chip). WDTON makes the
   watchdog impossible to switch off before sleep. The Arduino menus below
   never set them; do not edit fuses by hand.
3. Use **protected cells only** (cells with their own protection board).
4. For the very first power-up of a new board, use a bench supply set to
   3.7 V with a current limit of about 100 mA instead of the battery.

## 2.2 What you need

1. A PC with **Arduino IDE 2.3.6**.
2. An ISP programmer: USBasp (with the power jumper removed), USBtinyISP
   (not powering the target), AVRISP mkII or Atmel-ICE.
3. A **SOIC-8 test clip** with a cable to the programmer's 6-pin or 10-pin ISP connector.
4. A charged protected Li-ion cell connected to the board (the board is
   powered by its own battery while programming).
5. A multimeter (for calibration).

## 2.3 Install the software (once per PC)

1. Install Arduino IDE 2.3.6 from arduino.cc.
2. Open **File > Preferences**. In **Additional boards manager URLs** add:
   `https://mcudude.github.io/MicroCore/package_MCUdude_MicroCore_index.json`
   (if there is already a URL, put the new one on a new line). Click OK.
3. In the same Preferences window tick **Show verbose output during: upload**.
   You will need it for calibration.
4. Open **Tools > Board > Boards Manager**, search for **MicroCore**, select
   version **2.5.2** and click Install.
5. On Windows with a USBasp you may need a USB driver (for example installed
   with the Zadig tool, driver "libusbK" or "WinUSB"). Linux may need a udev
   rule to use the programmer without root.

## 2.4 Open the project

1. The folder must be named `OpenBikeLight` and contain `OpenBikeLight.ino`
   (Arduino requires the folder and the .ino file to have the same name).
2. Open `OpenBikeLight.ino` in Arduino IDE.

## 2.5 Per-build parameters

At the top of `OpenBikeLight.ino` there is a block **PER-BUILD PARAMETERS**.
Check it for every build. Everything below "INTERNALS" stays as it is.

| Parameter | Default | Meaning |
|---|---|---|
| `LED_VF_MV` | 2300 | Forward voltage of the main LED in mV at its working current. Red LEDs: 2100..2400. |
| `LED_VFULL_MV` | 3720 | Battery voltage at which the LED reaches full brightness with 100% PWM. R9 is calculated for this voltage. Keep it equal to `BAT_25_MV` unless you know why not. |

### Choosing R9

R9 is the only part a builder changes to match the LED. Pick the LED current
you want (`I`, in amps) and calculate:

`R9 = (LED_VFULL_MV - LED_VF_MV) / 1000 / I`

Example: 1 W red LED, Vf = 2.3 V, 150 mA: R9 = (3.72 - 2.30) / 0.150 = 9.5 ohm,
use 10 ohm (gives 142 mA).

Then check two things:

1. **Peak LED current** at a full battery: `(4.2 - Vf) / R9`. With 10 ohm it is
   190 mA. The LED must accept this as a pulse current.
2. **Heat in R9**: `I x (4.2 - Vf)` watts, worst case with a full battery.
   With 10 ohm: 0.142 x 1.9 = 0.27 W. Use a 2512 resistor (two in series if
   you want to spread the heat).

Typical LED currents (use the lower value when unsure):

| LED type | Current per LED |
|---|---|
| 5 mm through-hole | 20 mA |
| SMD 3528 | 20 mA |
| SMD 5050 (3 chips) | 60 mA |
| "Piranha" / superflux, 4 legs | 50..70 mA |
| SMD 2835 | 60..150 mA |
| 0.5..1 W on star | 150..350 mA |

Example for a 5 mm red LED at 20 mA, Vf = 2.0 V: R9 = (3.72 - 2.00) / 0.020 =
86 ohm, use 82 or 91 ohm, and set `LED_VF_MV` to 2000.

**The 10 ohm default destroys small LEDs (5 mm, 3528).** Always set R9 first.

If several LEDs are in parallel, each needs its own resistor.

## 2.6 Arduino settings - must be exact

Connect the programmer to the PC, then set in the **Tools** menu:

| Menu | Value |
|---|---|
| Board | MicroCore > **ATtiny13** |
| BOD | **BOD 2.7V** |
| EEPROM | **EEPROM retained** |
| Clock | **9.6 MHz internal osc.** |
| Bootloader | **No bootloader** |
| Programmer | your programmer, e.g. **USBasp** (not a "slow" one) |

These settings decide the fuses: low fuse **0x3A**, high fuse **0xFB**.

**Important:** with MicroCore, every "Upload Using Programmer" writes the fuses
again from these menus, not only "Burn Bootloader". If a menu is wrong when you
upload, the chip gets the wrong fuses. Check the menus every time.

## 2.7 Check that it compiles

Click **Verify** (the tick button). At the end of the output you should see the
flash use, about **996 of 1024 bytes**. If it reports that the sketch is too
big, something in the settings is wrong (wrong board or clock) or the code was
changed.

## 2.8 Connect the programmer

1. Make sure the programmer does **not** supply power (section 2.1).
2. Connect a charged battery (or the bench supply) to the board.
3. Put the SOIC-8 clip on U1. **Pin 1** of the clip (usually the red wire)
   goes to pin 1 of the ATtiny13A (the corner with the dot on the chip).
   The clip must sit straight, with all 8 contacts on the pins.
4. During programming the LED may flicker. This is normal: the MOSI line is
   also the LED gate.

## 2.9 First programming of a new chip

Do these steps in this order, once per new chip:

1. **Tools > Burn Bootloader.** With "No bootloader" this only writes the fuses
   (9.6 MHz, BOD 2.7 V, EEPROM retained). It also erases the chip.
   Wait for "Done burning bootloader".
2. **Sketch > Upload Using Programmer** (Ctrl+Shift+U). Wait for "Done uploading".
   Do not use the normal "Upload" button.
3. Remove the clip.
4. Calibrate (section 2.11).
5. Test (section 2.12).

## 2.10 Updating the firmware later

Only step 2 of 2.9: **Sketch > Upload Using Programmer**, with the menus set as
in 2.6. Calibration and the saved mode stay in EEPROM because of
"EEPROM retained".

## 2.11 Calibration (once per board)

### Why

The ATtiny13A measures the battery against its internal 1.1 V reference. This
reference differs from chip to chip by up to +-10%. Without calibration the
battery reading can be off by up to 0.4 V, which makes the battery indicator,
the low-battery switch and the cut-off wrong. One number stored in EEPROM fixes
this.

### How it works

Every time you make a short press with the light off (battery indication), the
firmware stores its raw battery reading (`SUM`) in EEPROM. You measure the same
battery with a multimeter, read `SUM` from the chip with the programmer,
calculate the calibration factor `SCALE` and write it back.

### Step 1 - find your avrdude command

Arduino IDE has avrdude inside. The easiest way to get the right command:

1. Make sure "Show verbose output during upload" is on (section 2.3).
2. Do an "Upload Using Programmer".
3. In the output window find the long line that starts with the path to
   `avrdude` and contains `-C` and `-p attiny13a` (or `-pattiny13a`). Copy:
   the path to avrdude, the `-C...` part with the config file, the `-p...` part
   and the `-c...` part (the programmer).

Below this is written as:

```
"<AVRDUDE>" "-C<CONFIG>" -pattiny13a -c<PROGRAMMER>
```

Replace these with what you copied. Run the commands in a terminal
(Windows: Command Prompt; macOS/Linux: Terminal).

### Step 2 - take the reading

1. Remove the clip. The battery must be connected and the light **off**.
2. Wait at least one minute after the light was last on.
3. Measure the battery voltage with the multimeter **directly on the battery
   terminals** of the board (BT1 + and -). Write it down in **millivolts**,
   for example 3987 mV. Use a meter that shows at least 3 decimals in volts.
4. Make **one short press**. The LED shows the battery level and, at the same
   moment, the firmware stores `SUM` in EEPROM.
5. Measure the voltage again. If it differs from the first value, use the
   average.

### Step 3 - read SUM from the chip

1. Put the clip on (programmer not powering the board).
2. Run:

```
"<AVRDUDE>" "-C<CONFIG>" -pattiny13a -c<PROGRAMMER> -U eeprom:r:eeprom.txt:h
```

3. Open `eeprom.txt` (it is in the folder where you ran the command). It
   contains the 64 EEPROM bytes in order, starting with byte 0, like this
   (the exact line layout can differ between avrdude versions):

```
0xff,0xff,0x8a,0x0c,0x03,0xff,...
```

Byte 0 and 1 are the calibration factor (0xff,0xff = not calibrated yet),
**byte 2 and 3 are SUM** (low byte first), byte 4 is the saved light mode.

In this example SUM = 0x0C8A. Convert to decimal: 0x0C8A = 3210
(Windows calculator in "Programmer" mode does this).

### Step 4 - calculate SCALE

```
SCALE = Vbat_mV x 16384 / SUM
```

Example: 3987 x 16384 / 3210 = 20350 (round to a whole number).

**Check:** SCALE must be between **17874 and 23518**. The nominal value is
20696. A value outside this range means a mistake (wrong voltage unit, bytes
swapped, wrong reading); the firmware ignores such a value and uses the
nominal reference.

Convert SCALE to hexadecimal: 20350 = 0x4F7E. The **low byte** is 0x7E, the
**high byte** is 0x4F.

### Step 5 - write SCALE

```
"<AVRDUDE>" "-C<CONFIG>" -pattiny13a -c<PROGRAMMER> -U eeprom:w:0x7E,0x4F:m
```

Low byte first, then high byte. This writes EEPROM addresses 0 and 1. Some
avrdude versions may reset the other EEPROM bytes (saved mode, SUM) while doing
this; that is harmless, the light will simply start in Eco the next time.

### Step 6 - verify

1. Read the EEPROM again (step 3) and check that bytes 0 and 1 are your values.
2. Remove the clip. When avrdude finishes it releases RESET, so the chip
   restarts and reads the new calibration factor by itself.
3. Make a short press with the light off and check that the number of flashes
   matches the multimeter reading and the table in 2.13.

Write the SCALE value on the board or in the club build log. If the chip ever
loses its EEPROM, you can write the same value again without measuring.

## 2.12 Test after building

1. **Sleep current.** Light off, multimeter in the uA range in series with the
   battery. After a few seconds it must read below about 1 uA (plus the
   battery protection board's own consumption, if you measure outside it).
   A much higher value means a solder bridge or a wrong part, most often around
   the divider (R11, R7) or the USB-C/charger section.
2. **Battery indication.** Short press: 1..4 flashes, as in 1.5.
3. **Switching on.** Long press: Eco (first time).
4. **All modes.** Short presses go through Eco > Normal > Hi-vis; double press
   goes to Ride; short press Ride > Beam.
5. **Memory.** Long press off in Beam, long press on: starts in Beam.
6. **Thermal test.** Beam mode for 15 minutes with the housing closed. It must
   be comfortably warm, not hot. If it gets hot, increase R9.
7. **Charging.** Plug in USB-C, the charge indicator must light.

## 2.13 Battery thresholds

The firmware uses these battery voltages (defines at the top of the .ino):

| Define | mV | Meaning |
|---|---|---|
| `BAT_75_MV` | 3970 | 4 flashes at and above |
| `BAT_50_MV` | 3850 | 3 flashes at and above |
| `BAT_25_MV` | 3720 | 2 flashes at and above, below: 1 dim flash |
| `BAT_10_MV` | 3650 | below: Eco only |
| `BAT_CUTOFF_MV` | 3100 | below: off (empty) |

The thresholds follow a typical Li-ion open-circuit voltage curve. The same
thresholds are used for readings at rest (battery indication, switching on) and
under load (while riding). The cut-off at 3.1 V leaves a margin for cold
weather; the battery's own protection board is the last safety level below that.

## 2.14 Troubleshooting

| Problem | Likely cause and fix |
|---|---|
| avrdude: "target doesn't answer", "initialization failed" | Clip not seated or pin 1 reversed; battery not connected; programmer SCK too fast for the chip (on USBasp try adding `-B 32` to the command, or use the "USBasp slow" programmer in Arduino for one Burn Bootloader). |
| avrdude: "device signature = 0x000000" | Same as above. Check every contact of the clip. |
| Sketch too big | Wrong board (must be MicroCore ATtiny13) or a changed source. |
| LED never lights, battery check gives nothing | Battery empty or below cut-off; R9 open; Q1 not soldered; LED reversed. |
| LED is always on at full brightness | Q1 shorted or R10 missing. Disconnect the battery at once. |
| Battery level always wrong | Not calibrated, or SCALE outside the valid range (then the nominal value is used). Redo 2.11. |
| Light ignores button presses while on | Battery below 10%: Eco only (see 1.6). Charge it. |
| Light switches off by itself | Battery empty. Charge it. |
| Saved mode and calibration lost after upload | EEPROM menu was "EEPROM not retained". Set "EEPROM retained", Burn Bootloader once, upload, calibrate again (or write the known SCALE). |
| Light resets or flickers when cold | Cold, old cell sagging under load below the BOD level (2.7 V). Use a better cell or a larger R9. |

---

# Part 3. How the firmware works

This part is for whoever maintains or adapts the code.

## 3.1 Overview

1. Clock 9.6 MHz internal RC. Timer0 runs in fast PWM mode with prescaler 8:
   PWM 4.69 kHz on OC0A (PB0), well above visible flicker.
2. The CPU sleeps in **idle** between Timer0 overflows; 47 overflows make one
   **10 ms tick**. All timing (button, patterns, battery checks) counts ticks.
3. With the light off and nothing to do, the MCU goes to **power-down**: LED
   off, divider off, ADC off, Timer0 stopped, watchdog off, BOD off during
   sleep (BODCR timed sequence). Only the button pin change wakes it.
4. Every reset (power-on, brown-out, watchdog, external) ends in the off state.
   Before `main()` runs, `earlyInit()` (section `.init3`) clears MCUSR and stops
   the watchdog, so a watchdog reset cannot cause a reset loop. All pins are
   high-Z after reset and R10 keeps the MOSFET off without any code.
5. The watchdog runs with a 2 s timeout while awake and is reset only in the
   main loop, once per tick. If the firmware hangs, the chip resets and the LED
   goes off.

## 3.2 Pins

| Pin | Port | Use |
|---|---|---|
| 1 | PB5 | RESET, 10k pull-up (needed for ISP) |
| 2 | PB3 | Button to GND, internal pull-up, pin-change wake-up |
| 3 | PB4 | ADC2, battery divider midpoint, digital input disabled |
| 5 | PB0 | OC0A PWM to the MOSFET gate |
| 6 | PB1 | Not connected, internal pull-up on (no floating input) |
| 7 | PB2 | Divider enable: high while awake, low in sleep |

## 3.3 Button

Debounce: the input must be stable for 50 ms (`BTN_DEBOUNCE_MS`).
Gestures follow the scheme used by the OneButton library and the Anduril
flashlight firmware:

1. The long-press time (`BTN_LP_MS`, 1000 ms) is counted from the moment the
   button is pressed. Long press fires while the button is still held, also
   on the second press of a double press.
2. The double-press window (`BTN_DP_WINDOW_MS`, 300 ms) is counted from the
   release of the first press. Double press fires on the second release.
3. With the light on, a short press is reported when the window expires. With
   the light off a double press has no function, so a short press is reported
   immediately on release.

## 3.4 Battery measurement

1. Divider R11 1M / R7 270k with C4 10 nF, read on ADC2 against the internal
   1.1 V reference. Four conversions are summed (`SUM`, 0..4092) after one
   discarded conversion.
2. Voltage: `Vbat_mV = SUM x SCALE / 16384`. `SCALE` comes from EEPROM
   (calibration), nominal 20696. It is handled in 8 mV steps internally.
3. Light off: measured at rest, on a short press (battery indication) and on a
   long press (switching on).
4. Light on: about once per second, under load. In steady modes at any time;
   in flashing modes in the last 10 ms of a flash. C4 averages the 4.69 kHz
   PWM, so the reading is the average battery voltage under load.
5. Below `BAT_10_MV` the light is forced to Eco until it is switched off
   (latched, so the voltage recovering in Eco cannot switch it back and forth).
   Below `BAT_CUTOFF_MV` it switches off.

## 3.5 Brightness compensation

R9 is chosen for full brightness at `LED_VFULL_MV`. With a higher battery
voltage the peak current is higher, so the PWM duty is reduced:

`duty = 255 x (LED_VFULL_MV - LED_VF_MV) / (Vbat - LED_VF_MV)`, at most 255.

The average LED current, and so the brightness, stays constant from full down
to `LED_VFULL_MV`. 50% intensity is half of this duty. The duty is updated
with every battery measurement.

## 3.6 Patterns

All light output (modes, battery indication, "empty" flash) is described by
the table `patTable`: flash length, pause between flashes, flashes per group
(plus a flag for 50% intensity), pause after the group. A pattern with a pause
after the group repeats; with no pause it plays once. Flash length 0 means
steady light. Times are in 10 ms ticks.

## 3.7 EEPROM map

| Address | Size | Content | Written by |
|---|---|---|---|
| 0..1 | 16 bit, low byte first | Calibration factor SCALE (valid 17874..23518) | You, with avrdude |
| 2..3 | 16 bit, low byte first | SUM of the last battery indication | Firmware, on every short press with the light off |
| 4 | 8 bit | Saved light mode 0..4 | Firmware, on long press off |

Light mode numbers: 0 Eco, 1 Normal, 2 Hi-vis, 3 Ride, 4 Beam.
EEPROM is only written when the value changes.

## 3.8 Flash size

The ATtiny13A has 1024 bytes of flash. The firmware uses about 996 bytes. To
fit:

1. All state is kept in local variables of `setup()`, which runs its own
   endless loop, so the compiler keeps it in registers (`loop()` is empty).
2. A `#pragma GCC optimize` line turns off a few optimisations that duplicate
   code.
3. Battery maths uses a small shift-and-add multiply and 8 mV steps instead of
   32-bit arithmetic.
4. Timer0 overflow only wakes the CPU (empty interrupt); the main loop counts
   the wake-ups.

Any new feature has to save space somewhere else. After every change, check
the size reported by Verify.
