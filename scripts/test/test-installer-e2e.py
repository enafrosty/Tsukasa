#!/usr/bin/env python3
#
# Project Tsukasa - Automated Installer & BIOS/UEFI Reboot Persistence E2E Test
#
# Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
#
# Project Tsukasa was created and is maintained by frosty (@enafrosty).
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version. See the top-level LICENSE file.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#

"""End-to-end installer, SeaBIOS/UEFI direct disk boot, persistence, and host fsck test."""

import argparse
import os
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from qemu_harness import (  # noqa: E402
    QemuHarness,
    QemuHarnessError,
    find_tool,
    verify_disk_filesystems,
)


def _parse_xy(spec):
    parts = [p.strip() for p in spec.split(",")]
    if len(parts) != 2:
        raise ValueError(f"Expected 'x,y' coordinate, got {spec!r}")
    return int(parts[0]), int(parts[1])


def _send_cmd_and_wait(vm, cmd_text, wait_serial=None, timeout=60.0, regex=False):
    offset = vm.serial_offset()
    line = cmd_text if cmd_text.endswith("\n") else cmd_text + "\n"
    vm.send_text(line)
    if wait_serial:
        vm.wait_for_serial(wait_serial, timeout=timeout, regex=regex, from_offset=offset)


def _poweroff_and_check(vm, poweroff_cmd, timeout=60.0):
    line = poweroff_cmd if poweroff_cmd.endswith("\n") else poweroff_cmd + "\n"
    vm.send_text(line)
    rc = vm.wait_for_exit(timeout=timeout)
    if rc != 0:
        raise QemuHarnessError(f"QEMU exited with status {rc} during poweroff")
    vm.assert_no_crashes()


