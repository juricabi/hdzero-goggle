# Module bay control

The goggle can set the channel (and on some modules the mode) of an analog
receiver module in the expansion module bay. Menu: **Module Bay**.

**It needs a hardware mod, for every module type.** On a stock goggle no
control signal reaches the analog module: the goggle only powers it. The mod is
three small wires on the bay board, from three pins of the connector that plugs
into the goggle to the channel-select positions of the 9-pin analog module
connector, as described in
[pkendall64's wiki](https://github.com/pkendall64/hdzero-goggle/wiki/Analog-Module-Backpack-Control)
for the **original Goggle**. On the **Goggle 2** the same mod is unproven: the
same SoC pins (PG0-PG2) are the built-in WiFi chip's SDIO bus there, and nothing
shows that they reach the bay connector at all. The mod puts the module's pins
on SoC pins and on the WiFi bus: put a series resistor in each wire and go
through the bench checklist in **[module-bay-mod-safety.md](module-bay-mod-safety.md)**
before the first power-up with a module.

| Setting | |
|---|---|
| Type | Unknown (default), Switch, rapidFIRE, Steadyview, Steadyview-X, True-D |
| Mode | depends on the type, see below; greyed out when the type has none |
| Pin Control | Off (default) / On. The goggle drives the bay pins only while it is On; it needs the wiring mod |
| Check Pins | reads PG0-PG2 as inputs (never drives) and shows the PG bank supply, see below |

The status line says whether control is active (with the module's channel),
paused (WiFi on, pins in use by WiFi, bay unpowered, module still booting) or
off. It is refreshed while the page is open or only highlighted in the menu.

**Check Pins** shows "PG bank 1.8 V / 3.3 V" (the SoC's own PIO_POW_VAL
register, bit 6) and the three levels, "~" for a line that changed during the
100 ms read. It reads only while the goggle does not control the bay, WiFi is
off and the pins are not in their SDIO function; reading exports them as GPIO
inputs, which is also what module control leaves them as. Use it to check the
wiring and the module (pulled up, pulled down, driving) before Pin Control On.

## Goggle 1 and Goggle 2

| | Goggle 1 | Goggle 2 |
|---|---|---|
| analog receivers | bay only | built-in RTC6715 and bay, Source > Analog Module = Built-in / Expansion |
| bay module controlled when | always the analog source | also while Built-in is selected (when the bay is powered) |
| channel stored in setting.ini | `[source] analog_channel` (as upstream) | built-in: `[source] analog_channel` (as upstream); bay: `[module] channel` (new, starts from analog_channel) |
| WiFi | on the Expansion Module V2, through the bay connector | built in |
| the mod | pkendall's wiki; supported there: rapidFIRE, Steadyview (batch 2), Steadyview-X, TBS Fusion and 1-of-8 modules (Switch) | unproven |

**Channels.** Each receiver keeps its own channel, always one of the 48
standard channels (A1-8, B1-8, E1-8, F1-8, R1-8, L1-8); nothing is folded or
overwritten. With the bay as the analog source, the dial (tune, click / long
press), the OSD channel, the status bar and the Source page use the bay
module's channel list. A type with 8 channels (Switch) shows and sends the
stored channel's place in its band, `(ch - 1) % 8 + 1` (R5 shows as CH5); a
choice in its list keeps the stored band (CH2 from R5 stores R2), and an ELRS
VTX channel is stored as it is (F4, shown CH4). Changing the type or Source >
Analog Module back and forth gives the same channel back. While the bay module
is not controlled (type Unknown, Pin Control Off, WiFi on) the goggle does not
tune it, shows no channel for it and stores nothing for it, also not from ELRS.

**Goggle 2, Source > Analog Module.** Switching to Expansion sends the bay
module its mode and channel again (also when the bay stays powered, Power >
AnalogRX Power = On), in case it was changed by hand meanwhile.

**Bay power** (batch 2 hardware, Power > AnalogRX Power): while the bay is off
the pins are inputs. After power-on the module gets time to boot (1 s,
rapidFIRE 2 s) and is then set up: its mode, then its channel. Batch 1 Goggle 1
hardware has no bay power switch: the module is set up when the goggle app
starts.

### Differences from upstream

* Goggle 2, Expansion selected and type Unknown (the default): no analog channel
  in the status bar, OSD or Source page. Upstream showed the stored channel,
  which an uncontrolled bay module does not follow; it stays hidden here.
* Goggle 2: the bay module's channel is a new key, `[module] channel`;
  `[source] analog_channel` keeps upstream's meaning (the built-in receiver).
* ELRS VTX channel commands in analog mode are ignored, without restarting the
  analog pipeline, while the goggle does not control the analog source;
  applied ones are saved to setting.ini.
* MSP: GET_BAND_CHAN / GET_FREQ answer the analog channel in analog mode (255 /
  0 Hz, the backpack's "unknown", for an uncontrolled bay module); upstream
  answered the HDZero channel. The dial's long-press confirm and the ELRS page's
  "Send VTX freq" send the analog channel in analog mode (upstream: the HDZero
  channel).

## The pins and WiFi

The goggle drives SoC pins **PG0/PG1/PG2** = GPIO 192/193/194, named CLK/DAT/CS
after their role in the SPI-style protocols (pkendall's analog-module-control
branch, written for the Goggle 1). Which bay-board connector pin each one is,
and which module CS pin the mod wires it to, is in pkendall's wiki pictures;
pkendall drove SPI CLK on PG0, DATA on PG1 and CS on PG2, and ExpressLRS wires
rapidFIRE as CS3 = CLK, CS2 = DATA, CS1 = NSS, so his mod presumably connects
PG0 → CS3, PG1 → CS2, PG2 → CS1. Nobody has confirmed that here.

PG0-PG5 are also the SDIO bus (`sdc1`) of the WiFi chip (XR819, `xradio_wlan`)
on **both** goggles. The official images (Goggle 1 9.5.0, Goggle 2 9.5.1 packages)
carry the same sys_config: `[sdc1]` on PG00-PG05, function 2, 4-bit; `[wlan]` on
SDIO bus 1. At boot `rc.sh` switches PG0-PG5 to "IO disabled"
(`0x0300B0D8 = 0x22777777`); the SDIO host takes them back when WiFi starts. So
the goggle:

* drives the pins only with a type set, Pin Control On, WiFi disabled (WiFi
  page) and the bay powered;
* releases them before the WiFi page starts WiFi, and takes them again after
  WiFi was stopped;
* leaves them alone while `xradio_wlan` is loaded or PG_CFG0 shows any of PG0-PG2
  in its SDC1 function (checked again every 5 s).

The PG bank supply (1.8 V or 3.3 V) is not settled by the images; it decides
whether resistors alone are enough: module-bay-mod-safety.md.

### Releasing the pins

A release request (bay power off, Pin Control Off, type change, WiFi on) stops a
transfer at its next abort point, then the type's close() makes the pins
inputs. The pins stay driven at most:

| type | after the request |
|---|---|
| Switch | ~1 ms (its waits are abortable) |
| True-D | the packet on the wire: ≤ ~12 ms (10.4 ms + 2 ms idle-high lead) |
| rapidFIRE | the SPI frame on the wire: ~5-10 ms on the goggle (not measured; 6-7 ms on the host) |
| Steadyview | one register access plus its fixed waits (≤ ~12 ms); 70 ms during the mode set's power pulse |
| Steadyview-X | during set-up: until insmod of soft_uart returns; then close() waits 100 ms and unloads it |

At a bay power-off the bay is already unpowered meanwhile (the power thread
switches first and only then tells the worker). module-bay-mod-safety.md has
what that means for the module.

## Module types

| Type | Channels | On the pins | Mode |
|---|---|---|---|
| Switch | 8, CH1-CH8 = channels 1-8 of the module's own band | static 3-bit code, active high, code n = CH n+1 | pin order, below |
| rapidFIRE | 48 (A B E F R L) | ImmersionRC SPI (CS, DATA, CLK), each command 3x | legacy / rapidFIRE 1 / rapidFIRE 2 |
| Steadyview | 48 | RTC6705 SPI; mode set through a bay power cycle (batch 2 only) | Diversity / Mix 1-3 |
| Steadyview-X | 48 | 115200 baud UART through `soft_uart.ko` (TX = PG0, RX = PG1), CS low | Diversity / Mix 1-3 |
| True-D | 48 | LINK command at 9600 8N1 on all three pins at once | none |

After every set-up (power-on, Pin Control On, WiFi off, type change) and every
mode change the goggle sends the channel itself, whatever the type's set-up
did.

### Switch

**Pin order.** The Mode list picks which pin carries which bit, because the mod's
wiring is not known. Each label gives the weight of each pin's bit:

| Mode | bit 0 | bit 1 | bit 2 | note |
|---|---|---|---|---|
| PG0=1 PG1=2 PG2=4 | PG0 | PG1 | PG2 | pkendall's order |
| PG0=4 PG1=1 PG2=2 | PG1 | PG2 | PG0 | rapidFIRE convention CH1 = DATA, CH2 = CS, CH3 = CLK |
| PG0=4 PG1=2 PG2=1 | PG2 | PG1 | PG0 | CS1 = bit 0 with pkendall's presumed wiring (True-D 1-8: D11 = CS1 = bit 0) |
| PG0=1 PG1=4 PG2=2 | PG0 | PG2 | PG1 | |
| PG0=2 PG1=1 PG2=4 | PG1 | PG0 | PG2 | |
| PG0=2 PG1=4 PG2=1 | PG2 | PG0 | PG1 | |

The first three are the orders offered before; stored settings keep their
meaning. Finding the order on the bench: dial CH1, CH2, CH3, CH5 and see which
channel the module takes for each.

**Sequence and timing** (what the goggle produces):

1. pins inputs until the set-up, 1.0 s after bay power-on (batch 1: at app
   start);
2. code 2 for 200 ms, from inputs straight to its levels: not part of the
   rapidFIRE SPI-mode entry (all high, then all low), so a rapidFIRE in legacy
   mode stays in it;
3. the channel's code, held. Each pin is set on its own (three sysfs writes, a
   few hundred µs between the first and the last);
4. channel 8 (code 7, all high) right after power-up: a True-D in its 1-8 mode
   takes a code after 300 ms, only once its loop runs (~3.5 s after power-on),
   and takes 7 (what an unconnected bay reads) only after another code. So code
   7 is preceded by code 6 (channel 7) until 5.0 s after bay power-on and for at
   least 500 ms; other modules show channel 7 that long. Codes 0-6 go out at
   once; so does 7 once another code has stood that long.
5. a release makes the pins inputs: the module reads its own pull-ups (a True-D
   reads 7 and, once it has taken another code, goes to channel 8 300 ms later).

### rapidFIRE

The goggle only writes: nothing reads a pin or waits for an answer, so a module
that only listens (a True-D in its rapidFIRE mode) gets exactly what a rapidFIRE
gets. Sequence (src/module/rapidfire.c; times after bay power-on from the host
test's trace, which has no sysfs cost):

| time | pins |
|---|---|
| 0-2.0 s | inputs (the module's pull-ups) |
| 2.000 s | straight to all three high (no low step before), 200 ms |
| 2.200 s | all three low, 200 ms (SPI mode entry, as the ExpressLRS backpack's `EnableSPIMode()`) |
| 2.400 s | CS high; idle from now on: CS high, CLK low, DATA low |
| 2.500 s | mode `D` x3 |
| 2.55 s | band `B` x3, then 100 ms |
| 2.70 s | channel `C` x3 |
| 5.000 s | band `B` x3, 100 ms, channel `C` x3 again (for a module that boots slower); channel and mode changes between 2.4 s and 5.0 s wait for this |
| later | each channel change: `B` x3, 100 ms, `C` x3; a mode change: 100 ms, `D` x3, then `B`, `C` |

Command: 5 bytes `cmd '=' 0x01 crc value`, crc = (cmd + '=' + 1 + value) & 0xFF;
`D` mode 0 legacy / 1 rapidFIRE 1 / 2 rapidFIRE 2; `B` IMRC band 1 F, 2 R, 3 E,
4 B, 5 A, 6 L; `C` channel 1-8. Each command 3 times, 10 ms between the end of
one frame and the start of the next. Frame: CS low, ≥ 16 µs, 40 bits MSB first
(DATA set while CLK is low, then CLK high ≥ 9 µs, CLK low ≥ 8 µs, each step
plus one sysfs write), ≥ 16 µs, CS high, DATA low. The receiver should sample
DATA on the rising CLK edge while CS is low, check length and checksum, and not
depend on seeing the SPI mode entry (it may still boot then). The real bit
time on the goggle is not measured: the sleeps take longer than asked (Linux
timer slack, ~50 µs), so expect roughly 100-200 µs per bit and 5-10 ms per
frame.

Against the ExpressLRS backpack (`src/rapidfire.cpp`, VRx "Rapidfire" target):
the same entry (200 ms high, 200 ms low), the same command bytes and band
mapping, 3 repeats. The backpack waits 2 s after its own boot before the entry
(the goggle: 2 s after bay power-on), floats the pins between frames (the goggle
keeps idle levels), clocks at 1 kHz with 1 ms CS lead and 100 ms CS gaps
(the goggle: pkendall's much faster timing, known to work on a rapidFIRE) and
never sends a mode command.

### Steadyview, Steadyview-X

Steadyview latches its mix mode at power-up: the mode set switches the bay off
for 70 ms and on again with CLK/DATA at the mode's levels. The bay power thread
(`external_analog_module_power()`) owns the bay power; the pulse runs under its
lock and only while the bay is on, so a switch-off requested meanwhile happens
right after it. Batch 1 Goggle 1 has no bay power switch: no mode list.
Steadyview-X gets its mix mode at set-up, then its channel.

### True-D (Furious True-D, firmware by the owner's project)

**Requirements:**
* the wiring mod with series resistors (module-bay-mod-safety.md); the True-D's
  bootloader drives D13 at 5 V at every reset, whatever its firmware;
* **True-D firmware MOD 1.16 or later, with its BAY mode set before Pin Control
  is turned on.** Never use module control with older True-D firmware, or with
  MOD 1.16 in BAY:AUTO: those drive D13 at 5 V push-pull for ~6 s after every
  power-up.

| goggle type | True-D firmware and BAY mode | wires used |
|---|---|---|
| **rapidFIRE** (the main way) | MOD 1.17 or later, BAY:RAPIDFIRE (its default) | all three: CS1 = NSS (D11), CS2 = DATA (D12), CS3 = CLK (D13) |
| True-D (LINK) | MOD 1.16, BAY:LINK (MOD 1.17 as being worked on drops LINK) | D12 only |
| Switch | BAY:1-8 (MOD 1.16 or later) | all three, D11 = bit 0, D12 = bit 1, D13 = bit 2: pin order PG0=4 PG1=2 PG2=1 with pkendall's presumed wiring |

MOD 1.17 is still being worked on in the True-D project; its pin roles (D11 =
CS1, D13 = CS3) are inferred there, not measured.

**Safe order:**
1. update the True-D and set its BAY mode in its own menu (with the goggle at
   type Unknown or Pin Control Off, or outside the goggle);
2. the bench checklist in module-bay-mod-safety.md;
3. choose the matching type, then Pin Control On;
4. keep WiFi off while flying analog: the SDIO traffic is on the same wires.

**True-D type ("LINK"):** command `FD 02 02 05 41 EE 00 <ch> <crc hi> <crc lo>`,
ch 0-47 = A1-8, B1-8, E1-8, F1-8, R1-8, L1-8, CRC-16/XMODEM over the first 8
bytes, at 9600 8N1 on PG0, PG1 and PG2 at once (only D12 listens):

* bit-banged through the sysfs value files (opened once), every edge at an
  absolute CLOCK_MONOTONIC deadline from the first start bit, busy-waited at
  SCHED_FIFO for the ~10.4 ms of a packet;
* each command 5 times, 30 ms apart; the pins are outputs (idle high) only for
  that burst and inputs otherwise;
* sent 1 s after bay power-on and again 3.5 s after power-on, and on every
  channel change. Changes in between wait for the 3.5 s send, so the True-D gets
  at most 50 bytes during its 2 s boot screen (it buffers 64).

### Foxeer Wildfire

What is known (sources below): the Wildfire (72CH, and the newer Wildfire /
Wildfire Lite 80CH) is a diversity receiver for Fat Shark style bays with its
own OLED and 5-way button, USB firmware update (80CH), "compatible with all Fat
Shark goggles". Its manual's Settings list has SWITCHING (Fast / Slow / Off),
OSD, SHOW RSSI, **FS SWITCH (ON / OFF)**, ALL BANDS and FAN. No SPI or serial
channel control is documented, and the ExpressLRS backpack has no Wildfire
target (its VRx targets: rapidFIRE, Steadyview, Fusion, HDZero, Skyzone, Orqa,
RX5808...).

What is **not** known: what FS SWITCH does (the name suggests the Fat Shark
goggle channel buttons, i.e. the three CS pins, but no text found explains it);
whether it reads the pins as an absolute channel 1-8 or as up/down steps, and
in which band; whether its CS pins have pull-ups, pull-downs or are ever driven;
their logic level. So the Switch type may or may not fit.

Bench procedure (after module-bay-mod-safety.md steps 1-2, Pin Control Off):
1. Wildfire in the bay, bay powered: Check Pins with FS SWITCH OFF, then ON.
   1 1 1: pull-ups; 0 0 0: pull-downs or nothing connected; "~": it drives a
   pin: stop. Multimeter across each resistor: ≤ 0.3 V.
2. FS SWITCH ON, type Switch, pin order PG0=1 PG1=2 PG2=4, Pin Control On. Dial
   CH1, CH2, CH3, CH5 with a few seconds each and watch the Wildfire's channel:
   * it goes to channel n of its band for CHn (in some pin order): absolute, the
     Switch type fits; try the other orders until CH1-CH8 map to 1-8;
   * it moves by one channel per change, whatever the size: steps; the goggle
     has no step type, so it cannot be controlled;
   * nothing happens: FS SWITCH is something else, or the wiring is wrong.

Sources: Foxeer product pages for the
[Wildfire 80CH](https://www.foxeer.com/foxeer-wildfire-5-8ghz-80ch-dual-receiver-g-607) and
[Wildfire Lite 80CH](https://www.foxeer.com/foxeer-wildfire-lite-4-9g-5-8ghz-80ch-dual-receiver-g-617);
the Foxeer manual
([PDF](http://download.foxeer.com/manual/MR1622/WildFire_Goggle_Receiver_User_Manual.pdf),
[transcription](https://manuals.plus/m/6992f2b851eddfc8f1905d4f2f449bce6b5c4256039b864e3c29ce2bda0c9b57))
as quoted by search results (the pages themselves could not be opened from
here); the ExpressLRS Backpack's `targets/`.

## ELRS / MSP in analog mode

* VTX channel (MSP_SET_BAND_CHAN / SET_FREQ): applied and stored only when the
  goggle tunes the analog source (built-in receiver, or a controlled bay
  module); a Switch module gets the channel's place in its band (A1, B1 ... L1
  all select CH1).
* MSP_GET_BAND_CHAN / GET_FREQ: the analog channel; 255 / 0 for an uncontrolled
  bay module.
* Dial long-press confirm and ELRS page "Send VTX freq": the analog channel.

## Host tests (no hardware)

`make -C utilities/module_bay_test test`:

* `test_trued_uart`: the True-D transmitter's pin trace, with simulated sysfs
  and interrupt jitter, through two software UART receivers;
* `test_module_worker`: `module.c` against recording module types whose init
  sends no channel: the set-up sequence and channel after every set-up, the
  True-D boot timing, power-off and abort within one packet, Pin Control, the
  WiFi interlock including the SDIO check (`bay_pins_busy()` against stand-in
  sysfs files), stored channels (Switch list, type round trips, built-in vs bay
  on the Goggle 2, nothing stored while uncontrolled), the source change, the
  pin check;
* `test_module_types`: the real Switch, rapidFIRE and True-D types on the real
  worker with a recording pin model: the Switch power-up sequence against a model
  of the True-D's 1-8 mode (channel 8 included), a rapidFIRE listener decoding
  the frames (the table above comes from it), the True-D bursts, and the pins
  inputs within 30 ms of a power-off in mid-command.

## Untested on hardware

Everything that touches a real goggle:

* the PG bank voltage (PIO_POW_VAL) on each goggle, and the mod with series
  resistors (module-bay-mod-safety.md's checklist);
* whether PG0-PG2 reach the bay at all on the Goggle 2, and which module pin
  each reaches on both;
* that the sysfs exports work on PG0-PG2 after `rc.sh` disabled them, and that
  WiFi still starts after module control or Check Pins released them;
* the real bit timing of rapidFIRE, Steadyview and True-D frames on the V536
  (sysfs write cost, timer slack, SCHED_FIFO);
* Switch pin orders, rapidFIRE, Steadyview, Steadyview-X and True-D on either
  goggle with this firmware; the Wildfire's FS SWITCH;
* the WiFi interlock (enable WiFi with a module active, disable it again) and
  the PG_CFG0 / PIO_POW_VAL reads through `/sys/class/sunxi_dump`.
