# Booting MyOS in VirtualBox: Use Legacy BIOS

## What the log showed

From `VBox.log`:

- **Firmware type: UEFI** (line 9) – the VM is set to use UEFI.
- **Booting from CD-ROM...** (line 1265) – the CD (myos.iso) is used.
- **VINF_EM_TRIPLE_FAULT** (lines 1266–1269) – the guest triple-faulted right after starting to boot from CD.
- **TRAP/08 = 159 times** – Double Fault exceptions; the CPU could not handle a fault (e.g. while entering the kernel).

Your kernel is a **32-bit Multiboot** image built for **legacy BIOS** boot (BIOS → GRUB legacy → kernel at 0x100000). With **UEFI** enabled, the VM uses the EFI boot path; the state GRUB leaves the CPU in when it jumps to your kernel can be wrong for a 32-bit multiboot image (e.g. segment or paging state), which leads to an immediate fault and then double/triple fault. So the ISO and kernel are fine; the mismatch is **UEFI vs legacy**.

## Fix: Switch VM to Legacy BIOS

1. Power off the VM completely (not just shut down the guest).
2. Select the VM **tsukasa** → **Settings**.
3. Open **System** → **Motherboard**.
4. Set **Firmware** (or “Enable EFI”) so that **EFI is disabled** and the VM uses **BIOS** (legacy).
   - In some VirtualBox versions: uncheck **“Enable EFI (special OSes only)”**.
   - In others: choose **“BIOS”** (or “Legacy”) instead of “EFI” in the firmware dropdown.
5. Click **OK** and start the VM again with the same **myos.iso** in the CD drive.

After this, the boot chain will be: **BIOS → boot from CD (El Torito) → GRUB legacy → your kernel**, which matches how your OS and `grub-mkrescue` ISO are built. The triple fault from the UEFI boot path should stop and the kernel should boot.
