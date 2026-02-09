# Booting MyOS with QEMU (legacy BIOS by default)

If VirtualBox keeps using UEFI and you cannot change it, use **QEMU** instead. QEMU typically uses **SeaBIOS** (legacy BIOS) by default, so your multiboot kernel should boot without changing any firmware setting.

## Install QEMU (Windows)

- **Chocolatey:** `choco install qemu`
- **Scoop:** `scoop install qemu`
- Or download from: https://www.qemu.org/download/#windows

## Boot the ISO

From the project directory (where `myos.iso` is):

```powershell
qemu-system-i386 -cdrom myos.iso -m 64
```

- `-m 64` = 64 MB RAM (matches your VM).
- QEMU will use legacy BIOS and boot from the CD; you should see the GRUB menu and then your kernel.

## Optional: no GUI (serial only)

```powershell
qemu-system-i386 -cdrom myos.iso -m 64 -nographic
```

(Output goes to the terminal; exit with Ctrl+A then X.)
