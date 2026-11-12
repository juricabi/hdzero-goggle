# First flash: the order that risks least (Goggle 1)

This page is for flashing this custom firmware onto an original HDZero Goggle (Goggle 1)
for the first time, checking it, and going back to the official firmware if you want
to. Nothing here has been tried on a real goggle: the menu texts and the file handling
below are read from the code (`src/ui/page_version.c`, `mkapp/app/script/update_goggle.sh`)
and from the official files; where something could not be checked, the text says so.

The image of this build is `HDZERO_GOGGLE-77-206-9.6.0-rc1-<commit>.bin`, with
`HDZERO_GOGGLE-77-206-9.6.0-rc1-<commit>.bin.sha256` next to it (`<commit>` is the
short commit id of the build).

Do the sections in order. Do not change any setting before section 4 is done.

## 1. Keep these on the PC before you flash

1. **The official firmware of the same version**, to go back with:
   `HDZERO_GOGGLE-77-206-9.6.0-rc1.bin`, from the GitHub release of hdzero-goggle
   9.6.0-rc1 (a pre-release):
   https://github.com/hd-zero/hdzero-goggle/releases/tag/v9.6.0-rc1. The copy the
   review downloaded is 8,151,040 bytes with SHA-256

   `15a6266d9581a648589ff997540cdf256af6d1e300f57e5081cd04dde158ba79`

   GitHub lists the same digest for that file in its release data. It is a tar archive
   of `hdzgoggle_app_ota-9.6.0-rc1.tar`, `HDZGOGGLE_RX-77.bin` and `HDZGOGGLE_VA-206.bin`.
2. **The full recovery package** `HDZEROGOGGLE_Rev20250319.zip`, from
   https://www.hd-zero.com/document (Goggle). The copy the review downloaded is
   25,300,568 bytes with SHA-256

   `17cba93bbd29c20a5c011947c368a65d10b1d6c2fd8ad6715138c5f349475fe7`

   It contains `HDZERO_GOGGLE-75-200-9.5.0.bin` and a folder `Recovery/` with
   `HDZG_BOOT.bin`, `HDZG_OS.bin`, `HDZGOGGLE_RX.bin`, `HDZGOGGLE_VA.bin` and
   `HDZero-Goggle-User-Manual-v1.9.pdf`. This hash only tells you whether your download
   is the same file the review looked at: no checksum published by HDZero was found for
   it, and the hd-zero.com page itself was not opened for this document (the review
   downloaded the file by its direct link).
3. **This build's image and its `.sha256` file**, checked (below).

**Checking a SHA-256 on Windows.** In a Command Prompt or PowerShell window, in the
folder with the file:

```
certutil -hashfile HDZERO_GOGGLE-77-206-9.6.0-rc1.bin SHA256
```

or in PowerShell:

```
Get-FileHash .\HDZERO_GOGGLE-77-206-9.6.0-rc1.bin -Algorithm SHA256
```

`certutil` prints the hash in small letters, `Get-FileHash` in capitals; compare the 64
characters and ignore the case. For this build's image, compare with the first 64
characters of its `.sha256` file (open it in Notepad: the hash, two spaces, the file
name), or let PowerShell compare (it ignores the case):

```
(Get-FileHash .\HDZERO_GOGGLE-77-206-9.6.0-rc1-<commit>.bin -Algorithm SHA256).Hash -eq (Get-Content .\HDZERO_GOGGLE-77-206-9.6.0-rc1-<commit>.bin.sha256).Split(' ')[0]
```

It must print `True`. If any hash differs, download or copy the file again; never
flash a file whose hash does not match.

Also write down (or photograph) the settings you care about: a recovery (section 8)
may reset them; whether it does was not checked.

## 2. The SD card: only the one image on its root

Use a FAT32 card (the official update instructions ask for FAT32 and no older firmware
on the card's root). Put **only** `HDZERO_GOGGLE-77-206-9.6.0-rc1-<commit>.bin` on the
root of the card: not its `.sha256`, no other `HDZERO_…` file, no `develop.sh`. Folders
(DVR recordings) can stay.

Why, from the code:

* The Firmware page lists **every file on the card's root whose name contains
  `HDZERO_GOGGLE`** anywhere (`page_version.c`: a substring match on the file names of
  `/mnt/extsd`). So the `.sha256` file (`…bin.sha256`) and every Goggle 2 image
  (`HDZERO_GOGGLE2-…`) appear in the list next to the right one. If any listed name
  contains `.zip`, the page also treats the chosen file as a zip archive.
* Whatever you choose goes to `update_goggle.sh`, which does not check what the file
  is. It unpacks it with `tar`, then **erases** the two RX flash chips (`/dev/mtd8`,
  `/dev/mtd9`) and the VA flash (`/dev/mtd10`) and writes what it unpacked. For a file
  that is not a tar archive (such as the `.sha256`), nothing is unpacked, the erase
  still happens and the writes fail; the app is left as it was (`hdz_upgrade_app.sh`
  finds no package and stops); and the script still prints "all done", so the screen
  reports a completed update. The goggle then has no HDZero video until a correct image
  is flashed. (This confirms the review's warning by reading the script; it was not
  tried.)
* A Goggle 2 image contains the same file names inside (`HDZGOGGLE_RX-77.bin`,
  `HDZGOGGLE_VA-198.bin`: listed here with `tar`), and the script checks only the sizes
  of the flash chips, so on a Goggle 1 it would write the Goggle 2's VA image and app.

With one file on the card there is nothing to choose wrongly.

## 3. Flash from the menu

1. Only the power cable connected (the official instructions: no HDMI, no USB), and a
   power source that will not run out for 10 minutes.
2. SD card in, power on. Main menu > **Firmware**. The **Current Version** row shows what
   is installed now: write it down.
3. Select **Update Goggle** and click. A window opens, titled "Goggle Firmware SD Card",
   with "Target:" and the file name: check that it is this build's image. ("Goggle
   Firmware not found" means the card has no file with `HDZERO_GOGGLE` in its name on
   its root.)
