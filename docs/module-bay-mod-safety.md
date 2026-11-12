# Module bay wiring mod: what to build, why, and how to check it

This is the one page to follow for the hardware side of module bay control
(firmware side: [module-bay.md](module-bay.md)). Nothing here has been tried on a
real goggle. Every statement carries one of three marks:

* **[analysis]**: worked out from datasheet numbers or from the code; no hardware
  involved;
* **[sim]**: seen in a simulation: the goggle firmware's host tests
  (`utilities/module_bay_test`) or the True-D project's board simulator;
* **[hw]**: needs hardware; not known until someone measures it.

Facts cited from the bay pin research (furiousdfw `docs/bay-pin-research.md`)
keep its evidence grades: **(a)** manufacturer document, **(b)** published
schematic, design file, firmware source or measurement, **(c)** inferred from
products that work in these bays, **(d)** unknown, **(snippet)** seen only as a
search-engine snippet (weak).

## Short version

1. The mod is three wires from three SoC pins of the goggle (PG0-PG2, which are
   also the WiFi chip's bus) to the channel pins 1-3 of the module socket. Put a
   **4.7 kΩ resistor in each wire, soldered at the goggle-side connector pin**.
   Nothing else on the bay board protects those pins. [analysis]
2. Before choosing any Module type: **Module Bay > Check Pins**. "PG bank 3.3 V":
   resistors alone are enough. "PG bank 1.8 V" (shown in orange): a 5 V module
   cannot see the goggle's high level; build the level shifter of section 5 or
   stop. [analysis]
3. Follow the checklist of section 8 in its order. Every step says what you should
   read and when to stop.
4. True-D: MOD 1.17, `BAY:RAPIDFIRE`, goggle Module = **rapidFIRE**. Never put a
   True-D with the stock firmware (3.9D), MOD 1.15 or MOD 1.16 in `BAY:AUTO` into
   a wired bay. [analysis]
5. Fly with WiFi off. Module control pauses while WiFi is on. [analysis, sim]

## 1. The three wires

**What they are.** The goggle drives SoC pins **PG0, PG1, PG2** of the Allwinner
V536 (GPIO 192, 193, 194 in the code: `GPIO_MODULE_CLK/DAT/CS`, from pkendall64's
analog-module-control, (b)). The same pins are the **SDIO bus of the WiFi chip**:
PG0 = SDC1 CLK, PG1 = CMD, PG2 = D0 (the official images' sys_config: `[sdc1]` on
PG00-PG05, function 2, (b)). On the Goggle 1 the WiFi chip sits on the Expansion
Module V2, behind the bay board's goggle connector; on the Goggle 2 it is built in.

**Where they go** (pkendall64's wiki "Analog Module Backpack Control", for the
Goggle 1, (b)): "Solder 3 enamelled copper wires (or other small gauge wire) to the
3 pins show on the connector that plugs into the HDZero goggles", route them to the
other side of the bay board "leaving space for the screw heads and keeping the
wires away from where the module will press down on the board", and solder them to
pins 1, 2 and 3 of the 9-pin module socket ("Feed the Pin 2 wire under the module
connector and solder to the connector leg"). The wiki's photos show the three wires
on three adjacent pins of one row of the 2×10 goggle connector, labelled Pin 1, 2,
3 after the socket pin each goes to. The text does not say which SoC pin each
connector pin is. From the code's roles and the standard rapidFIRE wiring (CS3 =
CLK, CS2 = DATA, CS1 = NSS: ExpressLRS, (b)) the mod presumably connects **PG0 →
socket pin 1 (CS3, clock), PG1 → pin 2 (CS2, data), PG2 → pin 3 (CS1, chip
select)**. [hw] for the actual connector pins: nobody has checked continuity on a
goggle.

**The stock bay board does nothing on these pins.** Socket pins 1-3 are not
connected on a stock bay board. An open-source clone of the Goggle 1 bay board
(NOSDracing/HDZero-Module-Bay, Gerber net attributes, (b)) has no components at
all, only traces: J2 pins 1-3 not connected, 4 Video, 5 Audio_Left, 6
Audio_Right, 7 not connected, 8 GND, 9 +5V. HDZero's own documentation (a): "HDZero
goggle provides 5V power to analog receiver and in-takes its CVBS output. There are
no control signals from the goggle to analog receiver." What the official board
carries is not documented (d). So **your resistors are the only protection** between
the module and the SoC (and the WiFi bus). [analysis]

**Goggle 2.** The mod is documented for the Goggle 1 only. Whether PG0-PG2 reach
the Goggle 2's bay connector at all is unknown. [hw]

## 2. Why 4.7 kΩ in each wire

### The numbers used

| | value | source |
|---|---|---|
| V536 VCC-PG (PG bank supply) | 1.62-1.98 V (1.8 V) or 2.97-3.63 V (3.3 V); abs. max 3.96 V | V536 datasheet rev. 1.6, (a) |
| V536 V_IH / V_IL | ≥ 0.7 × VCC / ≤ 0.3 × VCC; input max VCC + 0.3 V | (a) |
| V536 V_OH / V_OL | ≥ VCC − 0.2 V / ≤ 0.2 V (no load current stated) | (a) |
| V536 internal pull-up / input leakage | 80-120 kΩ / ≤ 10 µA | (a) |
| V536 clamp (injection) current limit | **not given**; this page uses ≤ 1 mA per pin as the design limit (a common choice for CMOS pins, not an Allwinner number) | (d) |
| ATmega328P at 5 V (True-D), V_IH / V_IL | ≥ 0.6 × VCC / ≤ 0.3 × VCC (VCC 2.4-5.5 V); pin max VCC + 0.5 V | Microchip DS40002061B, Common DC Characteristics, (a) |
| ATmega328P I/O pull-up | 20-50 kΩ | (a) |
| ATmega328P output | ~26 Ω source resistance at 5 V (4.47 V at 20 mA, 25 °C, Fig. 31-355); 40 mA per pin abs. max | (a) |
| RTC6715 receiver chip (the "dumb" modules' channel pins) | V_IH 0.7 × V_IO, V_IL 0.3 × V_IO, internal pull-ups of unstated strength, logic abs. max 5 V | RichWave datasheet, (a) |
| clamp diode forward drop | 0.3 V taken as the worst case (a lower drop means more current) | [analysis] |

**PG bank: 1.8 V or 3.3 V is not established.** The official images put `vcc-pg`
on a PMIC rail named with 1.8 V loads, which points to 1.8 V, but do not set its
voltage (d). The SoC reports it: PIO_POW_VAL (0x0300B348) bit 6 = 1: below 2.0 V, 0:
above 2.5 V (V536 datasheet p. 888, (a)). **Check Pins** shows it.

### What can push current into PG0-PG2, with and without the resistor

R = 4.7 kΩ in each wire. "Into the clamp" means the goggle pin is an input (or "IO
disabled", as at boot) and the excess current flows through its protection diode
into VCC-PG. [analysis] for every number; when each case can happen is from the code
[analysis] and the host tests [sim].

| case | duration | 3.3 V bank | 1.8 V bank | without resistor |
|---|---|---|---|---|
| **True-D stock bootloader**: D13 (pin 1, the clock wire = PG0) driven 5 V push-pull, goggle pin input: (5 − VCC − 0.3) / R | 100 ms at every True-D power-on or reset, (b) + sim | **0.30 mA** | **0.62 mA** | ~50 mA at 3.3 V (1.4 V over ~26 Ω), more at 1.8 V: far above any injection limit and the AVR's own 40 mA |
| the same pulse while the goggle drives that pin low: 5 V / R | cannot happen: the goggle keeps every pin an input until 2.0 s after bay power-on [sim] | 1.06 mA | 1.06 mA | a short between two outputs |
| bootloader's low phase (0.1-1.39 s) against a goggle high: VCC / R | cannot happen, same reason [sim] | 0.70 mA | 0.38 mA | a short |
| **stock True-D firmware 3.9D** (also MOD 1.15, MOD 1.16 in `BAY:AUTO`): D13 is the Dock-King link's TX, 5 V push-pull, idle high | ~6 s after every power-up, (b) | 0.30 mA into the clamp; 1.06 mA once the goggle drives (from 2.0 s) | 0.62 / 1.06 mA | tens of mA for seconds: **likely damage** |
| **goggle drives while WiFi is on**: GPIO output against the WiFi chip's drivers on CMD / D0 | the firmware never does it: no drive while WiFi is enabled, while the WiFi driver is loaded or while PG0-PG2 are in the SDIO function, and it lets go before WiFi starts [sim] | not limited by R (the contention is on the bus side of it) | the same | the same |
| **WiFi running with a module in the bay**: the SDIO host drives CLK / CMD / D0 at VCC-PG; a module output (bootloader pulse) or pull-up meets it | while WiFi runs and the module is powered | ≤ 1.06 mA (5 V output vs SDIO low); pull-ups 20-50 kΩ: ≤ 0.2 mA | the same | tens of mA |
| goggle high into an **unpowered module** (its clamp to its 0 V rail): (VCC − 0.3) / R | ≤ one rapidFIRE frame after a bay power-off (< 40 ms in the host test) [sim]; SteadyView's mode set, 70 ms by design | 0.64 mA | 0.32 mA | mA to tens of mA, can half-power the module's MCU |
| **True-D on USB, goggle off**: bootloader pulse into an unpowered goggle: (5 − 0.3) / R | 100 ms per reset, (b) | 1.0 mA | 1.0 mA | tens of mA into the goggle's unpowered rail |
| module pull-ups (True-D `BAY:1-8` / `STEP`, 20-50 kΩ) into a goggle input: (5 − VCC − 0.3) / (R_PU + R) | whenever the module is powered, also with Module = None | ≤ 57 µA | ≤ 117 µA | ≤ 70 / 145 µA |

With the **no-LED bootloader** (furiousdfw `bootloader/`, flashed once with an ISP
programmer) and MOD 1.17 the True-D never drives a bay pin outside an ISP session
(furiousdfw: [analysis] and [sim], not yet on the unit, [hw]); the bootloader rows
then disappear. Keep the resistors anyway: they also cover a module flashed with
other firmware, a module on USB, ISP, and a goggle driving an unpowered module.

**Most modules have no resistor of their own on these lines.** JAFaR, FS-SkyRF and
the True-D connect their MCU pins directly; the only resistors found on bay lines
are 1 kΩ inside some modules, on their own receiver-chip lines (bay pin research,
(b)). So every number above assumes nothing on the module side.

### Why this value

* **Lower bound**, ≤ 1 mA into a clamp from a 5 V output: R ≥ 1.4 kΩ (3.3 V
  bank), ≥ 2.9 kΩ (1.8 V bank). **1 kΩ is too low**: 1.4 mA (3.3 V) or 2.9 mA (1.8
  V). [analysis]
* **Upper bound**, a goggle low must read low at the module through its strongest
  pull-up: 5 V × R / (R + 20 kΩ) ≤ 1.5 V (AVR V_IL) gives R ≤ 8.6 kΩ. With 4.7 kΩ:
  0.95 V at a 20 kΩ pull-up, 0.43 V at 50 kΩ, 0 V in RAPIDFIRE (no pull-up).
  [analysis]
* **Speed**: 4.7 kΩ × ~25 pF (wire and module pin) = 0.12 µs, against the goggle's
  shortest step: ≥ 100 µs per rapidFIRE clock level, 8.7 µs per SteadyView X UART
  bit. [analysis]
* **The WiFi bus**: soldered at the goggle-side connector pin with < 5 mm of stub,
  the resistor hides the wire and the module from the bus: at the 25-50 MHz SDIO
  clock the bus sees about 4.7 kΩ (≤ 0.7 mA peaks at 3.3 V) instead of 10-20 cm of
  wire and a module input. [analysis]; that WiFi keeps working with the mod [hw].
* **Nothing found argues for another value.** No published design puts series
  resistors between goggle and module at all (bay pin research, (b)/(d)); the
  True-D project checked 4.7 kΩ from the module side and keeps it (furiousdfw
  `docs/bay-standards.md` 5.6). 3.3 kΩ-6.8 kΩ would also meet both bounds at
  3.3 V; 4.7 kΩ leaves margin both ways.

### "Dumb" modules: the low must reach about 1.0 V

A module that passes the pins straight to an RTC6715 receiver chip reads a low
only below 0.3 × V_IO ≈ **1.0 V** (3.3 V V_IO), against internal pull-ups whose
strength the datasheet does not give (a). Through 4.7 kΩ the low stays below
1.0 V only if that pull-up is above ~11 kΩ (3.3 V × 4.7 / (4.7 + R_int) ≤ 1.0 V).
[analysis]; the real value [hw]: check "low ≤ 1.0 V" at the module side (checklist
step 16). The True-D is different: an AVR input, low below 1.5 V.

## 3. The honest margin at 3.3 V

In `BAY:RAPIDFIRE` the True-D has no pull-ups, so it sees the goggle's high
exactly: V_OH ≥ VCC-PG − 0.2 V (a).

| | ATmega328P V_IH at its supply | margin of a 3.3 V high (3.1-3.3 V) |
|---|---|---|
| guaranteed (0.6 × VCC, (a)) | 3.0 V at 5.0 V, 3.15 V at 5.25 V, **3.3 V at 5.5 V** | +0.1 to +0.3 V at 5.0 V; −0.05 to +0.15 V at 5.25 V; none at 5.5 V; negative if VCC-PG sits at its 2.97 V minimum |
| typical (Fig. 31-356, 25 °C; the −40 to 105 °C curves nearly overlap, (a)) | ≈ 2.65 V at 5.0 V, ≈ 2.9 V at 5.5 V | ≈ +0.45 to +0.65 V at 5.0 V; ≈ +0.2 to +0.4 V at 5.5 V |

So a 3.3 V bank drives a True-D's input high **in practice, not by guarantee**.
[analysis] The module's supply is the bay's 5 V (pin 9); its tolerance is not
documented (d), and whether the True-D's ATmega runs directly from it or from a
regulator is not known (d). Measure the bay supply (checklist step 12); at 5.25 V
or more the guaranteed margin is gone, and the typical one shrinks by ~0.05 V per
0.1 V of supply. [analysis]

## 4. While WiFi is on

* The goggle never drives PG0-PG2 while WiFi is enabled, while the WiFi driver
  (`xradio_wlan`) is loaded, or while PG_CFG0 shows any of them in the SDIO
  function; it releases them (inputs, unexported) before the WiFi page starts WiFi,
  and waits for a running Check Pins to finish. [analysis, sim]
* The module still sees the SDIO traffic on its pins. A rapidFIRE listener (True-D
  MOD 1.17 `BAY:RAPIDFIRE`) accepts only checksummed frames and switches its
  receiver off for 200 ms on a pin storm (furiousdfw, [sim]). A module that reads a
  static code (True-D `BAY:1-8` / `STEP`, Fat Shark style) may change channel.
  [analysis]
* Whether the WiFi chip's I/O is powered while WiFi is **off** is unknown. If it is
  not, a goggle high on CLK / CMD / D0 back-feeds the WiFi chip through its input
  clamps, limited only by the SoC's drive (the resistor is on the module side, not
  between SoC and WiFi chip). This applies to pkendall64's code as much as to this
  firmware; only with Module = None is it ruled out. [hw]

## 5. If Check Pins says 1.8 V

With a 1.8 V bank the 4.7 kΩ resistors still keep every case of section 2 below
0.62 mA, but **a 5 V module cannot see the goggle's high**: 1.8 V (at most 1.98 V)
against the ATmega's 3.0 V V_IH (typically ~2.65 V). No resistor value fixes it,
and open-drain does not either (the released goggle pin is clamped at ~2.1 V).
[analysis, furiousdfw bay-standards 5.6] One direction is enough: goggle out,
module in.

### The option: one LM339 comparator per goggle (all three lines)

Chosen because it needs **only the bay's 5 V and GND** (both on the bay board, (b)
from the clone), puts **no clamp, pull-up or accelerator on the SDIO bus**, and its
input pins tolerate 36 V whether the comparator is powered or not.

Per line (three times; the LM339's fourth comparator is spare):

```
goggle connector pin (PG0/1/2)
   └─ R1 4.7 kΩ (the mod's resistor, at the pin, < 5 mm stub)
        └──●── LM339 IN+          LM339 OUT ──●── R4 1 kΩ ── module socket pin 1/2/3
           │                                  │
           R2 1 MΩ to +5 V (bay)              R3 10 kΩ to +5 V (bay)
                                              (R5 18 kΩ to GND: only for a 3.3 V module)
   LM339 IN− : the reference, 5 V × 1 k / (4.7 k + 1 k) = 0.88 V (one divider for all three)
   LM339 VCC: bay +5 V, 100 nF to GND at the pin; GND: bay GND
   spare comparator: IN+ to GND, IN− to the reference, output open
```

Parts a hobbyist can get anywhere: LM339N (DIP-14) or LM339 in SO-14 (any maker),
1/8 W resistors 3 × 4.7 kΩ, 3 × 1 MΩ, 3 × 10 kΩ, 3 × 1 kΩ, 1 × 4.7 kΩ + 1 × 1 kΩ
(1 %) for the reference, optionally 3 × 18 kΩ; one 100 nF ceramic capacitor (a
second one on the reference is good practice). Build it on a scrap of perfboard,
insulate it with Kapton, and place it where neither the module nor a screw head
can press on it; whether the HDZero bay has room for it is [hw].

LM339 numbers (TI SLCS006Z, non-B, (a)): supply 2-36 V; input voltage range −0.3 to
36 V on either input, also above the supply ("either or both inputs can go to 30 V
without damage"); common-mode range 0 to VCC − 1.5 V (25 °C), VCC − 2.0 V (full
temperature); input bias 25 nA typ, 250 nA max, flowing out of the pin; output open
collector, V_OL ≤ 400 mV at 4 mA, sinks ≥ 6 mA (16 mA typ) at V_OL 1.5 V, output
short to ground unlimited; response 1.3 µs (5 mV overdrive), 0.3 µs (TTL step).

**How it behaves** [analysis for all; nothing built, [hw]]:

* **Thresholds.** Goggle low: ≤ 0.2 V + 4.8 µA (through R2) × 4.7 kΩ ≈ 0.22 V at IN+, below
  0.88 V → output low. Goggle high: ≥ 1.42 V (V_OH at the 1.62 V minimum supply),
  above 0.88 V → output high. At least 0.5 V margin each side; a ±5 % bay supply
  moves the reference to 0.83-0.93 V.
* **Levels at the module.** 5 V module (True-D): high 5 V through R3 + R4 (no
  pull-up in RAPIDFIRE), low ≤ 0.4 V; with a 20 kΩ pull-up (1-8 / STEP) the low is
  0.4 + 4.6 × 1 k / 21 k ≈ 0.62 V. Both well inside 3.0 V / 1.5 V.
* **Goggle pins idle** (Module = None, the first 2.0 s, the goggle app stopped):
  R2 lifts IN+ to the goggle pin's clamp (~2.1 V), output high: the module reads
  all three lines high, as from an unconnected bay with pull-ups (code 7). The
  goggle pin takes (5 − 2.1) V / 1 MΩ ≈ 3 µA into its clamp: inside its 10 µA
  leakage figure. IN+ at 2.1 V is inside the common-mode range (≤ 3.0 V).
* **The True-D bootloader pulse never reaches the goggle**: D13 now meets the
  comparator's output, not PG0. D13 high against an output low: (5 − 0.4) V / 1 kΩ
  = 4.6 mA, within the LM339's sink and the AVR's 40 mA (and only if the goggle
  were driving low, which it does not in the first 2.0 s); D13 low against the
  output high: 5 V / 11 kΩ = 0.45 mA.
* **WiFi on, same pins.** The bus sees the < 5 mm stub, then 4.7 kΩ to a
  comparator input (a few pF) and the 1 MΩ: a DC pull toward 5 V of ≈ 3 µA (an SDIO
  pull-up of 100 kΩ at 1.8 V is 18 µA), and about 4.7 kΩ at the SDIO clock: the
  same as the plain resistor mod. The comparator cannot follow 25-50 MHz; the
  module sees slow garbage while WiFi runs (as it would without the shifter).
  **WiFi with this shifter is expected to work as well as with the plain mod;
  neither is tested.** [analysis, hw]
* **Bay unpowered** (batch 2 switches the bay): the comparator is unpowered; its
  inputs have no specified clamp to its supply (36 V input range regardless of
  VCC), so the goggle pin sees 4.7 kΩ + 1 MΩ to a 0 V rail: ≤ 2 µA; the module sees
  only open-collector outputs and pull-ups to the unpowered rail. No back-feeding
  either way. [analysis]
* **Speed.** ≤ ~1.3 µs comparator delay plus the output rise through 10 kΩ ×
  ~25 pF = 0.25 µs, against ≥ 100 µs rapidFIRE levels: fine. SteadyView X (8.7 µs
  UART bits) gets ~1 µs of skew between falling and rising edges: probably fine,
  [hw]. SteadyView's register read-back cannot pass a one-way shifter: the goggle
  then reads its own idle level and always writes the channel register (which is
  what it needs). [analysis]

**Options not chosen**, and why [analysis]:

* 74HCT / 74AHCT buffers at 5 V: TTL inputs need ≥ 2.0 V for a high (a): 1.8 V is
  not enough.
* Dual-supply translators (SN74LVC1T45 / 2T45 and the like): good parts, but their
  A side needs the goggle's 1.8 V as a supply, and whether any 1.8 V rail reaches the
  bay board is unknown (d). Where the two reference voltages would come from is
  exactly what nobody knows for this board.
* Auto-direction boards (TXS0108E, TXB0108): edge-rate one-shots and (TXS) 10 kΩ
  pull-ups on the goggle side. Directly on the SDIO bus they add load and actively
  drive edges on a 25-50 MHz bus; behind the 4.7 kΩ resistor they fight it. **WiFi
  with one of these hanging on CLK / CMD / D0 is doubtful.**
* BSS138 MOSFET boards: also need the 1.8 V reference, put 10 kΩ pull-ups to 1.8 V
  on all three lines (CLK included), and a BSS138's V_GS(th) of up to 1.5 V leaves
  little of a 1.8 V gate drive. The True-D in RAPIDFIRE has no pull-up on its side
  either.

### A 3.3 V module in the 1.8 V case (rapidFIRE, SteadyView, Wildfire)

* A 1.8 V high is below a 3.3 V CMOS input's 0.7 × 3.3 V = 2.31 V: a 3.3 V module
  needs the shifter too, unless its own input threshold is lower (unknown for each,
  below). [analysis]
* Fit **R5 = 18 kΩ** from each comparator output to GND: the high becomes
  5 V × 18 / (10 + 18) = **3.2 V** (above 2.31 V, below a 3.3 V part's 3.6-4 V
  limits), the low stays ≤ 0.4 V (+ ≤ 0.3 V through 1 kΩ against a 10 kΩ pull-up).
  [analysis]
* **SteadyView**: its channel lines go to RTC6715-class receiver chips: V_IH 2.31
  V, V_IL 0.99 V, logic abs. max 5 V (a). 3.2 V / ≤ 0.7 V fit. What else sits on those
  lines in a SteadyView is unknown (d). Its pin 8 is an RSSI output: lift it (section 6).
* **rapidFIRE**: its controller and input thresholds are not published (d). A
  3.3 V ESP drives it directly in the ExpressLRS backpack, so 3.2-3.3 V highs work
  (c). Whether its pins tolerate 5 V is unknown (d): use R5.
* **Foxeer Wildfire**: levels, pull-ups and whether it ever drives these pins are
  unknown (d): checklist steps 4-5 first; use R5 unless it measures as a 5 V part.

## 6. The 9-pin module socket, pin by pin

Pin 1 is the channel-pin end, pin 9 the +5 V end. Some projects number from the
other end: **identify the pins by +5 V and GND with a meter, not by number.**
Grades from the bay pin research.

| pin | what the HDZero bay does | what a module may put there |
|---|---|---|
| 1 | stock: not connected; with the mod: PG0 (SDIO CLK) through 4.7 kΩ (b) | rapidFIRE CLK (CS3) (b); SteadyView SPI clock (b); True-D D13: bootloader pulse, stock firmware's link TX (b); RTC6715 CS pin with pull-up (a); Wildfire (d) |
| 2 | stock: not connected; with the mod: PG1 (SDIO CMD) (b) | rapidFIRE DATA (CS2), the module answers queries on it (b); SteadyView DATA, driven by the module on read-back (b); True-D D12, never driven (MOD 1.17) (b); Wildfire (d) |
| 3 | stock: not connected; with the mod: PG2 (SDIO D0) (b) | rapidFIRE NSS (CS1) (b); SteadyView chip select (b); True-D D11 (b); Wildfire (d) |
| 4 | video into the TP2825 decoder (termination unknown) (b) | video out, ~1 Vpp into 75 Ω (a) |
| 5 | audio left, always mixed into recordings (b) | audio (rapidFIRE 6.5 MHz carrier audio) (b) |
| 6 | audio right (b) | audio (6.0 MHz) (b) |
| 7 | official board unknown, probably GND; the clone leaves it open (b)/(d) | GND (rapidFIRE, SteadyView) (b) |
| 8 | **GND** (pkendall64's wiki; the clone, (b)) | **differs by vendor**: GND on rapidFIRE (b); **RSSI output on the SteadyView, lift its pin 8** as the wiki shows (b); tied to +5 V on one old DIY design (FS-SkyRF, by its schematic's pin order, not verified physically: it would short the bay's 5 V) (b); RSSI on C5VRX (b); **True-D and Wildfire unknown** (d): checklist step 4 |
| 9 | +5 V from the goggle, switched on batch 2 goggles (b); current rating unknown (d) | supply in |

A SteadyView with pin 8 lifted still works in an HDZero bay: it gets GND from pin 7
(c).

## 7. Mechanical short risk

Weak evidence (forum posts seen only as search snippets), but cheap to prevent:

* HDZero V1 bay: screw heads and exposed solder points touching the back of
  modules: "screw heads that would contact my furious fpv module and it DID cause a
  issue"; another user suspected this killed a rapidFIRE (snippet).
* Early True-D V3 units: "the header on the top module panel could touch the bottom
  panel … causing a 5V-to-GND short", fixed from V3.5 (snippet).
* A forum post describes a "fried 5V voltage regulator BEC inside the HDZero
  goggles" (snippet).

So: inspect the bay and the module (checklist steps 1-2 and 7), cover screw heads
and exposed solder points facing the module with **Kapton tape**, and route the mod
wires (and a shifter board) clear of the screws and of where the module presses on
the board, as pkendall64's wiki says. [analysis]

## 8. The checklist

In this order. Each step: what to do, what you should read, **stop if**. Stop means:
do not go on until the cause is found and fixed. You need a multimeter (diode mode
and min/max hold help), a current-limited 5 V bench supply for step 5, and an LED
with 1 kΩ for step 6.

**A. Bay and module on the bench (goggle off, module out)**

1. **Bay socket, stock, no module, goggle off.** Pin 8 to GND: 0 Ω. Record pin 7
   to GND. Pins 1, 2, 3: open to everything. Pin 9 to pin 8: not a short. *Stop
   if* pin 9 is shorted to GND, or pins 1-3 are connected to anything on a stock
   board.
2. **Bay mechanics.** Look for screw heads and solder points that a module could
   touch; tape them with Kapton. *Stop if* anything metallic can touch the module.
3. **Module, unpowered: pins 7-9.** Pin 8 to pin 9: must not be ~0 Ω. Pin 8 to pin
   7: 0 Ω or open are fine. *Stop if* pin 8 shows a resistance to pin 7 that is
   neither (an output the HDZero would short to GND: lift that pin, as for a
   SteadyView), or pin 8 is tied to pin 9.
4. **Module, unpowered: pins 1-3.** Diode mode, red lead on the pin, black on GND:
   1.6-2.0 V means an LED on that pin. Also record each pin's resistance to GND
   and to pin 9 (True-D: what is on D11-D13 is not known, (d)). *Stop if* an LED
   (it would defeat 4.7 kΩ), or a resistance below ~10 kΩ to GND or 5 V (a pull
   resistor that would load the goggle pin in every mode).
5. **Module on a 5 V bench supply** (current limit ~0.5 A; pin 9 +, pin 7/8 −),
   after its boot: each of pins 1-3 open, then with 10 kΩ to GND. ~1 V under load:
   internal pull-up (fine). Unchanged 5 V or 3.3 V: a push-pull driver. 0 V both
   times: a plain input (True-D `BAY:RAPIDFIRE`: expect this). *Stop if* a driver:
   never wire that module without the resistors, and for a True-D it means the wrong
   firmware or mode.
6. **Bootloader pulse.** LED + 1 kΩ from pin 1 to GND (or the meter's min/max hold
   on pin 1), power the module up. A 0.1 s flash: **stock bootloader**, the pulse
   is there (that pin is D13; with the resistors it is harmless). No flash: the
   **no-LED bootloader** (or none). *Stop if* the flash lasts seconds: stock 3.9D /
   MOD 1.15 / 1.16 AUTO firmware, not usable in a wired bay.
7. **True-D revision and firmware.** V3 from before V3.5: check the clearance
   between its two boards (section 7). Flash MOD 1.17 out of the goggle, set
   `BAY:RAPIDFIRE` in its menu. *Stop if* the boards can touch, or the firmware is
   not MOD 1.17.

**B. The mod (goggle off)**

8. Resistors at the goggle-side connector pins (< 5 mm stub), wires to socket
   pins 1-3, routed clear of the screws, insulated. Each wire end to end: 4.7 kΩ
   ± 5 %; between the wires and from each to GND and to pin 9: > 1 MΩ (no module).
   *Stop if* any reading is off.

**C. Goggle on, no module**

9. Flash the goggle firmware. Module Bay: **Module = None** (the default; with None
   the goggle never touches the pins, as stock). WiFi off.
10. **Check Pins.** "PG bank 3.3 V": go on. "PG bank 1.8 V" (orange): *stop*, build
    section 5 first (and repeat steps 8-10 with it). "PG bank ?": measure instead,
    with WiFi on (a V2 Expansion Module on the Goggle 1), the DC voltage of the PG1
    or PG2 wire to GND: ~1.8 V or ~3.3 V.
11. Each wire, goggle side of the resistor, to GND: ≤ VCC-PG (the pins are inputs).
    *Stop if* above VCC-PG + 0.3 V.
12. **Bay supply.** Power > AnalogRX Power = On (bay powered), no module: pin 9 to
    pin 8: 4.75-5.25 V; write it down (section 3). *Stop if* outside that range.

**D. Module in, goggle passive (Module = None)**

13. Module in the bay, bay powered, **Check Pins**. True-D `BAY:RAPIDFIRE`: no
    pull-ups, the lines float: any reading, "~" possible. `BAY:1-8` / `STEP`:
    1 1 1. Multimeter across each resistor: ≤ 0.3 V (≤ 64 µA). Goggle side of
    each resistor: ≤ VCC-PG + 0.6 V. *Stop if* more than 1 V across a resistor
    (> 0.2 mA: something drives), or the goggle side is above VCC-PG + 0.6 V.
14. **Bay power cycle.** Power > AnalogRX Power = Auto, switch the source away from
    analog and back (the bay goes off and on), meter in min/max hold across the
    pin-1 resistor. Stock bootloader: a peak of ≤ ~1.7 V (3.3 V bank: 5 − 3.3 V)
    for 0.1 s, then 0; no-LED bootloader, or the section 5 shifter: nothing. Then
    Check Pins again: as in
    step 13. *Stop if* the goggle side ever exceeds VCC-PG + 0.6 V, or the reading
    lasts seconds.

**E. The goggle drives (first time)**

15. Module = **Switch**, Mode "PG0=1 PG1=2 PG2=4". A True-D in `BAY:RAPIDFIRE`
    ignores static codes, so nothing changes on it; a "dumb" module changes channel.
    Status must show "Active" (2.0 s after bay power-on).
16. Dial **CH8** (all three high), measure the module side of each resistor: ≥ 3.0
    V for a 5 V module (True-D; ~3.3 V expected), ≥ 2.31 V for a 3.3 V module.
    Dial **CH1** (all low): ≤ 1.5 V for an AVR module, **≤ 1.0 V for a "dumb"
    RTC6715 module**. Goggle side while high: the PG bank voltage. *Stop if* any
    level misses its limit: for a True-D at 3.0-3.2 V, see section 3 before going
    on.
17. Module = the real type: True-D MOD 1.17 → **rapidFIRE**; dial a few channels
    and watch the module follow ("Active R1" etc.). *Stop if* it does not: wiring
    order (section 1), or [hw] items of section 9.
18. **WiFi on**: status "Paused while WiFi is on"; WiFi must connect as before the
    mod. **WiFi off**: "Active" again within ~2 s. *Stop if* WiFi no longer works
    with the mod: the stub or the wiring loads the SDIO bus; shorten the stub, or
    remove the mod.

## 9. What the firmware guarantees, and what is unproven

**Goggle firmware** (src/module/, host tests in `utilities/module_bay_test`, 136
checks, all pass):

* Module = None (the default after flashing): no pin is ever touched, through boot,
  power cycles, source changes, WiFi on / off, standby and wake. Exactly stock.
  Check Pins, a click, exports them as GPIO inputs for 100 ms and never drives.
  [analysis, sim]
* Every type: the pins stay inputs until 2.0 s after every bay power-on (and after
  the app start on batch 1 hardware). The time counts from the goggle's power-on
  command to the DM5680 co-processor; the real switch-on follows it after an
  unmeasured delay [hw], inside the 0.55 s margin over the bootloader's 1.45 s.
  [analysis, sim] SteadyView is the one exception by protocol: its mode set
  power-cycles the bay with CLK / DATA driven (70 ms), only with Module =
  SteadyView. Never select it for another module.
* rapidFIRE: ≥ 100 µs between any two CS / CLK edges and DATA changes only while
  CLK is low, whatever the scheduler does: every wait ends only once the clock has
  passed an absolute deadline [analysis]; also under a storm of signals that cut
  every sleep short [sim]. Typical 13.8-14.6 ms per frame on a PC [sim]; on the
  goggle not measured [hw].
* Pins inputs again within one frame after a bay power-off, WiFi on or a Module
  change. [sim]

**Unproven, only hardware can settle it** [hw]:

* the Goggle 2 bay routing: whether PG0-PG2 reach its bay connector at all;
* the PG bank voltage, 3.3 V or 1.8 V, on each goggle (Check Pins);
* the official bay board: what it carries on socket pins 1-3 and 7 and on the
  goggle connector, and which connector pin is which PG pin;
* the True-D board's loads on D11-D13 (an LED on D13, pull resistors, protection
  parts, ICSP pads), and that bay pins 1-3 reach D13, D12, D11 in that order;
* whether the WiFi chip is powered while WiFi is off (section 4), and that WiFi
  works with the mod (and with the shifter);
* the shifter of section 5: nothing built; room for it in the bay;
* the goggle's real pin timing, and the DM5680's bay power switching delay.

**Foxeer Wildfire.** Its manual (search-result transcriptions, snippet) lists a
setting **FS SWITCH (ON / OFF)**, so the Fat Shark three-pin code is the likely
interface and **Switch** the likely Module type, absolute or stepped and pin order
unverified; its pins' levels and pull-ups are unknown (d): steps 3-6 first, then
Check Pins with FS SWITCH off and on, then Switch with each pin order (module-bay.md).
Separately, HDZero issue #108 (issue body read) reports a bad picture from a
Wildfire in the HDZero goggle that was fine in an HDO2, cause unknown; it predates
and is unrelated to this mod.
