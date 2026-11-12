# Module bay control

The goggle can set the channel (and on some modules the mode) of an analog
receiver module in the expansion module bay. Menu: **Module Bay**.

| Setting | |
|---|---|
| Type | Unknown (default: the goggle never touches the bay pins), Switch, rapidFIRE, Steadyview, Steadyview-X, True-D |
| Mode | depends on the type, see below; greyed out when the type has none |
| Pin Control | Off (default) / On. Nothing is driven on the bay pins while it is Off |

The status line on the page says whether control is active, paused (WiFi on,
bay unpowered, module still booting) or off.

## Goggle 1 and Goggle 2

| | Goggle 1 | Goggle 2 |
|---|---|---|
| analog receivers | bay only | built-in RTC6715 and bay, Source > Analog Module = Built-in / Expansion |
| bay module controlled when | always the analog source | Analog Module = Expansion |
| menu | Module Bay is the only analog control | Module Bay next to Analog RSSI |
| WiFi | on the Expansion Module V2, through the bay connector | built in |

With the bay as analog source, the dial (tune + click / long press), the OSD
channel, the status bar and the Source page use the module's channel list. While
the bay module is not controlled (type Unknown, Pin Control Off, WiFi on) the
goggle does not tune it and shows no channel for it. When the type or Analog
Module changes, a stored channel outside the new list is brought into it with
`(ch - 1) % count + 1`.

Bay power (batch 2 hardware, Power > Analog power): while the bay is off the
pins are inputs. After power-on the module gets 1 s to boot, then it is set up
again with its mode and the current channel. Batch 1 Goggle 1 hardware has no
bay power switch: the module is set up when the goggle app starts.

## The pins and WiFi

