# Module bay wiring mod: keeping the SoC pins safe

Module bay control (docs/module-bay.md) needs a hardware mod: three wires on the
bay board from three pins of the connector that plugs into the goggle to the
three channel-select positions (CS1-CS3) of the 9-pin analog module connector
([pkendall64's wiki](https://github.com/pkendall64/hdzero-goggle/wiki/Analog-Module-Backpack-Control);
its pictures show which pins). Those goggle-side pins are SoC pins **PG0, PG1,
PG2** of the Allwinner V536, and they are also the **WiFi SDIO bus** (PG0 = SDC1
CLK, PG1 = CMD, PG2 = D0). The mod is documented for the original Goggle; on the
Goggle 2 it is unproven, and PG0-PG2 there are the built-in WiFi chip's bus.

The mod connects these pins to whatever the analog module does with its CS
pins, **whatever the goggle firmware does**: a module that drives 5 V reaches
the SoC even with Pin Control Off. This page lists what can go wrong, puts
numbers on it, and ends with the wiring recommendation and a bench checklist.
Everything marked *measure* has not been seen on hardware.

## The numbers used here

**V536** (V536-H/V526 datasheet rev. 1.6, Allwinner, 2023: Tables 2-3, 2-4, 2-5;
PIO registers p. 886-888):

| | |
|---|---|
| VCC-PG (PG bank supply), operating | 1.62-1.98 V (1.8 V) or 2.97-3.63 V (3.3 V); absolute max 3.96 V |
| VIH / VIL | ≥ 0.7 × VCC / ≤ 0.3 × VCC; input max VCC + 0.3 V |
| VOH / VOL | ≥ VCC - 0.2 V / ≤ 0.2 V (no load current stated) |
| internal pull-up / pull-down | 80-120 kΩ |
| input leakage, CIN | ≤ 10 µA, ≤ 5 pF |
| ESD | HBM ±2 kV, CDM ±500 V |
| latch-up | JESD78E, ±200 mA pulse per pin, 1.5 × Vddmax: a qualification test, **not** a rating |

The datasheet gives **no continuous injection (clamp-diode) current limit** and
no output current per drive level. This page uses ≤ 1 mA per pin as the design
limit for current forced into a pin's clamp diodes: a conservative choice for a
CMOS pin when the datasheet is silent, not an Allwinner number.

**ATmega328P at 5 V** (the True-D's MCU; Microchip datasheet): VIH ≥ 0.6 × VCC =
3.0 V, VIL ≤ 0.3 × VCC = 1.5 V, I/O pull-up 20-50 kΩ (the True-D's ~35 kΩ),
pin voltage max VCC + 0.5 V, 40 mA per pin absolute max; a push-pull output
high is ~5 V with a few tens of Ω source resistance.

**PG bank supply: 1.8 V or 3.3 V is not established.** The official system
images (sys_config.fex, byte-identical in the Goggle 1 package
HDZERO_GOGGLE-75-200-9.5.0 and the Goggle 2 package HDZERO_GOGGLE2-76-176-9.5.1,
and the same regulator list in their device trees) put `vcc-pg` on the AXP2101's
BLDO1 together with only 1.8 V-named loads (`vcc18-pc vcc-lvds vcc18-lcd ...
vcc18-wifi`), and the SDIO host has UHS modes listed (which need 1.8 V
signalling). That points to **1.8 V**. But `[power_sply]` does not set BLDO1
(`;bldo1_vol = 1001800` is commented out), and the AXP2101's BLDO1 default comes
from its eFuse (AXP2101 datasheet 1.4, REG 96), so the files do not prove it.
The SoC itself reports it: **PIO_POW_VAL (0x0300B348) bit 6 = 1: PG supply below
2.0 V, 0: above 2.5 V**. Module Bay > **Check Pins** shows that as "PG bank 1.8 V"
or "3.3 V"; by hand: `echo 0x0300B348 > /sys/class/sunxi_dump/dump; cat
/sys/class/sunxi_dump/dump`. *Measure* it on each goggle before relying on bay
control.

## What can put harmful voltage or current on PG0-PG2

Currents below take the SoC clamp diode's forward voltage as 0.3 V (the worst
case: a lower drop means more current); R is the series resistor recommended
further down (4.7 kΩ).

### 1. A module that drives a CS pin at 5 V

Who does it:
* a True-D with firmware older than MOD 1.16, or MOD 1.16 in BAY:AUTO: D13
  push-pull at 5 V for ~6 s after every power-up (the stock goggle link);
* the True-D's Arduino bootloader (ATmegaBOOT): D13 is an output for its LED
  flash at every reset, whatever the firmware (MOD 1.17 makes it an input in
  the first instructions after the bootloader, not before);
* any module whose CS pins are outputs (unknown for most modules, e.g. the
  Foxeer Wildfire).

| | 3.3 V bank | 1.8 V bank |
|---|---|---|
| no resistor: into the clamp diode to VCC-PG | tens of mA, set only by the two drivers (≈ (5 - 3.6) V / ~40 Ω); far above any injection limit, above the AVR's own 40 mA | the same, (5 - 2.1) V / ~40 Ω |
| R = 4.7 kΩ: (5 - VCC - 0.3) / R | **0.30 mA** | **0.62 mA** |
| R = 4.7 kΩ, goggle drives the line low meanwhile: 5 V / R | 1.06 mA in both drivers | 1.06 mA |

The pin also sits above its VIH maximum (VCC + 0.3 V) while this lasts; the
resistor turns that into a bounded clamp current. A bootloader flash or a
6 s link phase happens at every module power-up, so this is the reason the
resistor is not optional.

### 2. A module's pull-ups to 5 V

A True-D MOD 1.16 (BAY:1-8 / LINK) or 1.17 has ~35 kΩ pull-ups on all three
pins (ATmega328P: 20-50 kΩ). With the goggle pin not driven, current flows
through the pull-up into the SoC's clamp: at most (5 - 3.6) V / 20 kΩ = 70 µA
(3.3 V bank) or (5 - 2.1) V / 20 kΩ = 145 µA (1.8 V bank) per pin without a
resistor, 57 µA / 117 µA with 4.7 kΩ. Small, but continuous whenever the module
is powered, also with Pin Control Off and while WiFi uses the lines.

### 3. The goggle driving into an unpowered module

The module's input clamp diodes then feed its unpowered supply: without a
resistor limited only by the SoC's output (no datasheet figure; milliamps to
tens of milliamps), which can half-power the module's MCU. With 4.7 kΩ: ≤
(3.3 - 0.3) V / R = 0.64 mA (3.3 V bank), 0.32 mA (1.8 V bank). When it happens:
* bay power-off (batch 2 hardware): the power thread switches the bay off
  first, then the module worker releases the pins: up to one transfer step
  later (≤ ~12 ms for True-D, rapidFIRE, Switch, SteadyView register access;
  ≤ 70 ms during the SteadyView power pulse; SteadyView-X: up to its insmod
  plus ~100 ms + rmmod), docs/module-bay.md "Releasing the pins";
* SteadyView's mode set, by design: the mode is latched at power-up, so CLK and
  DATA (and CS, high) are driven during its 70 ms bay-off pulse;
* batch 1 Goggle 1 has no bay power switch (the bay is powered with the goggle).

### 4. The pins are the WiFi SDIO bus

* Original Goggle: PG0-PG5 go through the bay connector to the Expansion Module
  V2's WiFi chip. Goggle 2: to the built-in WiFi chip (`vcc18-wifi`: its I/O is
  probably 1.8 V too). With the mod, the module's CS pins hang on CLK, CMD and
  D0 of that bus: its pull-ups (case 2) and any 5 V drive (case 1) also reach
  the WiFi chip's pins. The resistor protects the SoC **and** the WiFi chip only
  if it sits between the bus and the wire: at the goggle-side connector pin.
