#!/usr/bin/env python3
#
# Project Tsukasa - Automated QEMU Visual & Remote-Desktop Verification Harness
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

"""Boot Project Tsukasa in QEMU with QMP (and optional VNC) to inspect UI visuals."""

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
    check_no_pure_black_bars,
    find_tool,
    non_color_ratio,
    region_diff_ratio,
)


def run_visual_test(args):
    os.makedirs(args.out_dir, exist_ok=True)
    disk = os.path.join(args.out_dir, "visual_disk.img")
    qemu_img = find_tool("qemu-img")

    if not os.path.exists(disk):
        subprocess.check_call(
            [qemu_img, "create", "-f", "raw", disk, "64M"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    harness = QemuHarness(
        out_dir=args.out_dir,
        iso=args.iso,
        extra_disks=[disk],
        storage="ide",
        machine="pc",
        firmware="bios",
        smp=2,
        mem="256",
        qmp_port=args.qmp_port,
        vnc=args.vnc,
    )

    with harness as vm:
        print(f"[visual] waiting {args.boot_wait}s for desktop initialization...")
        time.sleep(args.boot_wait)

        if args.scenario == "interactive":
            print(
                f"[visual] interactive mode active (QMP 127.0.0.1:{vm.qmp_port}); "
                "Ctrl+C to stop"
            )
            while vm.proc.poll() is None:
                time.sleep(0.5)
            return 0

        w, h, raw_boot = vm.screendump("01_boot")
        assert (w, h) == (1280, 800), f"Unexpected framebuffer size {w}x{h}"

        # Verify dock bottom corners blend over wallpaper rather than opaque bg_base (0x2E3440).
        dock_x0 = (1280 - 256) // 2
        dock_y0 = 800 - 52 - 12
        corner_idx = (dock_y0 * w + dock_x0) * 3
        c_rgb = (raw_boot[corner_idx], raw_boot[corner_idx + 1], raw_boot[corner_idx + 2])
        assert c_rgb != (0x2E, 0x34, 0x40), f"Opaque bg_base box detected at dock corner: {c_rgb}"

        # Confirm dock stayed at 6 pinned items (256px) with all startup app titles resolved.
        serial_boot = vm.get_serial_log()
        for expected_title in ("title='Terminal'", "title='Task Manager'", "title='File Manager'"):
            assert expected_title in serial_boot, f"Missing startup window {expected_title} in serial log"
        assert "title='Dock' (310x52)" not in serial_boot, "Dock expanded unexpectedly to 310px (duplicate item)"

        if args.scenario == "boot":
            vm.assert_no_crashes()
            print("[visual] BOOT VISUAL CHECKS PASSED")
            return 0

        # Drag Terminal from titlebar at (236, 46) down-right 300x192 in 12 rapid moves.
        vm.drag(236, 46, steps=12, dx=25, dy=16, delay=0.04)
        _, _, raw_drag = vm.screendump("02_drag")
        ok_black, longest_run = check_no_pure_black_bars(w, h, raw_drag, 40, 260, 1240, 720, min_run=40)
        assert ok_black, f"Detected solid-black drag trail artifact ({longest_run}px run)"

        # Open System Menu via top-left Tsukasa emblem.
        vm.click(16, 14, wait_after=0.6)
        vm.screendump("03_sysmenu")

        # Open Quick Launcher via Alt+Space.
        vm.send_key("esc", delay=0.3)
        vm.send_key("alt-spc", delay=0.6)
        vm.screendump("04_launcher")

        # Close Launcher and inspect Task Manager Processes tab.
        vm.send_key("esc", delay=0.3)
        vm.click(740, 759, wait_after=0.6)  # Raise Task Manager via dock slot 5
        vm.click(946, 103, wait_after=0.6)  # Click Processes tab (648 + 596/2, 58 + 45)
        vm.screendump("05_taskmgr_procs")

        # Raise File Manager via dock slot 1.
        vm.click(580, 759, wait_after=0.6)
        _, _, raw_fm = vm.screendump("06_filemgr")

        # Click File Manager Maximize button at (599, 377), verify SHM resize reflow & work area.
        vm.click(599, 377, wait_after=1.0)
        _, _, raw_fm_max = vm.screendump("06b_filemgr_max")
        max_non_bg = non_color_ratio(w, raw_fm_max, 800, 54, 1270, 160, (0x2E, 0x34, 0x40))
        title_btn_non_bg = non_color_ratio(w, raw_fm_max, 1246, 34, 1274, 46, (0x2E, 0x34, 0x40))
        max_right_diff = region_diff_ratio(w, raw_fm, raw_fm_max, 700, 120, 1260, 710)
        print(
            f"[visual] FileMgr maximize non_bg={max_non_bg:.3f}, "
            f"title_btns={title_btn_non_bg:.3f}, right_diff={max_right_diff:.3f}"
        )
        assert max_non_bg > 0.15, f"Maximized File Manager did not reflow expanded UI (non_bg={max_non_bg:.3f})"
        assert title_btn_non_bg > 0.50, f"Maximized titlebar buttons hidden behind statusbar ({title_btn_non_bg:.3f})"
        assert max_right_diff > 0.15, f"Maximized right side did not fill work area ({max_right_diff:.3f})"

        # Click Maximize again at (1252, 40) to restore File Manager back to floating bounds.
        vm.click(1252, 40, wait_after=1.0)
        _, _, raw_fm_restored = vm.screendump("06c_filemgr_restore")
        restore_diff = region_diff_ratio(w, raw_fm_max, raw_fm_restored, 700, 540, 1260, 710)
        print(f"[visual] FileMgr restore right_diff={restore_diff:.3f}")
        assert restore_diff > 0.15, f"Restored File Manager did not unmaximize ({restore_diff:.3f})"

        # Launch Notepad via dock slot 2 and verify typed text visually.
        vm.click(620, 759, wait_after=1.6)
        _, _, raw_note_pre = vm.screendump("_note_pre", save_png=False)
        vm.send_text("Hello Tsukasa")
        time.sleep(0.4)
        _, _, raw_note_post = vm.screendump("07_notepad")
        note_diff = region_diff_ratio(w, raw_note_pre, raw_note_post, 270, 128, 540, 175)
        print(f"[visual] Notepad typing pixel delta in [270..540, 128..175]: {note_diff:.4f}")
        assert note_diff > 0.002, f"Notepad did not render typed text (diff={note_diff:.4f})"

        # Launch Calculator via dock slot 3 and verify 7*6 -> 42 visually.
        vm.click(660, 759, wait_after=1.6)
        _, _, raw_calc_pre = vm.screendump("_calc_pre", save_png=False)
        vm.send_text("7*6\n")
        time.sleep(0.4)
        _, _, raw_pre_settings = vm.screendump("08_calc")
        calc_diff = region_diff_ratio(w, raw_calc_pre, raw_pre_settings, 1020, 150, 1165, 218)
        print(f"[visual] Calculator result pixel delta in [1020..1165, 150..218]: {calc_diff:.4f}")
        assert calc_diff > 0.003, f"Calculator did not update display on keyboard input (diff={calc_diff:.4f})"

        # Launch Settings via dock slot 4 and confirm window actually opens & paints.
        vm.click(700, 759, wait_after=1.8)
        _, _, raw_settings = vm.screendump("09_settings")
        diff = region_diff_ratio(w, raw_pre_settings, raw_settings, 340, 110, 900, 550)
        print(f"[visual] Settings launch pixel delta in [340..900, 110..550]: {diff:.3f}")
        assert diff > 0.08, f"Settings window did not appear cleanly (diff={diff:.3f})"

        vm.assert_no_crashes()
        print("[visual] ALL VISUAL CHECKS PASSED")
        return 0


def main():
    parser = argparse.ArgumentParser(description="Tsukasa QEMU QMP visual & remote desktop test harness")
    parser.add_argument("--iso", default="tsukasa.iso", help="Path to tsukasa.iso")
    parser.add_argument("--out-dir", default="tmp-visual-test", help="Output directory for PNG screenshots & serial.log")
    parser.add_argument("--qmp-port", type=int, default=0, help="TCP port for QEMU QMP control (0 = ephemeral free port)")
    parser.add_argument("--vnc", default=None, help="Optional QEMU VNC display (e.g. :1 -> 127.0.0.1:5901)")
    parser.add_argument("--boot-wait", type=float, default=12.0, help="Seconds to wait after boot before testing")
    parser.add_argument("--scenario", choices=("boot", "full", "interactive"), default="full")
    args = parser.parse_args()
    sys.exit(run_visual_test(args))


if __name__ == "__main__":
    main()
