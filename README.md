# Project Tsukasa - The Operating System

Tsukasa is a modern, 32-bit bare-metal desktop operating system built entirely in C and Assembly. It features a custom window manager with a translucent glass aesthetic, a functional FAT12 ramdisk, and a suite of built-in desktop utilities.
**Note:** This project is built from scratch without the standard C library (`libc`) or standard OS headers.

![alt text](https://w0.peakpx.com/wallpaper/235/811/HD-wallpaper-anime-tonikawa-over-the-moon-for-you-tsukasa-yuzaki.jpg)


## Development Status
This project is currently in early development. Major components are being implemented. 


## Features

- **Modern Compositing Window Manager:**
  - True Z-ordering, focus management, drag-and-drop, and window closing.
  - Sleek, semi-transparent "dark glass" UI with drop shadows, rounded corners, and macOS-style traffic light window buttons.
  - Global accent colors configurable at runtime.
- **Virtual File System (VFS):**
  - **FAT12 (`/`)**: A fully functional FAT12 driver running off a Multiboot ramdisk module (`initrd.img`). Supports directory enumeration and file reading.
  - **MemFS (`/tmp/`)**: An in-memory writable filesystem for storing application data during a session.
- **Desktop Environment & Applications:**
  - **Desktop Shell**: A functional taskbar with active window pills, start menu, and desktop shortcut icons.
  - **Settings**: System personalization app to dynamically change the global window accent color and set desktop `.bmp` wallpapers.
  - **File Manager**: A robust graphical file browser that explores the FAT12 disk, featuring icon grids and double-click execution.
  - **Notepad**: A text editor capable of reading from the FAT12 disk and saving buffers to the `memfs` tmp directory.
  - **Calculator & Terminal**: Functional utility applications.
- **BMP Wallpaper Engine**: Natively parses 24-bit and 32-bit uncompressed BMP files and nearest-neighbor scales them into the framebuffer VRAM natively.

---


## Build Dependencies (WSL / Linux)

The OS uses a standard i686 GCC cross-compiler toolchain and is designed to be built in Linux or WSL (Windows Subsystem for Linux). 

To install the required tools rapidly on Ubuntu/Debian, run the included setup script:
```bash
chmod +x setup_wsl.sh
./setup_wsl.sh
```

Dependencies include:
- `build-essential` (gcc, make)
- `nasm` (for boot and interrupt assembly)
- `grub-common`, `grub-pc-bin`, `xorriso` (to pack the final bootable ISO)
- `dosfstools` (for `mformat` and `mcopy` to pack the FAT12 initrd image)

---

## Building the OS

The build process is completely automated via the `Makefile`. 

### 1. Build the FAT12 Ramdisk (`initrd.img`)
The OS relies on an initial ramdisk for its file system. 
Place any files you want the OS to read (like a `wallpaper.bmp` or text files) inside the `initrd_files/` directory, then run:

```bash
make initrd
```
This generates a 1.44 MB `initrd.img` formatted as FAT12 containing your files.

### 2. Build the Bootable ISO (`tsukasa.iso`)
Once the initrd is ready, compile the kernel and package everything into a GRUB2 bootable ISO:

```bash
make iso
```

---

## Running in QEMU

I strongly recommend **QEMU** for testing, as VirtualBox often defaults to UEFI which is incompatible with this Multiboot v1 legacy-BIOS kernel.

To run the ISO in QEMU with 64MB of RAM (adjust display parameters as needed for your host OS):

```powershell
qemu-system-i386 -cdrom tsukasa.iso -m 64 -vga std -display sdl
```

---

## Kernel Architecture Notes

### Memory & Paging
- The kernel boots at the 1 MiB mark.
- Paging is enabled at boot, identity mapping the first **16 MiB** of physical RAM. You can dynamically allocate up to 16 MiB of contiguous physical memory without worrying about virtual memory page fault mapping.
- The `kmalloc` memory allocator automatically requests `4KB` pages from the Physical Memory Manager (`PMM`) when the 16KB heap is exhausted.

### Input & Events
- `ps2mouse.c` tracks Mouse packets over IRQ 12. 
- Window redraws are exclusively event-driven. A global `event_queue` handles routing `EVENT_MOUSE` and `EVENT_KEY` packets to `wm_handle_mouse` and the active focused Window structure.

### Context Switching
- The system currently operates on a single core in a monolithic loop. If adding preemptive multitasking or a scheduler later, please ensure that `cli` and `sti` are heavily audited around state mutations, as there are currently no spinlocks protecting global state.
