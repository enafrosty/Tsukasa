# Booting Tsukasa with QEMU

Tsukasa boots via the Limine bootloader on x86_64.

## Install QEMU (Windows)

- **Chocolatey:** `choco install qemu`
- **Scoop:** `scoop install qemu`
- Or download from: https://www.qemu.org/download/#windows

## Boot the ISO

From the project directory (where `tsukasa.iso` is):

```powershell
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