The goggle drives three SoC pins, GPIO 192/193/194 = **PG0/PG1/PG2**, named
CLK/DAT/CS after their role in the SPI-style protocols (numbers from
pkendall64's analog-module-control branch, written for Goggle 1).

PG0-PG5 are also the SDIO bus (`sdc1`) of the WiFi chip (XR819, `xradio_wlan`)
on **both** goggles. The official images (Goggle 1 Rev20250319, Goggle 2
Rev20250715) carry the same device tree and sys_config: `[sdc1]` on
PG00-PG05, function 2, 4-bit; `[wlan]` on SDIO bus 1, regon PG08, hostwake PG09.
At boot `rc.sh` switches PG0-PG5 to "IO disabled" (`0x0300B0D8 = 0x22777777`);
the SDIO host takes them back when WiFi starts. Exporting the pins as GPIO while
WiFi runs would cut the WiFi chip off, and the WiFi driver taking them back would
cut module control off. So the goggle:

* drives the pins only with Pin Control On and WiFi disabled (WiFi page);
* releases them before the WiFi page starts WiFi, and takes them again after
  WiFi was stopped;
* leaves them alone while `xradio_wlan` is loaded or PG_CFG0 shows PG0-PG2 in
  their SDC1 function.

On the Goggle 1 the SDIO lines go to the bay connector for the Expansion Module
V2's WiFi; whether the module receiver socket of an expansion module is wired to
any of them is not documented (HDZero's docs say "there are no control signals
from the goggle to analog receiver"). On the Goggle 2 the same pins run to the
built-in WiFi chip, and HDZero says an Expansion Module V2's own WiFi is disabled
there; nothing shows that PG0-PG2 reach the bay at all on the Goggle 2.

## Module types

| Type | Channels | On the pins | Mode |
|---|---|---|---|
| Switch | 8, CH1-CH8 = channels 1-8 of the module's own band | static 3-bit code, active high, code n = CH n+1 | pin order, below |
| rapidFIRE | 48 (A B E F R L) | ImmersionRC SPI (CS, DATA, CLK), each command 3x | legacy / rapidFIRE 1 / rapidFIRE 2 |
| Steadyview | 48 | RTC6705 SPI; mode set through a bay power cycle (batch 2 only) | Diversity / Mix 1-3 |
| Steadyview-X | 48 | 115200 baud UART through `soft_uart.ko` (TX = PG0, RX = PG1), CS low | Diversity / Mix 1-3 |
| True-D | 48 | LINK command at 9600 8N1 on all three pins at once | none |

ELRS VTX channel (MSP) to the bay: 48-channel modules take it as is, the Switch
type takes `(ch - 1) % 8 + 1`, the VTX channel's place in its band (A1, B1 ... L1
all select CH1).

### Switch pin order

The Mode list picks which pin carries which bit of the channel code, because the
bay wiring is not known:

| Mode | bit 0 | bit 1 | bit 2 | source |
|---|---|---|---|---|
| CLK/DAT/CS | PG0 | PG1 | PG2 | pkendall's order |
| DAT/CS/CLK | PG1 | PG2 | PG0 | rapidFIRE convention CH1 = DATA, CH2 = CS, CH3 = CLK |
| CS/DAT/CLK | PG2 | PG1 | PG0 | ExpressLRS rapidFIRE backpack wiring: CS1 = NSS, CS2 = DATA, CS3 = CLK |

What is known about the bay pins: the Fat Shark module pinout has CS1, CS2, CS3
(channel select), video, two audio, two GND and +5 V. ExpressLRS' VRx backpack
docs wire rapidFIRE as CS3 = CLK, CS2 = DATA, CS1 = NSS, and SteadyView as pin 1
= CLK, pin 2 = DATA, pin 3 = CS. pkendall's code drives SPI CLK on PG0, DATA on
PG1 and CS on PG2, so if his rapidFIRE/SteadyView support works on a Goggle 1,
CS3 = PG0, CS2 = PG1, CS1 = PG2 there. Nobody has confirmed that on hardware,
and nothing confirms it for the Goggle 2.

### True-D ("LINK" bay mode)

Command `FD 02 02 05 41 EE 00 <ch> <crc hi> <crc lo>`, ch 0-47 = A1-8, B1-8,
E1-8, F1-8, R1-8, L1-8, CRC-16/XMODEM over the first 8 bytes. The True-D listens
on its D12 only; which goggle pin reaches it is unknown, so every command goes out
on PG0, PG1 and PG2 with the same waveform (`src/module/trued.c`):

* bit-banged 9600 8N1 through the sysfs value files (opened once), every edge at
  an absolute CLOCK_MONOTONIC deadline from the first start bit, busy-waited with
  the sending thread at SCHED_FIFO for the ~10.4 ms of a packet;
* each command 5 times, 30 ms apart; the pins are outputs (idle high) only for
  that burst and inputs otherwise (the True-D's ~35 k pull-ups to 5 V keep the
  lines high);
* sent when the type is chosen, on every channel change, 1 s after bay power-on
  and again 3.5 s after power-on. Channel changes in between wait for the 3.5 s
  send, so the True-D gets at most 50 bytes while it shows its 2 s boot screen
  (it buffers 64).

The True-D "1-8" bay mode (static code on its three pins, active high, code n =
channel n+1 of its current band, stable for >= 300 ms) is the goggle's **Switch**
type; find the pin order with the Mode list.

**Setup order.** In its default AUTO mode the True-D drives its D13 at 5 V for
the first seconds after power-up. Keep the goggle's type at **Unknown** (or Pin
Control Off) until the True-D is set to a BAY mode (LINK or 1-8); only then
choose True-D (LINK) or Switch (1-8) here and turn Pin Control on.

Host tests (no hardware): `make -C utilities/module_bay_test test`.
`test_trued_uart` feeds the transmitter's pin trace, with jitter from simulated
sysfs writes and interrupts, to a pin-change and a 16x-oversampling software
UART receiver and checks the test vectors byte for byte. `test_module_worker`
runs `module.c` against recording module types: the power-on sequence above,
power-off release, Pin Control, the WiFi interlock and the Switch channel range.

## Untested on hardware

Everything that touches a real goggle:

* whether PG0-PG2 reach the bay at all (Goggle 2) and on which bay pins (both);
* that the sysfs exports work on PG0-PG2 after `rc.sh` disabled them, and that
  WiFi still starts after module control released them;
* the True-D LINK timing on the V536 (sysfs write cost, SCHED_FIFO), the
  power-on timing against the True-D's boot screen, and whether the True-D sees
  its D12 line through the expansion module;
* Switch pin orders, rapidFIRE, Steadyview, Steadyview-X on a Goggle 2;
* the WiFi interlock (enable WiFi with a module active, disable it again) and
  the PG_CFG0 read through `/sys/class/sunxi_dump`.
