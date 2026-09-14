# Run Tsukasa OS in QEMU from Windows PowerShell
param(
    [int]$Memory = 256,
    [int]$Smp = 2,
    [switch]$Debug
)

$ErrorActionPreference = "Stop"

$IsoImage = "tsukasa.iso"
$DiskImage = "disk.img"

if (-not (Test-Path $IsoImage)) {
    Write-Host "[!] $IsoImage not found. Please build the ISO first (e.g., via WSL: make initrd && make ARCH=x86_64 iso)." -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $DiskImage)) {
    Write-Host "[*] Creating 64MB sparse virtual hard disk: $DiskImage..." -ForegroundColor Cyan
    qemu-img create -f raw $DiskImage 64M
}

$QemuArgs = @(
    "-cdrom", $IsoImage,
    "-hda", $DiskImage,
    "-boot", "d",
    "-m", $Memory,
    "-smp", $Smp,
    "-vga", "std",
    "-serial", "stdio",
    "-netdev", "user,id=u1",
    "-device", "e1000,netdev=u1"
)

if ($Debug) {
    Write-Host "[*] GDB debug mode enabled: pausing execution at startup on port 1234..." -ForegroundColor Magenta
    $QemuArgs += @("-s", "-S")
}

Write-Host "[*] Launching Tsukasa in QEMU ($Smp cores, ${Memory}MB RAM)..." -ForegroundColor Green
& qemu-system-x86_64 @QemuArgs

