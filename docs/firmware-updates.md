# Firmware updates

An IRL2FA takes new firmware over its infrared link, from another IRL2FA or
from a computer with a cheap USB infrared adapter. A small bootloader accepts
only firmware signed with the IRL2FA release key. Every code and contact stays
on the device. There is no USB and no radio, and nothing else changes on the
hardware.

- [Updating a device](#updating-a-device)
- [The USB infrared adapter](#the-usb-infrared-adapter)
- [Security model and limits](#security-model-and-limits)
- [How it works](#how-it-works): [flash layout](#flash-layout), [image format](#image-format), [transfer protocol](#transfer-protocol), [power cuts](#power-cuts), [the flash budget](#the-flash-budget), [locking the debug port](#locking-the-debug-port)
- [Releases and signing keys](#releases-and-signing-keys), [forks](#forks-your-own-key)
- [What is tested, and where](#what-is-tested-and-where)

## Updating a device

You need the update (`irl2fa-<version>.irl2fa` from a
[release](https://github.com/gemivnet/IRL2FA/releases)) on another IRL2FA, or
on a computer with the [adapter](#the-usb-infrared-adapter).

1. **On the device to update:** Settings → **Firmware update** → **Receive an
   update**. If the device has a PIN, it asks for it again. In simple mode,
   Settings is only reachable the usual way: hold the centre for 10 seconds
   and enter the family PIN first.
   The screen says *Waiting for the update* and stays like that until it is
   done (the bootloader has no display of its own). ← cancels.
2. **Send it**, holding the two top edges a few centimetres apart:
   - from another IRL2FA that already runs the new version: Settings →
     Firmware update → **Send it to another**. Its screen shows the progress.
   - from a computer:

     ```sh
     python3 firmware/tools/irl2fa_update.py --port /dev/ttyUSB0 irl2fa-v0.2.0.irl2fa
     ```

     (`pip install pyserial` once; on Windows the port is `COM3` or similar.)
     The tool checks the signature first, then shows the progress.
3. The transfer takes about **2 minutes** (48 KB at 4800 baud). Then the device
   checks the signature, copies the new firmware into place (about 20 s), and
   restarts into it: *Firmware updated, now running v…*. Codes, contacts, the
   PIN and the settings are kept.

If anything goes wrong the device restarts into the firmware it had and says
why: *Older than this one*, *Not signed by IRL2FA*, *The other device went
quiet*. A transfer that stops halfway (out of range, a flat cell, the cell
pulled out) **resumes where it stopped** when you send the same file again.

A device that received an update over IR can pass it on: "Send it to another"
appears when the device holds its own signed image (factory images built by
the release workflow include it too).

## The USB infrared adapter

The device's link is a plain UART at **4800 baud, 8N1, inverted** (idle = LED
off, a 0 bit = light), half duplex. Any USB-UART that can invert both lines
works. About $5 of parts:

| Part | Example | Note |
|-|-|-|
| USB-UART with an FT232R | any "FT232RL breakout", 3.3 V setting | FTDI's FT_PROG: tick **Invert TXD** and **Invert RXD**, program once |
| 940 nm IR LED | Vishay TSAL6200 or VSMB2948SL (the board's own) | |
| NPN transistor or logic MOSFET | 2N3904 / AO3400A | low-side switch for the LED |
| Phototransistor | Vishay VEMT2523SLX01 (the board's own), or any 940 nm NPN phototransistor | |
| Resistors | 100 Ω (LED), 1 kΩ (base), 10 kΩ (emitter to GND) | |

```text
 FT232R (3.3 V, TXD/RXD inverted)
   TXD ──1k──B  2N3904                3V3 ──100Ω──|>|── C  (LED: anode to the resistor)
               E── GND                          LED  K ── C of the 2N3904
   3V3 ── C  phototransistor
          E ──┬── RXD
              └── 10k ── GND
   GND ── GND
```

With the inversion set, TXD is low when idle, so the LED is off between
frames, and RXD reads the phototransistor's emitter (light = high). Point
both parts the same way, side by side, and hold them 1-3 cm from the
device's top edge, where its LED and phototransistor look out. The adapter
hears its own LED; the tool ignores its own frames.

Without an FT232R: any 3.3 V USB-UART, with one inverter (74LVC1G04) on TXD
before the transistor and one on the phototransistor's output before RXD.

**Bench test without infrared.** On a bare board, connect an inverting
USB-UART straight to the MCU: adapter TXD → **PA10** (IR_RX), adapter RXD ←
**PA9** (IR_TX), GND to GND, at 3.3 V logic. Use the board's test pads for
those nets if your board revision has them, otherwise the phototransistor's
emitter pad (PA10 side of R10) and the IR MOSFET's gate (PA9 side of R9). The
phototransistor is unpowered except during IR use and R10 is 10 kΩ, so the
adapter easily drives the line. Power the board from its cell, or from the
adapter's 3.3 V with the cell out, never both. `--port` also takes
`tcp://host:port` (for instance an emulator's UART).

## Security model and limits

**What it protects.** The bootloader installs and runs only firmware signed
with the release key (Ed25519). Without that key nobody can put their own
firmware on an IRL2FA over IR: not someone holding the device for a minute,
not a fake "update" from another device. A signed image cannot be replaced by
an *older* signed one (anti-rollback), so a fixed bug cannot be brought back.
Updating keeps the encrypted record store as it is; the new firmware reads
it with the same keys, which never leave the chip.

**Readout protection is never automatic.** The owner turns it on with
Settings → **Lock debug port** ([below](#locking-the-debug-port)). From then
on a debugger cannot read or write the flash; the only way back is the RDP
regression, which erases the whole flash, codes and contacts included, and
the IR update above is the only way to change the firmware. Until then
anyone holding the device with a debug cable can read its secrets (without a
PIN, in plain text).

**The PIN gate.** "Receive an update" asks for the device PIN again (a wrong
one counts towards the ten that erase everything, and locks the device). In
simple mode it is behind the family PIN as well. The gate is about not being
interrupted, not about trust: the signature is what decides.

**Limits.**

- Whoever holds the release key can sign firmware that every IRL2FA accepts,
  including firmware that reads out the store. Key custody is the trust
  anchor; it never leaves the release workflow's secret store and the
  maintainer's machine.
- The bootloader itself is never updated over IR. A bug in it stays for the
  life of the device unless the device is erased and reflashed over SWD.
- The update is not encrypted: the firmware is open source, nothing in it is
  secret.
- Someone with physical access and a working PIN can install any *newer*
  signed release. They cannot install anything else.
- A device that is in update mode can be kept busy (offers that fail late,
  for instance an image signed with another key takes a full transfer to
  refuse). The device then restarts into its firmware; nothing is changed.
- Fault injection (voltage glitching) defeats RDP level 1 on many STM32 parts;
  a PIN is what protects a stolen device's secrets.
- A *development* image (unsigned, from a build without the key) is only run
  while readout protection is off and never turns it on. It cannot be sent
  over IR and is never accepted as an update.

## How it works

### Flash layout

STM32U083KC, 256 KB in 128 pages of 2 KB
([`flash_layout.h`](../firmware/port/stm32u0/flash_layout.h); the linker
scripts, `sign_image.py` and `check_hw_constants.py` agree with it):

| Region | Address | Size | Contents |
|-|-|-|-|
| Bootloader | `0x08000000` | 26 KB (pages 0-12) | `port/stm32u0/boot/`: 24.5 KB, of which Monocypher's Ed25519 check is about 13 KB |
| Application slot | `0x08006800` | 96 KB (pages 13-60) | image header (padded to 256 bytes), then the application (vector table at `0x08006900`) |
| Staging slot | `0x0801E800` | 50 KB (pages 61-85) | an update image as received (header + compressed application) |
| Journal | `0x0802B000` | 4 KB (pages 86-87) | the bootloader's install journal and anti-rollback floor |
| Record store | `0x0802C000` | 80 KB (pages 88-127) | unchanged: codes, contacts, settings. Never erased or written by the bootloader |
| SRAM mailbox | `0x20009FF0` | 16 bytes | application ↔ bootloader messages (and 32 bytes below it, the bootloader's test block) |

### The flash budget

**This layout is fixed.** The record store keeps its 80 KB (512 accounts), so
the application, its compressed image and the bootloader have to fit the
regions above for good. At v0.1.0, with "Lock debug port" (the build's
`size-report.txt`):

| | Used | Region | Left |
|-|-|-|-|
| Application | 97,388 bytes | 98,048 (96 KB slot less the 256-byte header) | **660 bytes** |
| Update image (Thumb filter + LZMA) | 49,233 bytes | 51,200 (staging slot) | 1,967 bytes |
| Bootloader | 24,976 bytes | 26,624 | 1,648 bytes |

So the application can grow by **less than 1 KB** more, ever, unless
something else shrinks; its compressed image grows about half as fast and
has a little more room. The build fails, loudly, when any of them stops
fitting: the linker for the application or the bootloader, `size_report.py`
(`ERROR: the application is N bytes, M more than its 98048 ...`) for all
three, and `sign_image.py` for an update image that would not fit staging.
Before adding a feature, measure it (`firmware/port/stm32u0/build.sh`
prints the table) and find the bytes.

The target was a 16 KB bootloader. It came out at 24.5 KB: Monocypher's
field arithmetic is written for speed (unrolled 10-limb multiplies), about
10 KB of Thumb code, and replacing it with a smaller, slower or home-made
Ed25519 was not worth the risk. The rest is SHA-512, the LZMA decoder, the
IR receiver, the journal and the flash driver with its ECC recovery.

### Image format

An update image (`.irl2fa`, and what the staging slot holds) is a 128-byte
header and the payload ([`fwimage.h`](../firmware/core/fwimage.h)):

| Offset | Size | Field |
|-|-|-|
| 0 | 4 | magic `I2FW` |
| 4 | 2 | header version (1) |
| 6 | 2 | flags: bit 0 = unsigned development image |
| 8 | 4 | firmware version, `major << 16 | minor << 8 | patch` |
| 12 | 4 | hardware ID, `0x00830001` (STM32U083KC, IRL2FA board) |
| 16 | 4 | lowest bootloader version that can install it (1) |
| 20 | 4 | application size (bytes) |
| 24 | 4 | payload size (bytes) |
| 28 | 1 | compression: 0 stored, 1 LZMA, 2 ARM-Thumb branch filter + LZMA (releases) |
| 29 | 1 | LZMA properties byte (lc 0, lp 1, pb 1) |
| 30 | 1 | log2 of the LZMA dictionary (13: 8 KB, the bootloader's RAM window) |
| 31-63 | | zero |
| 64 | 64 | Ed25519 signature (RFC 8032, SHA-512) of **SHA-512(header[0..63] ‖ application)** |

All integers little-endian. The signature covers the *decompressed*
application, so the same header sits in the application slot (padded with
0xFF to 256 bytes) and the bootloader checks it there at every start. A
development image has the 64-byte SHA-512 digest itself where the signature
goes.

Compression: xz's ARM-Thumb BCJ filter (it turns BL offsets into absolute
targets, which repeat) then raw LZMA1 with an 8 KB dictionary: 96 KB → 48 KB.
Both are decoded by our own [`lzma_dec.c`](../firmware/core/lzma_dec.c) (from
Igor Pavlov's public-domain LzmaSpec) and the 4-byte BCJ window in
`fwimage.c` (from liblzma's public-domain `armthumb.c`); the encoder is
Python's standard `lzma` module.

Keys and images: [`firmware/tools/sign_image.py`](../firmware/tools/sign_image.py)
(`keygen`, `sign`, `verify`, `info`, `factory`). Its Ed25519 is RFC 8032's
reference algorithm in pure Python (no dependency; checked against the RFC's
vectors and against the C side).

### Transfer protocol

The pairing link's framing (`'I' '2' type len payload crc16`, see
[pairing-protocol.md](pairing-protocol.md)) with three new frame types
([`fwlink.h`](../firmware/core/fwlink.h)):

| Type | Direction | Payload |
|-|-|-|
| `0x21` OFFER | sender → device | the 128-byte header |
| `0x22` DATA | sender → device | offset (u32) ‖ up to 128 payload bytes |
| `0x23` STATUS | device → sender | state ‖ error ‖ next offset (u32) ‖ anti-rollback floor (u32) |

The sender repeats OFFER every 0.6 s until the device answers. The device
checks the header first (hardware, bootloader version, size, version against
the floor, signed at all) and refuses at once if it would never install it.
Then it asks for the payload by offset: each STATUS names the next offset
wanted, the sender sends exactly that chunk, and resends after 0.9 s of
silence. A lost frame costs one retry; nothing is written twice. The device
writes each chunk straight into the staging slot (erasing a page when a chunk
starts one). If the same image is offered again later, the device finds the
first incomplete chunk in staging and asks from the start of its page:
**resume**. Each side hears its own LED and ignores its own frame types; the
device only ever talks in answer, so the half-duplex link needs no other
arbitration.

When everything is in, the device says *complete*, checks the signature over
the whole decompressed image, installs, and answers the sender's polls with
*done* (or the error) for 6 seconds before restarting. The sender gives up
after 20 s of silence, or 2 minutes of installing.

Entry points: the application's "Receive an update" puts `UPDATE` in the RAM
mailbox and resets; the bootloader also listens on its own when no valid
application is left. It gives up after 3 minutes without a frame (or ←) and
restarts into the application; with none, it sleeps in Stop 2 and listens
again when the joystick is pressed. Device to device, the sender is the
application itself ([`app.c`](../firmware/core/app.c), "Send it to another"),
sending its staging slot when that holds the image it runs.

### Power cuts

A cell pulled at any moment leaves a bootable device. The bootloader checks
the application at **every** start (SHA-512 over it and the Ed25519
signature) before running it: about 35 million instructions, 2.2 s at 16 MHz
in Renode (a little more on silicon, where loads take two cycles), so a new
cell or a reset shows the first screen about 2-3 s later. Waking from sleep is
not a start (Stop 2 keeps the core's state) and costs nothing extra. The
bootloader keeps its state in a journal
([`fwlog.h`](../firmware/core/fwlog.h)) of 8-byte, CRC-checked, write-once
records in two pages: `INSTALL v` (staging holds a verified image v, copying
may have begun), `DONE v` (installed; the anti-rollback floor rises to v),
`CANCEL`, `FLOOR`. A record torn by a power cut reads as noise or as an ECC
error and is skipped; a full page is compacted into the other one, whose
first record (written last) makes it live.

| Cut during | Afterwards |
|-|-|
| receiving | the application slot was not touched: the old firmware runs. Sending again resumes. |
| checking the signature | nothing written yet: the old firmware runs. |
| writing `INSTALL` | torn: skipped, the old firmware runs; complete: as below |
| copying | the journal says an install is under way: the bootloader checks staging again and finishes the copy. The copy is idempotent (pages already right are skipped), page 0 with the header goes last, so a half-copied slot never verifies. Then the new firmware runs. |
| writing `DONE` | the copy is checked again (nothing to write), `DONE` written, the new firmware runs |
| a journal page change | the old page stays live until the new one is complete |

Torn flash words raise an ECC NMI on the STM32U0 (RM0503 3.3.5); the
bootloader reads the slots and the journal through the same NMI-recovering
path as the record store ([firmware-hardware-review.md](firmware-hardware-review.md) F1).
If the application slot is ever found damaged with no install pending, the
bootloader reinstalls the image in staging (when it holds a signed one at
least as new); with nothing to install from, it waits for an update.

The record store is never written by the bootloader (its flash callbacks
refuse anything outside the slots and the journal), so codes and contacts
survive any of the above.

### Locking the debug port

Readout protection (RDP level 1) is **never set automatically**: not at the
first start, not on a programming fixture, not when the device wakes.
JLCPCB, or whoever flashes the factory image over SWD, only ever handles an
unprotected chip, and so does a developer.

The owner sets it: Settings → **Lock debug port**. It asks for the device
PIN again if one is set, and in simple mode it is behind the family PIN like
every setting. A screen says, in plain words, what it does: it stops someone
who has the device and a debug cable from reading its codes; it is
permanent, undoing it erases everything; updates over IR still work. Then a
confirm. The application puts `LOCK` in the RAM mailbox and restarts
([`fw_app.c`](../firmware/port/stm32u0/fw_app.c)); the bootloader sets RDP
level 1 if, and only if:

- the chip is still at level 0,
- the application is **signed** (a development image never locks the chip),
- the cell measures at least **2.9 V** twice, 20 ms apart (2.96 V on parts
  without ST's VREFINT calibration, ES0602 2.1.2).

That last rule is [firmware-hardware-review.md](firmware-hardware-review.md)
F7: an option-byte write cut by a power loss can lock the part for good
(RM0503 3.4.2), so it is only attempted on a cell that will not sag. The
write ends with `OBL_LAUNCH`, which restarts the chip once. The bootloader
tells the application the outcome through the mailbox, and the device says
*Debug port locked*, *Not locked: battery low*, or *Not locked: development
firmware*. Settings then shows "Debug port: locked", and About shows
"Debug port locked" or "Debug port open".

## Releases and signing keys

The release key's public half is
[`firmware/keys/irl2fa-release.pub`](../firmware/keys/irl2fa-release.pub); the
bootloader build embeds it. The private half is a 32-byte Ed25519 seed (64
hex digits), kept by the maintainer and, for releases, in the repository
secret **`IRL2FA_SIGNING_KEY`**. Tagging `vX.Y.Z` (it must match
`IRL2FA_VERSION` in `firmware/core/app.h`) runs
[`release.yml`](../.github/workflows/release.yml):

- with the secret: `irl2fa-vX.Y.Z.irl2fa` (the signed update) and
  `irl2fa-factory-vX.Y.Z.bin/.hex` (bootloader + signed application + its
  update image staged, so the device can pass it on): what goes to JLCPCB or
  onto a new board over SWD;
- without it: only `irl2fa-factory-UNSIGNED-vX.Y.Z.bin/.hex` and a warning in
  the release notes. Such a device is a development unit: it runs, never
  locks itself, and becomes a normal one with the first signed update.

**Flashing the factory image** (bootloader + application in one file, for the
whole chip from `0x08000000`): see
[firmware/port/stm32u0/README.md](../firmware/port/stm32u0/README.md#flashing-st-link--tag-connect-tc2030).

### Forks: your own key

A fork cannot sign images IRL2FA devices accept, and does not need to:

```sh
python3 firmware/tools/sign_image.py keygen --out ~/.config/irl2fa/my-key --pub firmware/keys/irl2fa-release.pub
firmware/port/stm32u0/build.sh            # the bootloader now embeds your public key
python3 firmware/tools/sign_image.py sign --key ~/.config/irl2fa/my-key --in firmware/build/stm32u0/irl2fa.bin --out mine.irl2fa
python3 firmware/tools/sign_image.py factory --boot firmware/build/stm32u0/boot.bin --image mine.irl2fa --out factory.bin --hex factory.hex
```

(or keep the file and pass `-DIRL2FA_BOOT_PUBKEY=<hex or file>` to CMake;
`NONE` builds a bootloader that accepts no update at all). Put your seed in
your fork's `IRL2FA_SIGNING_KEY` secret.

A device already running another key's bootloader keeps trusting that key:
the bootloader is not replaceable over IR. Changing it means SWD, and on a
device with readout protection that means the **RDP regression, which
mass-erases the whole chip, codes and contacts included**. That is on
purpose: otherwise anyone with a debugger could swap the key and the
firmware.

## What is tested, and where

| Property | Host tests (`make -C firmware test`) | Renode (`firmware/renode/update.robot`) |
|-|-|-|
| Ed25519 against RFC 8032; C and Python signers agree | `test_fw_ed25519`, `test_fw_header`, `tools/test_update_tools.py` | |
| LZMA and ARM-Thumb decoding against Python's encoder; damaged streams | `test_fw_lzma`, `test_fw_thumb` | the real application image, every update test |
| Refusals: other key, wrong hardware, newer bootloader needed, older version, unsigned, too big, truncated, corrupted chunk, torn word | `test_fw_verify_errors`, `test_fw_install_and_update`, `test_fw_factory_floor` | *Refuses An Image Not Signed With Its Key*, *Refuses An Older Image At Once* |
| Transfer with 20-30 % frame loss and corrupted bytes, receiver restarts, resume | `test_fw_link`, `test_fw_install_and_update` | *Updates Over IR Through Power Cuts In Every Phase* |
| Power cut at **every** flash operation of a transfer and of an install (dropped, half done, noise with ECC errors), and again during the recovery; the record store byte for byte | `test_fw_powercut_receive`, `test_fw_powercut_install`, `test_fw_log` | 3 cuts while receiving, 4 or more while installing, store hashed before and after |
| Device to device through the screens; PIN and simple-mode gates; the report after the restart | `test_app_update_*` | *Updates Over IR And Reports It* |
| Host tool against the bootloader's receiver code, resume | `tools/test_update_tools.py` | |
| Locking the debug port: only from Settings, behind the PIN and the simple-mode gate, explained and confirmed, the outcome shown, the state in About | `test_app_lock_*` | |
| RDP never set on its own (power-ons, wakes); set once from Settings on a healthy cell with a signed image; declined on a weak cell and for a development image | | *Never Locks The Debug Port On Its Own*, *Locks The Debug Port Once From Settings*, *Declines To Lock On A Weak Cell*, *Never Locks A Development Image* |
| Parser and decoder robustness | `fuzz/fuzz_fwupdate.c` (libFuzzer, 60 s in CI) | |

Only in emulation, so far: the option-byte write and `OBL_LAUNCH`, flash
timing, and what a real torn write leaves (the
Renode model tears the last operation and injects an ECC error, as the
silicon is documented to). On a real board, check: an update between two
devices and from the adapter at 1-3 cm, a cell pulled during the copy, and
that RDP reads 0 after programming and after use, and 1 after Settings → Lock
debug port on a fresh cell (`STM32_Programmer_CLI -c
port=SWD -ob displ`). See [hardware-testing.md](hardware-testing.md).
