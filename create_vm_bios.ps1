# Create a new VirtualBox VM with BIOS (not UEFI) for MyOS.
# Run from PowerShell. Adjust VM name and paths if needed.

$VBox = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
$VMName = "MyOS-BIOS"
$IsoPath = (Resolve-Path "myos.iso").Path
$VMFolder = "C:\Users\frost145\VirtualBox VMs\$VMName"

if (-not (Test-Path $VBox)) {
    Write-Error "VBoxManage not found at $VBox"
    exit 1
}
if (-not (Test-Path $IsoPath)) {
    Write-Error "myos.iso not found. Run 'make iso' first."
    exit 1
}

# Create VM (Other / Other)
& $VBox createvm --name $VMName --ostype Other --register --basefolder "C:\Users\frost145\VirtualBox VMs"
& $VBox modifyvm $VMName --firmware bios
& $VBox modifyvm $VMName --memory 64 --vram 9
& $VBox modifyvm $VMName --nic1 nat
& $VBox storagectl $VMName --name "IDE" --add ide --controller PIIX4 --bootable on
& $VBox storageattach $VMName --storagectl "IDE" --port 1 --device 0 --type dvddrive --medium $IsoPath

Write-Host "VM '$VMName' created with BIOS firmware. Start it from VirtualBox with myos.iso attached."
Write-Host "In VirtualBox: select $VMName -> Start."
