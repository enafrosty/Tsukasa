# Booting Tsukasa with QEMU

Tsukasa boots via the Limine bootloader on x86_64.

## Install QEMU (Linux / Fedora)

```bash
sudo dnf install -y qemu-system-x86 qemu-img
```

Or install the full toolchain via `./setup_fedora.sh`.

## Install QEMU (Debian / Ubuntu)

```bash
sudo apt-get install -y qemu-system-x86 qemu-utils
```

## Install QEMU (Windows)

- **Chocolatey:** `choco install qemu`
- **Scoop:** `scoop install qemu`
- Or download from: https://www.qemu.org/download/#windows

## Boot the ISO

From the project directory (where `tsukasa.iso` is):

```bash
./run_qemu.sh
```

Or run QEMU directly (`bash` or `powershell`):

```bash
qemu-system-x86_64 -cdrom tsukasa.iso -hda disk.img -m 256 -smp 2 -vga std -serial stdio
```

- `-m 256` = 256 MB RAM.
- `-smp 2` = 2 CPU cores.
- `-vga std` = Standard VGA framebuffer.
- `-serial stdio` = Redirect COM1 serial output to terminal.

## Optional: Headless / No GUI (Serial Only)

```powershell
qemu-system-x86_64 -cdrom tsukasa.iso -hda disk.img -m 256 -smp 2 -vga std -display none -serial stdio
```

(Output goes to the terminal; exit with Ctrl+C or kill the process.)