4. In the window, select **Update Goggle** and click. The row shows "WAIT... DO NOT
   POWER OFF..." and a progress bar (it runs to about 45 % for the RX and VA images,
   then to 100 % for the app). **Do not power off.** It takes about 3 minutes (the
   official page).
5. When the update script reports "all done", the goggle shows **"Update complete"**:
   "Goggle update completed successfully. Please repower goggle now.", beeps three
   times and ignores the buttons. Power off, wait 5 s, power on.
6. Main menu > Firmware: Current Version now starts with `app: 9.6.0-rc1-` followed by
   the first 5 characters of `<commit>` (the row has room for 9 characters after the
   first dash).
7. Take the image off the card's root again, so that the Firmware page does not offer
   it again later.

If the row shows **"No firmware found."** or **"FAILED"** (red) instead: power off, put
only the official `HDZERO_GOGGLE-77-206-9.6.0-rc1.bin` on the card's root and flash it
the same way (section 8.1); if the goggle no longer reaches its menu, section 8.2.

## 4. Check the untouched goggle

Change no setting yet. Check each item against what the goggle did with the official
firmware:

1. **HDZero video and OSD**: picture, OSD elements, channel.
2. **DVR**: record 1 minute with your usual settings (Record Option > Record Audio =
   Yes; the audio source is taken over from your old Audio Source, the built-in mic by
   default), then play it back (Main menu > Playback): picture and mic sound.
3. **Headphones at boot**: headphones on before you power on; listen during the first
   10 s after boot. The official firmware is silent there; note anything you hear (how
   long, what it sounds like): the review found that up to about 2 s of A/V-in sound are
   possible after the SD card check at boot.
4. **Analog with the bay module**: insert the module only with the goggle off; then
   Source > Analog. The picture must be as before. With Module = None (the default) the
   goggle does not set the module's channel: set it on the module.
5. **HDMI out**: the picture on a monitor.
6. **WiFi**: WiFi Module > Enable = On > Apply Settings (click twice); connect and
   stream as you usually do; then Enable = Off > Apply Settings.
7. **ELRS backpack**, if you use it: as before.
8. **Standby and wake**: Go Sleep!, then wake it as you usually do.

If something that worked with the official firmware does not work now, go back to the
official firmware (section 8.1) and report what you saw.

### What looks different from stock

<!-- differences: finalised by the code change -->

* Main menu: two new entries, **Audio** and **Module Bay**, right before "Go Sleep!";
  every other stock entry keeps its place. The list scrolls and shows a scroll
  indicator.
* Record Option: the Audio Source row has moved to the Audio page (the old choice is
  taken over).
* DVR playback plays on both ears and mutes the live A/V sound while a file plays.
* WiFi Module > System has a new **Stream Audio** row (Off).
* Firmware > Current Version shows 9.6.0-rc1-`<commit>` (step 6 of section 3).
* `setting.ini` gets a `[mixer]` section on the first boot.

## 5. Then the Audio page

Main menu > **Audio**. Change the levels under Headphones one at a time and listen
(Mic; Line In if you use that jack; A/V In with an analog source). Then record each
source you want: set its row under Recording to Yes, record about 30 s, play it back,
and set it back as you want it. [audio.md](audio.md) explains every row. Leave the two
rows under "HDMI Audio (experimental)" Off until section 6 says otherwise.

## 6. Optional: the probe kit (HDMI audio)

Only if you want HDMI audio. Everything is in `utilities/probe/` of this repository
([utilities/probe/README.md](../utilities/probe/README.md)):

1. **The read-only probe first.** Copy `hdmi_audio_probe.sh` to the SD card's root and
   rename it `develop.sh` (the goggle then runs it instead of starting the goggle app;
   the script starts the app itself). Optionally connect an HDMI display to HDMI out.
   Boot: the screen stays dark for up to about a minute, then **2 beeps** and the app
   starts. Leave the goggle on for **2 more minutes**. Power off, and keep
   `hdmi_audio_probe_N.txt`, `hdmi_audio_dmesg_N.txt` (and `hdmi_audio_config_N.gz` if
   it exists) from the card. **Delete or rename `develop.sh`.**