* Load: a module input (~10 pF) and 10-20 cm of wire (~1 pF/cm) on lines clocked
  at up to 50 MHz (`max-frequency` of sdc1 in the device tree) would slow the
  edges and can break WiFi. Behind 4.7 kΩ the wire and module are decoupled
  (R·C ≈ 0.1 µs, against a 20 ns clock period); only the few millimetres before
  the resistor load the bus.
* While WiFi runs, the module sees SDIO traffic and idle levels on its CS pins:
  a module that takes a static 3-bit code (True-D BAY:1-8, Fat Shark style) may
  change channel; a rapidFIRE listener accepts only frames with the right
  length and checksum (True-D MOD 1.17 also ignores pin storms). Fly analog with
  module control with WiFi off.
* The goggle firmware never drives PG0-PG2 while WiFi is enabled or while they
  are in their SDIO function (module.c `bay_pins_busy()`, docs/module-bay.md).

### 5. Goggle firmware paths

From the code (src/module/): the pins are driven only by the module types'
functions, which only the worker calls, and only while a type is set, Pin
Control is On, WiFi is off, the pins are not in the SDIO function and the bay is
powered (batch 1: always). Exceptions: the SteadyView power pulse (case 3) and
the release delay after a power-off (case 3). They are inputs: at boot (PG_CFG0
resets to "IO disabled", rc.sh keeps PG0-PG5 so: `0x0300B0D8 = 0x22777777`),
after every release (close(): direction "in", unexported; SteadyView-X: soft_uart
unloaded, then the same), on every abort path, and during Check Pins (read only).
If the goggle app dies while a Switch code is on the pins, the sysfs exports keep
it there until reboot: the same levels as while it ran. After WiFi has been used,
PG0-PG5 may keep the SDIO configuration's internal 100 kΩ pull-ups (sys_config
`<2><1><3>`: function 2, pull-up, drive level 3).