def _create_blank_disk(disk_path, size_spec):
    qemu_img = find_tool("qemu-img")
    if os.path.exists(disk_path):
        os.remove(disk_path)
    subprocess.check_call(
        [qemu_img, "create", "-f", "raw", disk_path, size_spec],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def run_stage1_install(args, disk_path):
    stage_dir = os.path.join(args.out_dir, "stage1_install")
    print(f"[e2e] Stage 1: live ISO install -> {disk_path} ({args.disk_size})")
    harness = QemuHarness(
        out_dir=stage_dir,
        iso=args.iso,
        extra_disks=[disk_path],
        storage=args.storage,
        machine=args.machine,
        firmware="bios",
        smp=args.smp,
        mem=args.mem,
        qmp_port=0,
    )
    with harness as vm:
        vm.wait_for_serial(args.ready_serial, timeout=args.timeout)
        if args.boot_wait > 0:
            time.sleep(args.boot_wait)
        cx, cy = _parse_xy(args.terminal_click)
        vm.click(cx, cy, wait_after=0.3)
        _send_cmd_and_wait(
            vm,
            args.install_cmd,
            wait_serial=args.install_wait_serial,
            timeout=args.timeout,
            regex=True,
        )
        vm.screendump("stage1_installed")
        _poweroff_and_check(vm, args.poweroff_cmd, timeout=args.timeout)
    print("[e2e] Stage 1 PASS")


def run_stage2_create_marker(args, disk_path, create_cmd, verify_cmd):
    stage_dir = os.path.join(args.out_dir, "stage2_bios_write")
    print("[e2e] Stage 2: direct disk boot (SeaBIOS, no -cdrom) -> create persistence marker")
    harness = QemuHarness(
        out_dir=stage_dir,
        iso=None,
        boot_disk=disk_path,
        storage=args.storage,
        machine=args.machine,
        firmware="bios",
        smp=args.smp,
        mem=args.mem,
        qmp_port=0,
    )
    with harness as vm:
        vm.wait_for_serial(args.ready_serial, timeout=args.timeout)
        if args.boot_wait > 0:
            time.sleep(args.boot_wait)
        cx, cy = _parse_xy(args.terminal_click)
        vm.click(cx, cy, wait_after=0.3)
        _send_cmd_and_wait(vm, create_cmd, wait_serial=None, timeout=args.timeout)
        time.sleep(1.0)
        _send_cmd_and_wait(
            vm,
            verify_cmd,
            wait_serial=args.persist_token,
            timeout=args.timeout,
        )
        vm.screendump("stage2_bios_marker")
        _poweroff_and_check(vm, args.poweroff_cmd, timeout=args.timeout)
    print("[e2e] Stage 2 PASS")


def run_verify_boot(args, disk_path, firmware, stage_name, verify_cmd):
    stage_dir = os.path.join(args.out_dir, stage_name)
    print(f"[e2e] Verify direct disk boot ({firmware}, no -cdrom) -> {args.persist_path}")
    harness = QemuHarness(
        out_dir=stage_dir,
        iso=None,
        boot_disk=disk_path,
        storage=args.storage,
        machine=args.machine,
        firmware=firmware,
        smp=args.smp,
        mem=args.mem,
        qmp_port=0,
    )
    with harness as vm:
        vm.wait_for_serial(args.ready_serial, timeout=args.timeout)
        if args.boot_wait > 0:
            time.sleep(args.boot_wait)
        cx, cy = _parse_xy(args.terminal_click)
        vm.click(cx, cy, wait_after=0.3)
        _send_cmd_and_wait(
            vm,
            verify_cmd,
            wait_serial=args.persist_token,
            timeout=args.timeout,
        )
        vm.screendump(f"{stage_name}_verify")
        _poweroff_and_check(vm, args.poweroff_cmd, timeout=args.timeout)
    print(f"[e2e] {stage_name} ({firmware}) PASS")


def main():
    parser = argparse.ArgumentParser(
        description="Project Tsukasa automated E2E installer and BIOS/UEFI reboot persistence test"
    )
    parser.add_argument("--iso", default="tsukasa.iso", help="Path to live installation ISO")
    parser.add_argument("--out-dir", default="tmp-installer-e2e", help="Output directory for logs/PNGs/disk")
    parser.add_argument("--disk", default=None, help="Path to target disk image (default: <out-dir>/disk.img)")
    parser.add_argument("--disk-size", default="256M", help="Blank target disk size (default: 256M)")
    parser.add_argument(
        "--storage",
        choices=("ide", "ahci", "virtio", "nvme"),
        default="ide",
        help="Storage controller for target disk",
    )
    parser.add_argument("--machine", choices=("pc", "q35"), default="pc", help="QEMU machine type")
    parser.add_argument("--smp", type=int, default=2, help="Number of vCPUs")
    parser.add_argument("--mem", default="256", help="Guest RAM in MiB")
    parser.add_argument("--boot-wait", type=float, default=4.0, help="Extra settle delay after ready milestone")
    parser.add_argument("--timeout", type=float, default=120.0, help="Timeout in seconds per stage")
    parser.add_argument(
        "--ready-serial",
        default="[vanilla] Listening for client connections.",
        help="Serial milestone indicating desktop/terminal readiness",
    )
    parser.add_argument(
        "--terminal-click",
        default="236,120",
        help="Desktop x,y coordinate to focus Terminal before typing",
    )
    parser.add_argument(
        "--install-cmd",
        default=None,
        help="Automated installer command typed via QMP send_text (default: /bin/installer --auto /dev/<disk> --yes --no-reboot)",
    )
    parser.add_argument(
        "--install-wait-serial",
        default=r"(?i)(INSTALL COMPLETE|installation complete)",
        help="Serial regex confirming installation finished",
    )
    parser.add_argument("--poweroff-cmd", default="/bin/poweroff", help="Guest poweroff command")
    parser.add_argument("--persist-path", default="/root/persist.txt", help="Persistence marker file path")
    parser.add_argument("--persist-token", default="TSUKASA_E2E_PERSIST_OK", help="Persistence marker content")
    parser.add_argument("--create-marker-cmd", default=None, help="Override command to create marker file")
    parser.add_argument("--verify-marker-cmd", default=None, help="Override command to print marker to serial")
    parser.add_argument("--skip-uefi", action="store_true", help="Skip UEFI OVMF direct-disk verification stage")
    parser.add_argument("--no-fsck", action="store_true", help="Skip host e2fsck/fsck.fat partition checks")
    args = parser.parse_args()

    if not args.install_cmd:
        dev_by_storage = {
            "ide": "/dev/hda",
            "ahci": "/dev/sda",
            "virtio": "/dev/vda",
            "nvme": "/dev/nvme0n1",
        }
        args.install_cmd = f"/bin/installer --auto {dev_by_storage[args.storage]} --yes --no-reboot"

    os.makedirs(args.out_dir, exist_ok=True)
    disk_path = args.disk or os.path.join(args.out_dir, "disk.img")
    create_cmd = args.create_marker_cmd or f"echo {args.persist_token} > {args.persist_path}"
    verify_cmd = args.verify_marker_cmd or f"cat {args.persist_path} > /dev/tty0"

    try:
        _create_blank_disk(disk_path, args.disk_size)
        run_stage1_install(args, disk_path)
        run_stage2_create_marker(args, disk_path, create_cmd, verify_cmd)
        run_verify_boot(args, disk_path, "bios", "stage3_bios_reboot", verify_cmd)
        if not args.skip_uefi:
            run_verify_boot(args, disk_path, "uefi", "stage4_uefi_reboot", verify_cmd)
        if not args.no_fsck:
            print("[e2e] Stage 5: host partition & filesystem check (e2fsck -fn / fsck.fat -n)")
            fs_results = verify_disk_filesystems(
                disk_path,
                out_dir=args.out_dir,
                require_partitions=True,
            )
            for part, fstype, status in fs_results:
                print(
                    f"[e2e]   partition {part.index} ({part.scheme} {part.part_type}, "
                    f"lba={part.lba_start}, sectors={part.sector_count}, fs={fstype}): {status}"
                )
        print("[e2e] ALL INSTALLER & REBOOT PERSISTENCE CHECKS PASSED")
    except Exception as exc:
        print(f"[e2e] FAILED: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