2. **Then the test script**, `hdmi_audio_test.sh` as `develop.sh` together with
   `tone1k_48k_s16le_2ch.raw`, as in part 2 of the probe README (a TV or monitor with
   speakers on HDMI out; for part B an HDMI source that plays sound). Delete
   `develop.sh` afterwards.
3. Switch HDMI Out Audio / HDMI In Audio on in the Audio page **only if the probe shows
   the wiring**: the decision table in [hdmi-audio.md](hdmi-audio.md), section 8, says
   what the results mean.

Do not run `bay_gpio_pattern.sh` here: it belongs to the module bay checklist (section 7).

## 7. Then the module bay

Only after sections 1-4, and with the checklist of
[module-bay-mod-safety.md](module-bay-mod-safety.md), which is the one to follow. In
short, its order:

1. Bay empty, Module Bay > Module = **None** (the default; Status "Off, bay pins
   untouched"), WiFi off.
2. **Check Pins** twice (checklist step 3): the result, for example "Pins 1 1 0, 3.3 V",
   ends with the PG bank voltage. If it shows "? V", the pin finder
   (`bay_gpio_pattern.sh`, checklist step 4, with clip leads attached while the goggle
   is off). If Check Pins refuses, its row says why: "Not read: set Module to None
   first", "Not read: switch WiFi off first", "Not read: pins in use by WiFi" or "Not
   read: no GPIO access".
3. After Check Pins, switch WiFi on and confirm that it still connects (checklist
   step 5), then off again.
4. Then the mod, by the voltage: **resistors (3.3 V, path R)** or **the LM339 shifter
   (1.8 V, path S)**, checklist steps 6-21.
5. Then the True-D (MOD 1.17, `BAY:RAPIDFIRE`) in the bay with the goggle off, and
   **Module = rapidFIRE** (checklist steps 22-25). Keep WiFi off while flying analog.

## 8. Back to the official firmware

### 8.1 From the menu (the goggle still reaches its menu)

Put **only** the official `HDZERO_GOGGLE-77-206-9.6.0-rc1.bin` on the card's root and
flash it exactly as in section 3. The update script is the goggle's own (`update_goggle.sh`
is unchanged in this build: git shows no change under `mkapp/app/script/` since the
upstream base a85841d), and the official image carries its own RX, VA and app.

The same applies when the goggle reached its menu but shows no HDZero video after an
update (for example after the wrong file was chosen, section 2): flash the official
image from the menu.

### 8.2 If the goggle app no longer starts: the emergency update

The official "Goggle Emergency Firmware Update Process"
(https://docs.hd-zero.com/goggles-firmware-update), with the files of the recovery zip:

1. On a FAT32 card's root, only `HDZG_OS.bin`, `HDZGOGGLE_RX.bin` and `HDZGOGGLE_VA.bin`
   from the zip's `Recovery/` folder.
2. Only the power cable connected; card in; power on and **wait 5 minutes**; power off.
   Do not cut it short.
3. If it worked, the three files are gone from the card.

The review says the goggle's boot loader (u-boot) does this before the app starts, so
it works even when the app partition is broken; the official page does not say which
part of the system does it, and this was not checked here. Afterwards the goggle runs
the firmware of that recovery package (dated 2025-03-19; which app version its
`HDZG_OS.bin` carries was not checked); flash 9.6.0-rc1 from the menu (8.1) if you want
it.

### 8.3 Last resort: PhoenixCard

The official page also describes a PhoenixCard process (Windows only): the
`PhoenixCard.zip` tool from the same page writes a bootable card, the goggle flashes
itself from it (a long beep at once, another after about 3 minutes), then the card is
restored to a normal FAT32 card for the RX and VA files. Follow the page exactly,
including its figures. Its warning matters: a bootable card has a hidden partition, and
if such a card is in the goggle at power-on, the goggle may flash itself again.

### 8.4 What stays in setting.ini

After going back, `setting.ini` keeps this build's `[mixer]` and `[module]` keys (and
`[wifi] stream_audio`): the official firmware does not read them, so they do no harm.
The official firmware takes the recorded audio source from the old
`[record] audio_source`, which this build never changes: a source you chose on the Audio
page is lost; set Record Option > Audio Source again.

## What could not be checked here

* The flash itself, every menu text on a real screen, and the timings: read from the
  code, never run on a goggle.
* The hd-zero.com download page: not opened; the recovery zip's hash is that of the
  review's download only.
* The official update page (docs.hd-zero.com/goggles-firmware-update) was read for this
  document; its figures were not, so follow the page itself for PhoenixCard.
* Whether the emergency update (8.2) or PhoenixCard (8.3) resets the settings, and which
  app version `HDZG_OS.bin` carries.
* That u-boot performs the emergency update (the review's statement; not on the official
  page).