## Wiring recommendation

To be checked on the bench (next section) before it is relied on.

Constraints on the series resistor R in each wire (module side: AVR at 5 V,
pull-up Rpu 20-50 kΩ, or none):

1. clamp current ≤ 1 mA when a module drives 5 V:
   R ≥ (5 - VCC - 0.3) V / 1 mA = **1.4 kΩ** (3.3 V bank), **2.9 kΩ** (1.8 V bank);
2. the goggle's low must read low at the module through the strongest pull-up:
   5 V × R / (R + 20 kΩ) ≤ 1.5 V → **R ≤ 8.5 kΩ**;
3. the goggle's high must read high at the module (VIH 3.0 V): the line settles
   at (VCC × Rpu + 5 V × R) / (R + Rpu):
   * 3.3 V bank: ≥ 3.3 V for any R (the pull-up only raises it). Without a
     pull-up in the module it is 3.3 V: 0.3 V over the AVR's 3.0 V (at a 5.5 V
     module supply VIH is 3.3 V: no margin).
   * 1.8 V bank: needs R ≥ 0.6 × Rpu = 12-30 kΩ, which contradicts 2. **A
     resistor alone cannot make a 1.8 V bank drive a 5 V AVR input high.**
4. speed: R × (module + wire ≈ 25 pF) = 0.12 µs at 4.7 kΩ, far below the
   shortest step the goggle produces (~8 µs rapidFIRE clock phase, 8.7 µs at
   115200 baud).

**3.3 V bank: 4.7 kΩ in each of the three wires.** Module side at a 35 kΩ
pull-up: low 0.59 V (0.95 V at 20 kΩ), high 3.50 V (3.62 V at 20 kΩ); clamp
current 0.30 mA against a 5 V push-pull output. Any value from 1.5 kΩ to
8.2 kΩ meets 1 and 2; 4.7 kΩ leaves margin both ways.

