# Automated QEMU Testing & Visual Verification

Project Tsukasa provides a headless QEMU test automation framework under `scripts/test/` for booting, driving mouse and keyboard input via QMP/HMP, capturing PNG screenshots, asserting serial milestones and crash-free execution, testing storage controllers, and verifying disk installation and reboot persistence.

## Prerequisites

- `qemu-system-x86_64` and `qemu-img`
- `python3` (standard library only; PNG encoding uses `zlib` and `struct`)
- Optional host filesystem validators: `e2fsck` (`e2fsprogs`) and `fsck.fat` (`dosfstools`)
- Optional UEFI firmware for `--firmware uefi`: OVMF (`OVMF_CODE.fd` and `OVMF_VARS.fd` in `/usr/share/edk2/ovmf/` or `/usr/share/OVMF/`)

Build the bootable ISO before running guest tests:

```bash
make -j$(nproc) iso-x86_64
```

## 1. Core Harness (`scripts/test/qemu_harness.py`)

`scripts/test/qemu_harness.py` is both a reusable Python library and a standalone CLI.

### Key Capabilities

- **Ephemeral QMP Port Allocation**: Passing `qmp_port=0` (default in the library and CLI) calls `find_free_port()` (`socket.bind(("127.0.0.1", 0))`) to avoid TCP port collisions across concurrent runs.
- **Machine, Firmware & Storage Matrix**:
  - `--machine pc|q35`
  - `--firmware bios|uefi` (auto-detects `OVMF_CODE.fd`, copies `OVMF_VARS.fd` into `<out-dir>/OVMF_VARS.fd` as writable pflash)
  - `--storage ide|ahci|virtio|nvme`
  - `--iso <path>` and/or `--boot-disk <path>` (direct disk boot without `-cdrom`) plus repeatable `--extra-disk <path>`
  - `--smp`, `--mem`, repeatable `--fw-cfg`, `--cmdline`, `--vnc`
- **Continuous Serial Monitoring & Crash Detection**:
  - A background reader thread streams QEMU serial output to `<out-dir>/serial.log`.
  - `wait_for_serial(pattern, timeout=..., regex=False, from_offset=0)` blocks until a pattern appears.
  - `assert_no_crashes()` fails on `PANIC`, `panic:`, `Unhandled exception`, `Page fault`, `General protection`, `Double fault`, `Triple fault`, `UBSAN`, `[ubsan]`, and `stack smashing`.
- **QMP/HMP Input & Pixel Inspection API**:
  - `move_to(x, y)` (zeros PS/2 relative pointer to `(0, 0)` then steps in `<=90` deltas)
  - `hover(x, y, duration=0.2)`
  - `click(x, y, button=1, wait_after=0.5)` (`1`=left, `2`=middle, `4`=right), `right_click(x, y, wait_after=0.5)`, `double_click(x, y, wait_after=0.5)`
  - `drag(sx, sy, steps, dx, dy, delay=0.05)`
  - `send_key(k)` and `send_text(s)` (full printable ASCII including `/`, `-`, `_`, `=`, `.`, `:`, `>`, `<`, `|`, `"`, `'`, `\n`, `\t`, spaces, upper/lowercase)
  - `screendump(name, save_png=True)` and `wait_for_screen_change(...)`
  - `region_diff_ratio`, `non_color_ratio`, `check_no_pure_black_bars`
- **Automatic Failure Diagnostics**:
  - On any exception or assertion failure inside `with QemuHarness(...) as vm:`, the harness automatically captures `<out-dir>/failure.png` (if QEMU is still running) and prints the last 100 lines of `serial.log` to `stderr`.

### CLI Examples

Boot check with milestone verification:

```bash
python3 scripts/test/qemu_harness.py \
  --action boot-check \
  --iso tsukasa.iso \
  --check-milestones scripts/ci/boot-milestones.txt \
  --out-dir tmp-boot-check
```

Focus Terminal, type a command, wait for serial output, and capture a screenshot:

```bash
python3 scripts/test/qemu_harness.py \
  --action cmd \
  --iso tsukasa.iso \
  --ready-serial "[vanilla] Listening for client connections." \
  --boot-wait 3 \
  --click 236,120 \
  --text "/bin/uname > /dev/tty0" \
  --auto-enter \
  --wait-serial "Tsukasa" \
  --screenshot cmd_result \
  --out-dir tmp-cmd
```

Capture a desktop screenshot under `q35` + UEFI:

```bash
python3 scripts/test/qemu_harness.py \
  --action screenshot \
  --iso tsukasa.iso \
  --machine q35 \
  --firmware uefi \
  --boot-wait 12 \
  --screenshot uefi_desktop \
  --out-dir tmp-uefi-shot
```

## 2. Visual Desktop Verification (`scripts/test/visual-test.py`)

`scripts/test/visual-test.py` exercises the desktop compositor and GUI applications end-to-end over QMP:

```bash
make visual-test
# or directly with an ephemeral QMP port:
python3 scripts/test/visual-test.py --iso tsukasa.iso --qmp-port 0 --scenario full --out-dir tmp-visual-test
```

Scenarios:
- `boot`: verifies `1280x800` framebuffer, dock corner alpha blending, startup windows (`Terminal`, `Task Manager`, `File Manager`), and crash-free serial log.
- `full`: runs `boot` checks plus Terminal drag without black trails, System Menu, Quick Launcher (`Alt+Space`), Task Manager Processes tab, File Manager maximize/restore SHM reflow, Notepad typing pixel delta, Calculator `7*6` rendering, Settings window painting, and `assert_no_crashes()`.
- `interactive`: boots with QMP (and optional `--vnc :1`) until interrupted.

## 3. Storage Controller Matrix (`scripts/test/test-storage-matrix.py`)

Tests QEMU separately across AHCI, VirtIO-blk, and NVMe (and optionally IDE):

```bash
# Discovery & crash-free boot probe across AHCI, VirtIO-blk, and NVMe:
python3 scripts/test/test-storage-matrix.py --iso tsukasa.iso --probe-only

# Require driver discovery logs and test both pc and q35:
python3 scripts/test/test-storage-matrix.py --iso tsukasa.iso --machine both --mode probe --require-driver

# Full R/W + host e2fsck -fn / fsck.fat -n verification:
python3 scripts/test/test-storage-matrix.py --iso tsukasa.iso --mode full --guest-cmd "<rw-cmd>" --rw-wait-serial "<marker>" --require-partitions
```

## 4. Installer & Reboot Persistence E2E (`scripts/test/test-installer-e2e.py`)

Automates the full install and reboot lifecycle:

1. Boots the live ISO with a fresh 256 MiB raw disk image and runs the installer via QMP `send_text` (`--install-cmd`, default `"/bin/install hda ERASE"`), then powers off.
2. Boots directly from `disk.img` without `-cdrom` under SeaBIOS (`--firmware bios`), creates and verifies `/root/persist.txt`, then powers off.
3. Reboots directly from `disk.img` without `-cdrom` under both SeaBIOS (`bios`) and UEFI OVMF (`uefi`), confirming the marker survives.
4. Parses MBR/GPT partition tables on `disk.img` and runs host `e2fsck -fn` / `fsck.fat -n` on extracted partitions.

Example:

```bash
python3 scripts/test/test-installer-e2e.py --iso tsukasa.iso --out-dir tmp-e2e
```

Pass `--skip-uefi` for SeaBIOS-only targets or `--install-cmd "/bin/installer --auto"` to override the installer invocation.
