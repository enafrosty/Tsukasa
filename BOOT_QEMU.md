# Booting Tsukasa with QEMU

Tsukasa boots on `x86_64` hardware and emulators via the Limine v8 bootloader (supporting both Legacy BIOS and UEFI).

## Prerequisites

### Linux / Fedora
```bash
sudo dnf install -y qemu-system-x86 qemu-img
```
Or run `./setup_fedora.sh` to install the full build, emulation, and filesystem check toolchain.

### Linux / Debian / Ubuntu / WSL2
```bash
sudo apt-get install -y qemu-system-x86 qemu-utils
```
Or run `./setup_wsl.sh`.

### Windows
- **Chocolatey:** `choco install qemu`
- **Scoop:** `scoop install qemu`
- **Installer:** https://www.qemu.org/download/#windows

Optional for UEFI testing: install OVMF firmware (`edk2-ovmf` on Fedora, `ovmf` on Debian/Ubuntu).

---

## Quick Launch

From the repository root after building `tsukasa.iso`:

```bash
make run          # Builds tsukasa.iso if needed, creates disk.img, starts QEMU
# or:
./run_qemu.sh     # Linux / macOS / WSL2
.\run_qemu.ps1    # Windows PowerShell
```

---

## Storage Controller Configurations

First create a raw virtual disk (at least `96M` if you want to run `/bin/installer` onto it):

```bash
qemu-img create -f raw disk.img 256M
```

### 1. Default PC / IDE + E1000 Network
```bash
qemu-system-x86_64 \
    -cdrom tsukasa.iso -hda disk.img -boot d \
    -m 256 -smp 2 -vga std -serial stdio \
    -netdev user,id=u1 -device e1000,netdev=u1
```

### 2. Q35 + AHCI SATA (`/dev/sda`)
```bash
qemu-system-x86_64 \
    -machine q35 -cdrom tsukasa.iso -boot d \
    -drive if=none,id=d0,file=disk.img,format=raw \
    -device ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0 \
    -m 256 -smp 2 -vga std -serial stdio \
    -netdev user,id=u1 -device e1000,netdev=u1
```

### 3. Q35 + VirtIO-blk (`/dev/vda`)
```bash
qemu-system-x86_64 \
    -machine q35 -cdrom tsukasa.iso -boot d \
    -drive if=none,id=d0,file=disk.img,format=raw \
    -device virtio-blk-pci,drive=d0 \
    -m 256 -smp 2 -vga std -serial stdio
```

### 4. Q35 + NVMe (`/dev/nvme0n1`)
```bash
qemu-system-x86_64 \
    -machine q35 -cdrom tsukasa.iso -boot d \
    -drive if=none,id=d0,file=disk.img,format=raw \
    -device nvme,serial=DEADCAFE,drive=d0 \
    -m 256 -smp 2 -vga std -serial stdio
```

---

## Booting Directly from an Installed Disk (No ISO)

After running `/bin/installer /dev/sda ERASE` (or `/bin/install`) inside the live environment, boot straight into the persistent GPT/`ext4` installation:

### Legacy BIOS (SeaBIOS)
```bash
qemu-system-x86_64 \
    -machine q35 -boot c \
    -drive if=none,id=d0,file=disk.img,format=raw \
    -device ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0 \
    -m 256 -smp 2 -vga std -serial stdio
```

### UEFI (OVMF)
Copy `OVMF_VARS.fd` to a writable scratch copy first:
```bash
cp /usr/share/edk2/ovmf/OVMF_VARS.fd ./OVMF_VARS.fd
qemu-system-x86_64 \
    -machine q35 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
    -drive if=pflash,format=raw,file=./OVMF_VARS.fd \
    -drive if=none,id=d0,file=disk.img,format=raw \
    -device ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0 \
    -m 256 -smp 2 -vga std -serial stdio
```

*(On Debian/Ubuntu, OVMF files typically live under `/usr/share/OVMF/`.)*

---

## Parameter Reference

| Flag | Purpose |
| :--- | :--- |
| `-cdrom tsukasa.iso` | Attaches the hybrid BIOS/UEFI Limine ISO containing `tsukasa_x64.elf` and `initrd.tar.lz4` |
| `-boot d` / `-boot c` | Forces CD-ROM boot (`d`) when an MBR/GPT disk is also attached, or hard-disk boot (`c`) |
| `-m 256` | Allocates 256 MiB RAM |
| `-smp 2` | Exposes 2 CPU cores for LAPIC/IOAPIC/SMP verification |
| `-vga std` | Configures Bochs/VESA 32bpp linear framebuffer (`1280x800x32` default; required by Vanilla) |
| `-serial stdio` | Streams COM1 kernel and daemon diagnostics to your terminal |
| `-no-reboot` | Exits cleanly instead of looping on CPU reset if a fault occurs |

---

## Wayland / Hyprland & Small Screen Tips

Tsukasa currently uses a relative PS/2 mouse at `1280x800x32`. On Wayland compositors (such as Hyprland with Quickshell panels) or laptops with small screens or fractional scaling, QEMU's default GTK backend can clip the viewport or mis-scale relative pointer grabs.

1. **Use the SDL display backend** (recommended on Wayland/Hyprland):
   ```bash
   SDL_VIDEODRIVER=wayland qemu-system-x86_64 ... -display sdl
   ```
   Click inside the VM to capture the pointer, press `Ctrl+Alt+G` to release it, and `Ctrl+Alt+F` to toggle fullscreen.

2. **Or force XWayland GTK with `zoom-to-fit`**:
   ```bash
   GDK_BACKEND=x11 qemu-system-x86_64 ... -display gtk,zoom-to-fit=on
   ```

---

## Headless, Kernel Selftests & GDB Debugging

### Headless Serial-Only Run
```bash
timeout 120 qemu-system-x86_64 -cdrom tsukasa.iso -hda disk.img -m 256 -smp 2 \
    -vga std -display none -no-reboot -serial stdio
```

### Automated Boot + Kernel Selftest Run
```bash
bash scripts/ci/boot-test.sh tsukasa.iso boot.log
```
This passes `-fw_cfg name=opt/org.tsukasa.selftest,string=1` (and `opt/org.tsukasa.test,string=1` via `make check`) and checks that all boot milestones and selftests pass with zero panics or faults.

### Remote GDB Attach
```bash
qemu-system-x86_64 -cdrom tsukasa.iso -s -S -no-reboot -serial stdio
```
Then in another terminal:
```bash
gdb tsukasa_x64.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main_x64
(gdb) continue
```