**1.8 V bank: resistors make the mod safe but not functional.** 4.7 kΩ still
limits every case above (0.62 mA at most), but the module sees the goggle's
high as ~2.2 V (35 kΩ pull-up), below its 3.0 V VIH. Control then needs a level
shifter: a unidirectional single-supply buffer powered from the bay's 5 V whose
VIH at 5 V is ≤ 1.2 V (check its datasheet), goggle side → module side, with
the 4.7 kΩ resistor kept between the goggle pin and the buffer input (the input
also keeps the module's pull-ups and drive off the SDIO bus), a pull-down at
each buffer input (the goggle's pins are inputs most of the time) and ~1 kΩ
between each buffer output and the module (a bootloader output must not short
the buffer). A buffer drives the module's pins all the time, also while WiFi
toggles the bus; SteadyView's register read-back (the only read) does not
pass it. This is a sketch, not a tested design.

**Where:** solder each resistor directly at the goggle-side connector pin of the
bay board (a 0603/0805 part or a small through-hole resistor on the pin, < 5 mm
of bare stub on the bus side), run the wire from its far end to the module
connector, insulate it, and keep it clear of the screw heads and the module
(pkendall's wiki). For a non-X SteadyView also lift module connector pin 8 as
the wiki says (RSSI on the receiver, GND in the HDZero bay).

## Bench checklist before the first power-up with a module

Goggle firmware with Module Bay > Check Pins; WiFi Off; Module Bay: Type
Unknown, Pin Control Off. A multimeter; a logic analyser or scope helps for
step 5. **Stop** at any stop condition and fix it first.

1. **Wiring, goggle off, no module.** Resistance from each goggle-side
   connector pin to its module-connector position: R (4.7 kΩ ± 5 %). Between
   the three wires, and from each to GND and to the bay's 5 V: > 1 MΩ.
   *Stop:* a short, or a reading below R.
2. **PG bank, goggle on, no module.** Module Bay > Check Pins: "PG bank 3.3 V":
   go on. "1.8 V": stop, resistors alone will not drive a 5 V module (see
   above). "?" (no register access): measure in step 5 instead.
3. **Module powered, goggle passive.** Insert the module (True-D: its firmware
   updated and its BAY mode set first, docs/module-bay.md), Power > AnalogRX
   Power: On (bay powered), Check Pins. Expected: a True-D MOD 1.16 BAY:1-8 /
   LINK or MOD 1.17 reads 1 1 1 (its pull-ups); a module without pull-ups may
   read anything. "~" on a pin: the module drives it. *Stop:* any "~"; any 0 on
   a True-D (a pin driven low, or a broken wire).
   Multimeter, each wire to GND: module side of R ≈ 5 V (pull-up) or ≈ 0 V;
   goggle side ≤ VCC-PG + 0.6 V. The voltage across R divided by R is the
   current into the goggle pin: ≤ ~0.06 mA (≤ 0.3 V across 4.7 kΩ) for a
   pull-up. *Stop:* more than 1 V across R (≥ 0.2 mA: a driven output), or the
   goggle side above 4 V.
4. **Power cycle of the module.** Power > AnalogRX Power: Auto, switch the source
   away from analog and back (the bay powers off and on), then Check Pins again:
   the same result as step 3. With a logic analyser on the module side: a True-D
   shows its bootloader's D13 activity in the first second; through R it is
   harmless, but note which wire carries it (that wire goes to D13).
5. **Goggle drives.** Type Switch, pin order PG0=1 PG1=2 PG2=4, Pin Control On;
   dial CH8 (all three high), then CH1 (all low). Module side of each R:
   high ≥ 3.0 V (3.3 V bank with a 35 kΩ pull-up: ~3.5 V), low ≤ 1.5 V
   (~0.6 V). Goggle side of R while high: the PG bank voltage itself (3.3 or
   1.8 V). *Stop:* a high below 3.0 V or a low above 1.5 V at the module side.
6. Only then the real type (docs/module-bay.md for the order per module), and
   keep WiFi off while flying analog with module control.

On a Goggle 2 the mod is unproven: if Check Pins reads the same with the module
powered and unpowered, and step 5 shows nothing at the module, the bay
connector does not carry PG0-PG2 there.
