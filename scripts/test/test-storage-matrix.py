#!/usr/bin/env python3
#
# Project Tsukasa - Automated Storage Controller Matrix Verification
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

"""Verify AHCI, VirtIO-blk, and NVMe QEMU boot, serial discovery, R/W, and host fsck."""

import argparse
import os
import re
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


BACKEND_DISCOVERY_REGEX = {
    "ide": r"(?i)(\[ata\]|\[blockdev\] registered hd)",
    "ahci": r"(?i)(\[ahci\]|ahci.*(port|link|drive)|\[blockdev\] registered (sd|ahci))",
    "virtio": r"(?i)(\[virtio[-_]?blk\]|virtio.*blk|\[blockdev\] registered vd)",
    "nvme": r"(?i)(\[nvme\]|nvme.*(ns|queue|ready)|\[blockdev\] registered nvme)",
}

DEFAULT_BACKENDS = ("ahci", "virtio", "nvme")


def create_raw_disk(disk_path, size_spec):
    qemu_img = find_tool("qemu-img")
    if os.path.exists(disk_path):
        os.remove(disk_path)
    subprocess.check_call(
        [qemu_img, "create", "-f", "raw", disk_path, size_spec],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def _parse_xy(spec):
    parts = [p.strip() for p in spec.split(",")]
    if len(parts) != 2:
        raise ValueError(f"Expected 'x,y' coordinate, got {spec!r}")
    return int(parts[0]), int(parts[1])


def test_backend(args, machine, backend):
    run_dir = os.path.join(args.out_dir, f"{machine}_{backend}")
    os.makedirs(run_dir, exist_ok=True)
    disk_path = os.path.join(run_dir, f"{backend}_disk.img")
    create_raw_disk(disk_path, args.disk_size)

    mode = "probe" if args.probe_only else args.mode
    fw_cfg = list(args.fw_cfg)

    harness = QemuHarness(
        out_dir=run_dir,
        iso=args.iso,
        extra_disks=[disk_path],
        storage=backend,
        machine=machine,
        firmware=args.firmware,
        smp=args.smp,
        mem=args.mem,
        qmp_port=0,
        fw_cfg=fw_cfg,
    )

    with harness as vm:
        vm.wait_for_serial(args.boot_serial, timeout=args.timeout)
        serial_log = vm.get_serial_log()

        if "[pci] discovered devices=" not in serial_log:
            raise QemuHarnessError(
                f"[{machine}/{backend}] missing '[pci] discovered devices=' in serial log"
            )

        drv_pat = BACKEND_DISCOVERY_REGEX[backend]
        driver_found = bool(re.search(drv_pat, serial_log))
        if mode in ("probe", "rw", "full") and args.require_driver and not driver_found:
            raise QemuHarnessError(
                f"[{machine}/{backend}] storage driver discovery pattern not found: {drv_pat}"
            )

        if mode in ("rw", "full"):
            if args.boot_wait > 0:
                time.sleep(args.boot_wait)
            if args.terminal_click:
                cx, cy = _parse_xy(args.terminal_click)
                vm.click(cx, cy, wait_after=0.3)

            offset = vm.serial_offset()
            guest_cmd = args.guest_cmd
            if guest_cmd:
                if not guest_cmd.endswith("\n"):
                    guest_cmd += "\n"
                vm.send_text(guest_cmd)
                if args.rw_wait_serial:
                    vm.wait_for_serial(
                        args.rw_wait_serial,
                        timeout=args.timeout,
                        from_offset=offset,
                    )
            if args.poweroff_cmd:
                po_cmd = args.poweroff_cmd
                if not po_cmd.endswith("\n"):
                    po_cmd += "\n"
                vm.send_text(po_cmd)
                rc = vm.wait_for_exit(timeout=args.timeout)
                if rc != 0:
                    raise QemuHarnessError(
                        f"[{machine}/{backend}] QEMU exited with status {rc} on poweroff"
                    )

        vm.assert_no_crashes()

    fsck_summary = "skipped"
    if mode == "full" or args.fsck:
        fs_results = verify_disk_filesystems(
            disk_path,
            out_dir=run_dir,
            require_partitions=args.require_partitions,
        )
        if fs_results:
            fsck_summary = ", ".join(
                f"p{part.index}:{fstype}={status}" for part, fstype, status in fs_results
            )
        else:
            fsck_summary = "no-partitions"

    print(
        f"[storage] {machine}/{backend}: PASS "
        f"(mode={mode}, driver_discovered={'yes' if driver_found else 'no'}, fsck={fsck_summary})"
    )
    return {
        "machine": machine,
        "backend": backend,
        "driver_discovered": driver_found,
        "fsck": fsck_summary,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Project Tsukasa storage controller matrix verification (AHCI, VirtIO-blk, NVMe)"
    )
    parser.add_argument("--iso", default="tsukasa.iso", help="Path to bootable ISO")
    parser.add_argument("--out-dir", default="tmp-storage-matrix", help="Output directory")
    parser.add_argument("--disk-size", default="64M", help="Raw test disk size (e.g. 64M, 256M)")
    parser.add_argument(
        "--backends",
        default=",".join(DEFAULT_BACKENDS),
        help="Comma-separated backends to test (ide,ahci,virtio,nvme)",
    )
    parser.add_argument(
        "--machine",
        choices=("pc", "q35", "both"),
        default="pc",
        help="QEMU machine type(s) to test",
    )
    parser.add_argument("--firmware", choices=("bios", "uefi"), default="bios", help="Firmware mode")
    parser.add_argument("--smp", type=int, default=2, help="Number of vCPUs")
    parser.add_argument("--mem", default="256", help="Guest RAM in MiB")
    parser.add_argument(
        "--mode",
        choices=("boot", "probe", "rw", "full"),
        default="probe",
        help="Test depth: boot, probe, rw, or full (rw + host fsck)",
    )
    parser.add_argument(
        "--probe-only",
        action="store_true",
        help="Alias for --mode probe (serial discovery check without R/W or fsck)",
    )
    parser.add_argument(
        "--require-driver",
        action="store_true",
        help="Fail if backend-specific storage driver discovery log is absent",
    )
    parser.add_argument(
        "--boot-serial",
        default="[init] PID 1 active",
        help="Serial marker confirming successful boot",
    )
    parser.add_argument("--boot-wait", type=float, default=6.0, help="Settle delay before R/W commands")
    parser.add_argument("--timeout", type=float, default=60.0, help="Per-stage timeout in seconds")
    parser.add_argument("--fw-cfg", action="append", default=[], help="Extra -fw_cfg spec")
    parser.add_argument("--terminal-click", default="236,120", help="Terminal x,y to focus before typing")
    parser.add_argument("--guest-cmd", default=None, help="Command to run inside guest during rw/full mode")
    parser.add_argument("--rw-wait-serial", default=None, help="Serial marker expected after --guest-cmd")
    parser.add_argument("--poweroff-cmd", default="/bin/poweroff", help="Guest command to cleanly power off")
    parser.add_argument("--fsck", action="store_true", help="Force host e2fsck/fsck.fat pass")
    parser.add_argument(
        "--require-partitions",
        action="store_true",
        help="Fail if no MBR/GPT partitions are present during fsck",
    )
    args = parser.parse_args()

    backends = [b.strip().lower() for b in args.backends.split(",") if b.strip()]
    for b in backends:
        if b not in BACKEND_DISCOVERY_REGEX:
            parser.error(f"Unknown storage backend: {b!r}")

    machines = ["pc", "q35"] if args.machine == "both" else [args.machine]

    os.makedirs(args.out_dir, exist_ok=True)
    summary = []
    try:
        for machine in machines:
            for backend in backends:
                summary.append(test_backend(args, machine, backend))
    except Exception as exc:
        print(f"[storage] FAILED: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"[storage] MATRIX COMPLETE ({len(summary)} runs passed)")


if __name__ == "__main__":
    main()
