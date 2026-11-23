#!/usr/bin/env python3
#
# Project Tsukasa - Automated Headless QEMU Testing & QMP Input Harness
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

"""Reusable headless QEMU runner, QMP/HMP input driver, serial monitor, and CLI."""

import argparse
from dataclasses import dataclass
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib


CRASH_SIGNATURES = (
    "PANIC",
    "panic:",
    "Unhandled exception",
    "Page fault",
    "General protection",
    "Double fault",
    "Triple fault",
    "UBSAN",
    "[ubsan]",
    "stack smashing",
)

OVMF_CODE_CANDIDATES = (
    "/usr/share/edk2/ovmf/OVMF_CODE.fd",
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/edk2/x64/OVMF_CODE.4m.fd",
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
    "/usr/share/qemu/OVMF_CODE.fd",
)

OVMF_VARS_CANDIDATES = (
    "/usr/share/edk2/ovmf/OVMF_VARS.fd",
    "/usr/share/OVMF/OVMF_VARS.fd",
    "/usr/share/edk2/x64/OVMF_VARS.4m.fd",
    "/usr/share/OVMF/OVMF_VARS_4M.fd",
    "/usr/share/qemu/OVMF_VARS.fd",
)

ASCII_KEYMAP = {
    " ": "spc",
    "\n": "ret",
    "\t": "tab",
    "-": "minus",
    "_": "shift-minus",
    "=": "equal",
    "+": "shift-equal",
    "[": "bracket_left",
    "{": "shift-bracket_left",
    "]": "bracket_right",
    "}": "shift-bracket_right",
    ";": "semicolon",
    ":": "shift-semicolon",
    "'": "apostrophe",
    '"': "shift-apostrophe",
    "`": "grave_accent",
    "~": "shift-grave_accent",
    "\\": "backslash",
    "|": "shift-backslash",
    ",": "comma",
    "<": "shift-comma",
    ".": "dot",
    ">": "shift-dot",
    "/": "slash",
    "?": "shift-slash",
    "!": "shift-1",
    "@": "shift-2",
    "#": "shift-3",
    "$": "shift-4",
    "%": "shift-5",
    "^": "shift-6",
    "&": "shift-7",
    "*": "shift-8",
    "(": "shift-9",
    ")": "shift-0",
}

MOUSE_BUTTON_MAP = {
    "left": 1,
    "l": 1,
    "middle": 2,
    "m": 2,
    "right": 4,
    "r": 4,
}


class QemuHarnessError(RuntimeError):
    """Raised when QEMU boot, serial wait, crash check, or QMP operation fails."""


def find_tool(name):
    """Locate a host executable on PATH or standard MSYS2 directories."""
    found = shutil.which(name)
    if found:
        return found
    for prefix in (r"C:\msys64\ucrt64\bin", r"C:\msys64\usr\bin"):
        for suffix in (".exe", ""):
            cand = os.path.join(prefix, name + suffix)
            if os.path.exists(cand):
                return cand
    raise FileNotFoundError(f"Required tool not found on PATH: {name}")


def find_free_port(host="127.0.0.1"):
    """Allocate an ephemeral free TCP port via bind(('', 0))."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, 0))
        return sock.getsockname()[1]


def find_ovmf_paths(ovmf_code=None, ovmf_vars=None):
    """Locate OVMF_CODE.fd and OVMF_VARS.fd for UEFI QEMU launches."""
    code_path = ovmf_code
    if not code_path:
        for cand in OVMF_CODE_CANDIDATES:
            if os.path.isfile(cand):
                code_path = cand
                break
    if not code_path or not os.path.isfile(code_path):
        raise FileNotFoundError(
            "OVMF_CODE.fd not found; checked: " + ", ".join(OVMF_CODE_CANDIDATES)
        )

    vars_path = ovmf_vars
    if not vars_path:
        sibling = os.path.join(os.path.dirname(code_path), "OVMF_VARS.fd")
        candidates = (sibling,) + OVMF_VARS_CANDIDATES
        for cand in candidates:
            if os.path.isfile(cand):
                vars_path = cand
                break
    if not vars_path or not os.path.isfile(vars_path):
        raise FileNotFoundError(
            "OVMF_VARS.fd not found; checked: " + ", ".join(OVMF_VARS_CANDIDATES)
        )
    return code_path, vars_path


def _next_ppm_token(f):
    """Read the next whitespace-delimited header token from a binary PPM file."""
    buf = bytearray()
    while True:
        ch = f.read(1)
        if not ch:
            break
        if ch == b"#":
            f.readline()
            continue
        if ch.isspace():
            if buf:
                break
            continue
        buf.extend(ch)
    return bytes(buf)


def read_ppm(ppm_path):
    """Read a binary P6 PPM image and return (width, height, raw_rgb_bytes)."""
    with open(ppm_path, "rb") as f:
        magic = _next_ppm_token(f)
        if magic != b"P6":
            raise RuntimeError(f"Unsupported PPM format in {ppm_path}: {magic!r}")
        w = int(_next_ppm_token(f))
        h = int(_next_ppm_token(f))
        _maxval = int(_next_ppm_token(f))
        raw = f.read(w * h * 3)
    if len(raw) != w * h * 3:
        raise RuntimeError(f"Truncated PPM data in {ppm_path}")
    return w, h, raw


def write_png(w, h, raw_rgb, png_path):
    """Encode raw 24-bit RGB pixels to a PNG file using stdlib zlib."""
    stride = w * 3
    rows = b"".join(b"\x00" + raw_rgb[y * stride:(y + 1) * stride] for y in range(h))

    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 6))
        + chunk(b"IEND", b"")
    )
    with open(png_path, "wb") as f:
        f.write(png)


def check_no_pure_black_bars(w, h, raw, x0, y0, x1, y1, min_run=40):
    """Verify no horizontal run of pure black pixels reaches min_run length."""
    longest = 0
    for y in range(max(0, y0), min(h, y1)):
        run = 0
        row_off = y * w * 3
        for x in range(max(0, x0), min(w, x1)):
            idx = row_off + x * 3
            if raw[idx] == 0 and raw[idx + 1] == 0 and raw[idx + 2] == 0:
                run += 1
                if run > longest:
                    longest = run
            else:
                run = 0
    return longest < min_run, longest


def region_diff_ratio(w, raw_a, raw_b, x0, y0, x1, y1):
    """Return fraction of pixels that differ between two RGB buffers in [x0,x1) x [y0,y1)."""
    total = 0
    changed = 0
    for y in range(y0, y1):
        row_off = y * w * 3
        for x in range(x0, x1):
            idx = row_off + x * 3
            total += 1
            if (
                raw_a[idx] != raw_b[idx]
                or raw_a[idx + 1] != raw_b[idx + 1]
                or raw_a[idx + 2] != raw_b[idx + 2]
            ):
                changed += 1
    return (changed / total) if total else 0.0


def non_color_ratio(w, raw, x0, y0, x1, y1, rgb):
    """Return fraction of pixels in [x0,x1) x [y0,y1) that do not equal rgb."""
    total = 0
    diff = 0
    tr, tg, tb = rgb
    for y in range(y0, y1):
        row_off = y * w * 3
        for x in range(x0, x1):
            idx = row_off + x * 3
            total += 1
            if raw[idx] != tr or raw[idx + 1] != tg or raw[idx + 2] != tb:
                diff += 1
    return (diff / total) if total else 0.0


def find_crash_signatures(text, extra_signatures=None):
    """Return a list of (signature, line) tuples matched in serial output."""
    sigs = list(CRASH_SIGNATURES)
    if extra_signatures:
        sigs.extend(extra_signatures)
    hits = []
    for line in text.splitlines():
        low = line.lower()
        for sig in sigs:
            if sig.lower() in low:
                hits.append((sig, line.strip()))
                break
    return hits


def assert_no_crashes(text, extra_signatures=None):
    """Raise QemuHarnessError if any kernel panic, exception, or sanitizer hit is present."""
    hits = find_crash_signatures(text, extra_signatures=extra_signatures)
    if hits:
        sig, line = hits[0]
        raise QemuHarnessError(f"Crash signature '{sig}' detected in serial log: {line}")


@dataclass
class PartitionInfo:
    """Partition metadata parsed from MBR or GPT."""

    scheme: str
    index: int
    part_type: str
    lba_start: int
    sector_count: int


def parse_mbr_partitions(sector0):
    """Parse primary MBR partition entries from a 512-byte sector 0."""
    if len(sector0) < 512 or sector0[510:512] != b"\x55\xAA":
        return []
    parts = []
    for idx in range(4):
        entry = sector0[446 + idx * 16:462 + idx * 16]
        ptype = entry[4]
        lba_start, sec_count = struct.unpack("<II", entry[8:16])
        if ptype == 0 or lba_start == 0 or sec_count == 0:
            continue
        parts.append(
            PartitionInfo(
                scheme="mbr",
                index=idx + 1,
                part_type=f"0x{ptype:02x}",
                lba_start=lba_start,
                sector_count=sec_count,
            )
        )
    return parts


def inspect_disk_partitions(disk_path, sector_size=512):
    """Inspect MBR or GPT partitions on a raw disk image."""
    with open(disk_path, "rb") as f:
        mbr_sec = f.read(sector_size)
        gpt_hdr = f.read(sector_size)
        mbr_parts = parse_mbr_partitions(mbr_sec)

        has_protective_mbr = any(p.part_type == "0xee" for p in mbr_parts)
        if len(gpt_hdr) >= 92 and gpt_hdr[:8] == b"EFI PART" and has_protective_mbr:
            part_lba = struct.unpack("<Q", gpt_hdr[72:80])[0]
            num_entries, entry_size = struct.unpack("<II", gpt_hdr[80:88])
            if entry_size >= 128 and num_entries > 0:
                f.seek(part_lba * sector_size)
                table = f.read(num_entries * entry_size)
                gpt_parts = []
                for idx in range(num_entries):
                    ent = table[idx * entry_size:(idx + 1) * entry_size]
                    if len(ent) < 56:
                        break
                    type_guid = ent[0:16]
                    if type_guid == b"\x00" * 16:
                        continue
                    first_lba, last_lba = struct.unpack("<QQ", ent[32:48])
                    if last_lba >= first_lba:
                        gpt_parts.append(
                            PartitionInfo(
                                scheme="gpt",
                                index=idx + 1,
                                part_type=type_guid.hex(),
                                lba_start=first_lba,
                                sector_count=(last_lba - first_lba + 1),
                            )
                        )
                if gpt_parts:
                    return gpt_parts
        return mbr_parts


def detect_fs_type(part_head):
    """Identify ext2/3/4 or FAT filesystem from the first 4096 bytes of a partition."""
    if len(part_head) >= 1024 + 0x3A:
        magic = struct.unpack("<H", part_head[1024 + 0x38:1024 + 0x3A])[0]
        if magic == 0xEF53:
            return "ext2"
    if len(part_head) >= 512 and part_head[510:512] == b"\x55\xAA":
        if part_head[82:87] == b"FAT32" or part_head[54:57] == b"FAT":
            return "fat"
    return "unknown"


def verify_disk_filesystems(disk_path, out_dir=None, require_partitions=True):
    """Extract partitions from raw disk image and run host e2fsck -fn / fsck.fat -n."""
    parts = inspect_disk_partitions(disk_path)
    if not parts:
        if require_partitions:
            raise QemuHarnessError(f"No partitions found on {disk_path}")
        return []

    results = []
    cleanup_dir = None
    work_dir = out_dir
    if work_dir is None:
        cleanup_dir = tempfile.mkdtemp(prefix="tsukasa-fsck-")
        work_dir = cleanup_dir
    else:
        os.makedirs(work_dir, exist_ok=True)

    try:
        for part in parts:
            slice_path = os.path.join(work_dir, f"part_{part.scheme}_{part.index}.img")
            offset = part.lba_start * 512
            total_bytes = part.sector_count * 512
            with open(disk_path, "rb") as src:
                src.seek(offset)
                head = src.read(min(4096, total_bytes))
                fs_type = detect_fs_type(head)
                if fs_type == "unknown":
                    results.append((part, fs_type, "skipped"))
                    continue
                src.seek(offset)
                with open(slice_path, "wb") as dst:
                    remaining = total_bytes
                    while remaining > 0:
                        chunk = src.read(min(1024 * 1024, remaining))
                        if not chunk:
                            break
                        dst.write(chunk)
                        remaining -= len(chunk)

            if fs_type == "ext2":
                tool = find_tool("e2fsck")
                proc = subprocess.run(
                    [tool, "-fn", slice_path],
                    text=True,
                    capture_output=True,
                )
            else:
                tool = find_tool("fsck.fat")
                proc = subprocess.run(
                    [tool, "-n", slice_path],
                    text=True,
                    capture_output=True,
                )

            if proc.returncode != 0:
                raise QemuHarnessError(
                    f"{os.path.basename(tool)} failed on partition {part.index} ({fs_type}):\n"
                    f"{proc.stdout}\n{proc.stderr}"
                )
            results.append((part, fs_type, "ok"))
            try:
                os.remove(slice_path)
            except OSError:
                pass
    finally:
        if cleanup_dir:
            shutil.rmtree(cleanup_dir, ignore_errors=True)
    return results


class QmpClient:
    """QMP / HMP socket client with mouse, keyboard, and screendump helpers."""

    def __init__(self, port, timeout=15.0, proc=None):
        self.port = port
        self.sock = None
        self.sf = None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if proc is not None and proc.poll() is not None:
                raise QemuHarnessError(
                    f"QEMU exited early with status {proc.returncode} before QMP connected"
                )
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=5.0)
                break
            except OSError:
                time.sleep(0.15)
        if not self.sock:
            raise QemuHarnessError(f"Timed out connecting to QMP port {port}")
        self.sf = self.sock.makefile("rwb", buffering=0)
        self.read_msg()
        self.cmd("qmp_capabilities")

    def read_msg(self):
        while True:
            line = self.sf.readline()
            if not line:
                return None
            obj = json.loads(line.decode("utf-8"))
            if "event" not in obj:
                return obj

    def cmd(self, execute, arguments=None):
        req = {"execute": execute}
        if arguments is not None:
            req["arguments"] = arguments
        self.sf.write((json.dumps(req) + "\n").encode("utf-8"))
        resp = self.read_msg()
        if resp and "error" in resp:
            raise QemuHarnessError(f"QMP error on {execute}: {resp['error']}")
        return resp

    def hmp(self, command_line):
        return self.cmd("human-monitor-command", {"command-line": command_line})

    def move_to(self, tx, ty):
        """Zero PS/2 pointer to top-left corner, then step safely to (tx, ty)."""
        for _ in range(15):
            self.hmp("mouse_move -120 -120")
            time.sleep(0.012)
        cx, cy = 0, 0
        while cx < tx or cy < ty:
            dx = min(90, tx - cx)
            dy = min(90, ty - cy)
            self.hmp(f"mouse_move {dx} {dy}")
            cx += dx
            cy += dy
            time.sleep(0.018)

    def hover(self, tx, ty, duration=0.2):
        """Move pointer to (tx, ty) and dwell for duration seconds."""
        self.move_to(tx, ty)
        if duration > 0:
            time.sleep(duration)

    @staticmethod
    def _resolve_button(button):
        if isinstance(button, str):
            key = button.strip().lower()
            if key not in MOUSE_BUTTON_MAP:
                raise ValueError(f"Unknown mouse button: {button!r}")
            return MOUSE_BUTTON_MAP[key]
        return int(button)

    def click(self, tx, ty, button=1, wait_after=0.5):
        """Move to (tx, ty) and click mouse button (1=left, 2=middle, 4=right)."""
        btn_mask = self._resolve_button(button)
        self.move_to(tx, ty)
        time.sleep(0.08)
        self.hmp(f"mouse_button {btn_mask}")
        time.sleep(0.06)
        self.hmp("mouse_button 0")
        if wait_after > 0:
            time.sleep(wait_after)

    def right_click(self, tx, ty, wait_after=0.5):
        """Move to (tx, ty) and perform a right mouse click."""
        self.click(tx, ty, button=4, wait_after=wait_after)

    def double_click(self, tx, ty, wait_after=0.5, interval=0.08):
        """Move to (tx, ty) and perform two rapid left mouse clicks."""
        self.move_to(tx, ty)
        time.sleep(0.08)
        for idx in range(2):
            self.hmp("mouse_button 1")
            time.sleep(0.05)
            self.hmp("mouse_button 0")
            if idx == 0 and interval > 0:
                time.sleep(interval)
        if wait_after > 0:
            time.sleep(wait_after)

    def drag(self, sx, sy, steps, dx, dy, delay=0.05, wait_after=0.6):
        """Press left button at (sx, sy), step by (dx, dy) for steps moves, and release."""
        self.move_to(sx, sy)
        time.sleep(0.08)
        self.hmp("mouse_button 1")
        time.sleep(0.08)
        for _ in range(steps):
            self.hmp(f"mouse_move {dx} {dy}")
            time.sleep(delay)
        self.hmp("mouse_button 0")
        if wait_after > 0:
            time.sleep(wait_after)

    def send_key(self, key, delay=0.05):
        """Send a QEMU HMP key chord (e.g. 'esc', 'alt-spc', 'ret')."""
        self.hmp(f"sendkey {key}")
        if delay > 0:
            time.sleep(delay)

    @staticmethod
    def _char_to_sendkey(ch):
        if ch in ASCII_KEYMAP:
            return ASCII_KEYMAP[ch]
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            return ch
        if "A" <= ch <= "Z":
            return "shift-" + ch.lower()
        raise ValueError(f"Unsupported character for QEMU sendkey: {ch!r}")

    def send_text(self, text, delay=0.05):
        """Type printable ASCII text (including symbols, spaces, newlines, tabs) via sendkey."""
        for ch in text:
            self.send_key(self._char_to_sendkey(ch), delay=delay)

    def screendump(self, out_dir, name, save_png=True):
        """Capture framebuffer via QMP screendump, optionally saving <out_dir>/<name>.png."""
        os.makedirs(out_dir, exist_ok=True)
        ppm_path = os.path.join(out_dir, f"{name}.ppm")
        png_path = os.path.join(out_dir, f"{name}.png")
        self.cmd("screendump", {"filename": ppm_path.replace("\\", "/")})
        time.sleep(0.2)
        w, h, raw = read_ppm(ppm_path)
        if save_png:
            write_png(w, h, raw, png_path)
        try:
            os.remove(ppm_path)
        except OSError:
            pass
        if save_png:
            print(f"[visual] saved {png_path} ({w}x{h})")
        return w, h, raw

    def wait_for_screen_change(
        self,
        out_dir,
        region=None,
        timeout=10.0,
        interval=0.25,
        threshold=0.001,
        baseline=None,
    ):
        """Poll screendump until pixel diff ratio in region exceeds threshold."""
        if baseline is None:
            w, h, base_raw = self.screendump(out_dir, "_screen_wait_base", save_png=False)
        else:
            w, h, base_raw = baseline

        if region is None:
            x0, y0, x1, y1 = 0, 0, w, h
        else:
            x0, y0, x1, y1 = region

        deadline = time.monotonic() + timeout
        last_diff = 0.0
        while time.monotonic() < deadline:
            time.sleep(interval)
            cw, ch, cur_raw = self.screendump(out_dir, "_screen_wait_cur", save_png=False)
            if (cw, ch) != (w, h):
                return cw, ch, cur_raw, 1.0
            last_diff = region_diff_ratio(w, base_raw, cur_raw, x0, y0, x1, y1)
            if last_diff > threshold:
                return cw, ch, cur_raw, last_diff
        raise QemuHarnessError(
            f"Timed out after {timeout:.1f}s waiting for screen change in "
            f"[{x0}..{x1}, {y0}..{y1}] (last_diff={last_diff:.4f}, threshold={threshold:.4f})"
        )

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        try:
            if self.sf:
                self.sf.close()
            if self.sock:
                self.sock.close()
        except Exception:
            pass


class QemuHarness:
    """Managed QEMU VM session with background serial logging, QMP control, and failure dumps."""

    def __init__(
        self,
        out_dir="tmp-qemu-harness",
        iso=None,
        boot_disk=None,
        extra_disks=None,
        storage="ide",
        machine="pc",
        firmware="bios",
        smp=2,
        mem="256",
        qmp_port=0,
        vnc=None,
        fw_cfg=None,
        cmdline=None,
        kernel=None,
        boot_order=None,
        ovmf_code=None,
        ovmf_vars=None,
        extra_args=None,
        no_reboot=True,
        capture_failure_on_exit=True,
        echo_serial=False,
    ):
        self.out_dir = out_dir
        self.iso = iso
        self.boot_disk = boot_disk
        self.extra_disks = list(extra_disks) if extra_disks else []
        self.storage = storage
        self.machine = machine
        self.firmware = firmware
        self.smp = str(smp)
        self.mem = str(mem)
        self.requested_qmp_port = int(qmp_port)
        self.qmp_port = 0
        self.vnc = vnc
        self.fw_cfg = list(fw_cfg) if fw_cfg else []
        self.cmdline = cmdline
        self.kernel = kernel
        self.boot_order = boot_order
        self.ovmf_code = ovmf_code
        self.ovmf_vars = ovmf_vars
        self.extra_args = list(extra_args) if extra_args else []
        self.no_reboot = no_reboot
        self.capture_failure_on_exit = capture_failure_on_exit
        self.echo_serial = echo_serial

        self.serial_path = os.path.join(self.out_dir, "serial.log")
        self.ovmf_vars_copy = None
        self.proc = None
        self.qmp = None

        self._serial_lock = threading.Lock()
        self._serial_cond = threading.Condition(self._serial_lock)
        self._serial_buf = ""
        self._serial_eof = False
        self._serial_thread = None
        self._serial_file = None
        self._diagnostics_captured = False

    def _build_storage_args(self):
        args = []
        disks = []
        if self.boot_disk:
            disks.append(self.boot_disk)
        disks.extend(self.extra_disks)

        if not disks:
            return args

        if self.storage == "ide":
            for disk in disks:
                args.extend(["-drive", f"file={disk},format=raw,if=ide"])
        elif self.storage == "ahci":
            args.extend(["-device", "ahci,id=ahci"])
            for idx, disk in enumerate(disks):
                drv_id = f"ahci_d{idx}"
                args.extend([
                    "-drive", f"if=none,id={drv_id},file={disk},format=raw",
                    "-device", f"ide-hd,drive={drv_id},bus=ahci.{idx}",
                ])
        elif self.storage == "virtio":
            for disk in disks:
                args.extend(["-drive", f"file={disk},if=virtio,format=raw"])
        elif self.storage == "nvme":
            for idx, disk in enumerate(disks):
                drv_id = f"nvme_d{idx}"
                serial = "deadbeef" if idx == 0 else f"deadbeef{idx}"
                args.extend([
                    "-drive", f"if=none,id={drv_id},file={disk},format=raw",
                    "-device", f"nvme,serial={serial},drive={drv_id}",
                ])
        else:
            raise ValueError(f"Unsupported storage controller: {self.storage!r}")
        return args

    def build_command(self):
        """Construct the QEMU command-line arguments."""
        qemu_sys = find_tool("qemu-system-x86_64")
        if self.qmp_port == 0:
            self.qmp_port = (
                find_free_port() if self.requested_qmp_port == 0 else self.requested_qmp_port
            )

        cmd = [qemu_sys, "-machine", self.machine]

        if self.firmware == "uefi":
            code_path, vars_src = find_ovmf_paths(self.ovmf_code, self.ovmf_vars)
            self.ovmf_vars_copy = os.path.join(self.out_dir, "OVMF_VARS.fd")
            shutil.copyfile(vars_src, self.ovmf_vars_copy)
            cmd.extend([
                "-drive", f"if=pflash,format=raw,readonly=on,file={code_path}",
                "-drive", f"if=pflash,format=raw,file={self.ovmf_vars_copy}",
            ])
        elif self.firmware != "bios":
            raise ValueError(f"Unsupported firmware type: {self.firmware!r}")

        if self.kernel:
            cmd.extend(["-kernel", self.kernel])
            if self.cmdline:
                cmd.extend(["-append", self.cmdline])
        elif self.cmdline:
            cmd.extend(["-fw_cfg", f"name=opt/org.tsukasa.cmdline,string={self.cmdline}"])

        if self.iso:
            cmd.extend(["-cdrom", self.iso])

        cmd.extend(self._build_storage_args())

        boot = self.boot_order
        if boot is None and self.boot_disk and not self.iso:
            boot = "c"
        if boot:
            cmd.extend(["-boot", boot])

        cmd.extend([
            "-m", self.mem,
            "-smp", self.smp,
            "-vga", "std",
            "-serial", "stdio",
            "-qmp", f"tcp:127.0.0.1:{self.qmp_port},server,nowait",
        ])

        if self.no_reboot:
            cmd.append("-no-reboot")

        for spec in self.fw_cfg:
            cmd.extend(["-fw_cfg", spec])

        if self.vnc:
            vnc_spec = self.vnc if ":" in self.vnc else f"127.0.0.1:{self.vnc}"
            if vnc_spec.startswith(":"):
                vnc_spec = "127.0.0.1" + vnc_spec
            cmd.extend(["-vnc", vnc_spec])
            print(f"[qemu] VNC server enabled at {vnc_spec}")
        else:
            cmd.extend(["-display", "none"])

        cmd.extend(self.extra_args)
        return cmd

    def _serial_reader_loop(self):
        try:
            while True:
                chunk = self.proc.stdout.read(4096)
                if not chunk:
                    break
                self._serial_file.write(chunk)
                self._serial_file.flush()
                text = chunk.decode("utf-8", errors="replace")
                if self.echo_serial:
                    sys.stdout.write(text)
                    sys.stdout.flush()
                with self._serial_cond:
                    self._serial_buf += text
                    self._serial_cond.notify_all()
        finally:
            with self._serial_cond:
                self._serial_eof = True
                self._serial_cond.notify_all()

    def start(self, connect_qmp=True, qmp_timeout=15.0):
        """Launch QEMU, start serial logging thread, and optionally connect QMP."""
        os.makedirs(self.out_dir, exist_ok=True)
        self._serial_buf = ""
        self._serial_eof = False
        self._diagnostics_captured = False

        cmd = self.build_command()
        self._serial_file = open(self.serial_path, "wb")
        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        self._serial_thread = threading.Thread(
            target=self._serial_reader_loop,
            name="qemu-serial-reader",
            daemon=True,
        )
        self._serial_thread.start()

        if connect_qmp:
            self.qmp = QmpClient(self.qmp_port, timeout=qmp_timeout, proc=self.proc)
        return self

    def __enter__(self):
        return self.start()

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is not None and self.capture_failure_on_exit:
            self.capture_failure_diagnostics()
        self.stop()
        return False

    def stop(self, timeout=5.0):
        """Shut down QMP and terminate QEMU cleanly."""
        if self.qmp is not None:
            self.qmp.close()
            self.qmp = None
        if self.proc is not None:
            try:
                self.proc.wait(timeout=timeout)
            except Exception:
                self.proc.kill()
                try:
                    self.proc.wait(timeout=2.0)
                except Exception:
                    pass
            self.proc = None
        if self._serial_thread is not None:
            self._serial_thread.join(timeout=2.0)
            self._serial_thread = None
        if self._serial_file is not None:
            try:
                self._serial_file.close()
            except Exception:
                pass
            self._serial_file = None

    def serial_offset(self):
        """Return current length of captured serial text."""
        with self._serial_lock:
            return len(self._serial_buf)

    def get_serial_log(self):
        """Return all serial output captured so far."""
        with self._serial_lock:
            return self._serial_buf

    def get_serial_tail(self, lines=100):
        """Return the last N lines of the serial log."""
        text = self.get_serial_log()
        split = text.splitlines()
        return "\n".join(split[-lines:])

    def wait_for_serial(self, pattern, timeout=30.0, regex=False, from_offset=0):
        """Block until pattern matches in serial output from from_offset, or raise."""
        compiled = re.compile(pattern) if regex else None
        deadline = time.monotonic() + timeout
        with self._serial_cond:
            while True:
                haystack = self._serial_buf[from_offset:]
                if regex:
                    match = compiled.search(haystack)
                    if match:
                        return match.group(0)
                else:
                    idx = haystack.find(pattern)
                    if idx != -1:
                        return haystack[idx:idx + len(pattern)]

                if self._serial_eof:
                    raise QemuHarnessError(
                        f"QEMU serial stream ended before pattern matched: {pattern!r}"
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise QemuHarnessError(
                        f"Timed out after {timeout:.1f}s waiting for serial pattern: {pattern!r}"
                    )
                self._serial_cond.wait(timeout=min(remaining, 0.25))

    def wait_for_exit(self, timeout=60.0):
        """Wait for QEMU to power off / exit and return its exit code."""
        if self.proc is None:
            return 0
        try:
            rc = self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise QemuHarnessError(
                f"Timed out after {timeout:.1f}s waiting for QEMU to exit"
            ) from exc
        if self._serial_thread is not None:
            self._serial_thread.join(timeout=2.0)
        return rc

    def assert_no_crashes(self, extra_signatures=None):
        """Assert no panic, CPU exception, UBSAN hit, or stack smashing appears in serial.log."""
        assert_no_crashes(self.get_serial_log(), extra_signatures=extra_signatures)

    def capture_failure_diagnostics(self, name="failure", tail_lines=100):
        """Save failure.png (if VM is still alive) and print last N lines of serial.log."""
        if self._diagnostics_captured:
            return
        self._diagnostics_captured = True
        if self.qmp is not None and self.proc is not None and self.proc.poll() is None:
            try:
                self.qmp.screendump(self.out_dir, name, save_png=True)
            except Exception as exc:
                print(f"[diagnostics] failed to capture {name}.png: {exc}", file=sys.stderr)
        tail = self.get_serial_tail(lines=tail_lines)
        print(
            f"----- FAILURE DIAGNOSTICS (last {tail_lines} lines of {self.serial_path}) -----",
            file=sys.stderr,
        )
        print(tail if tail else "(serial log empty)", file=sys.stderr)
        print("----- END FAILURE DIAGNOSTICS -----", file=sys.stderr)

    # Convenience QMP/HMP delegates
    def cmd(self, execute, arguments=None):
        return self.qmp.cmd(execute, arguments)

    def hmp(self, command_line):
        return self.qmp.hmp(command_line)

    def move_to(self, x, y):
        return self.qmp.move_to(x, y)

    def hover(self, x, y, duration=0.2):
        return self.qmp.hover(x, y, duration=duration)

    def click(self, x, y, button=1, wait_after=0.5):
        return self.qmp.click(x, y, button=button, wait_after=wait_after)

    def right_click(self, x, y, wait_after=0.5):
        return self.qmp.right_click(x, y, wait_after=wait_after)

    def double_click(self, x, y, wait_after=0.5, interval=0.08):
        return self.qmp.double_click(x, y, wait_after=wait_after, interval=interval)

    def drag(self, sx, sy, steps, dx, dy, delay=0.05, wait_after=0.6):
        return self.qmp.drag(sx, sy, steps, dx, dy, delay=delay, wait_after=wait_after)

    def send_key(self, key, delay=0.05):
        return self.qmp.send_key(key, delay=delay)

    def send_text(self, text, delay=0.05):
        return self.qmp.send_text(text, delay=delay)

    def screendump(self, name, save_png=True):
        return self.qmp.screendump(self.out_dir, name, save_png=save_png)

    def wait_for_screen_change(
        self,
        region=None,
        timeout=10.0,
        interval=0.25,
        threshold=0.001,
        baseline=None,
    ):
        return self.qmp.wait_for_screen_change(
            self.out_dir,
            region=region,
            timeout=timeout,
            interval=interval,
            threshold=threshold,
            baseline=baseline,
        )


def _parse_xy(spec):
    parts = [p.strip() for p in spec.split(",")]
    if len(parts) != 2:
        raise ValueError(f"Expected 'x,y' coordinate, got {spec!r}")
    return int(parts[0]), int(parts[1])


def _check_milestones_file(serial_text, milestones_path):
    with open(milestones_path, "r", encoding="utf-8") as f:
        for raw in f:
            pat = raw.strip()
            if not pat:
                continue
            if pat not in serial_text:
                raise QemuHarnessError(f"Missing boot milestone: {pat!r}")


def _run_cli(args):
    iso = args.iso
    if iso is None and args.boot_disk is None:
        iso = "tsukasa.iso"

    harness = QemuHarness(
        out_dir=args.out_dir,
        iso=iso,
        boot_disk=args.boot_disk,
        extra_disks=args.extra_disk,
        storage=args.storage,
        machine=args.machine,
        firmware=args.firmware,
        smp=args.smp,
        mem=args.mem,
        qmp_port=args.qmp_port,
        vnc=args.vnc,
        fw_cfg=args.fw_cfg,
        cmdline=args.cmdline,
        ovmf_code=args.ovmf_code,
        ovmf_vars=args.ovmf_vars,
        echo_serial=args.echo_serial,
    )

    with harness as vm:
        if args.action == "interactive":
            print(f"[qemu] interactive mode active (QMP 127.0.0.1:{vm.qmp_port}); Ctrl+C to stop")
            while vm.proc.poll() is None:
                time.sleep(0.5)
            return 0

        if args.action == "boot-check":
            if args.wait_exit:
                rc = vm.wait_for_exit(timeout=args.timeout)
                if rc != 0:
                    raise QemuHarnessError(f"QEMU exited with status {rc}")
                if args.wait_serial:
                    vm.wait_for_serial(args.wait_serial, timeout=1.0, regex=args.regex)
            else:
                target = args.wait_serial or "[vanilla] Listening for client connections."
                vm.wait_for_serial(target, timeout=args.timeout, regex=args.regex)
                if args.boot_wait > 0:
                    time.sleep(args.boot_wait)
            if args.check_milestones:
                _check_milestones_file(vm.get_serial_log(), args.check_milestones)
            if args.screenshot:
                vm.screendump(args.screenshot, save_png=True)
            vm.assert_no_crashes()
            print("[qemu] boot-check PASS")
            return 0

        if args.action == "screenshot":
            if args.wait_serial:
                vm.wait_for_serial(args.wait_serial, timeout=args.timeout, regex=args.regex)
            if args.boot_wait > 0:
                time.sleep(args.boot_wait)
            shot_name = args.screenshot or "screenshot"
            vm.screendump(shot_name, save_png=True)
            vm.assert_no_crashes()
            print(f"[qemu] screenshot PASS ({shot_name}.png)")
            return 0

        if args.action == "cmd":
            if args.ready_serial:
                vm.wait_for_serial(args.ready_serial, timeout=args.timeout, regex=args.regex)
            elif args.boot_wait > 0:
                time.sleep(args.boot_wait)

            if args.click:
                cx, cy = _parse_xy(args.click)
                vm.click(cx, cy, wait_after=0.3)

            offset = vm.serial_offset()
            if args.send_key:
                vm.send_key(args.send_key)
            if args.text is not None:
                text = args.text
                if not text.endswith("\n") and args.auto_enter:
                    text += "\n"
                vm.send_text(text)

            if args.wait_serial:
                vm.wait_for_serial(
                    args.wait_serial,
                    timeout=args.timeout,
                    regex=args.regex,
                    from_offset=offset,
                )
            elif args.wait_exit:
                rc = vm.wait_for_exit(timeout=args.timeout)
                if rc != 0:
                    raise QemuHarnessError(f"QEMU exited with status {rc}")

            if args.screenshot:
                vm.screendump(args.screenshot, save_png=True)
            vm.assert_no_crashes()
            print("[qemu] cmd PASS")
            return 0

    return 0


def build_arg_parser():
    parser = argparse.ArgumentParser(
        description="Project Tsukasa headless QEMU test & QMP automation harness"
    )
    parser.add_argument(
        "--action",
        choices=("boot-check", "cmd", "screenshot", "interactive"),
        default="boot-check",
        help="Automation action to execute",
    )
    parser.add_argument("--iso", default=None, help="Bootable ISO image path")
    parser.add_argument("--boot-disk", default=None, help="Raw disk image to boot directly (without -cdrom)")
    parser.add_argument("--extra-disk", action="append", default=[], help="Extra raw disk image (repeatable)")
    parser.add_argument(
        "--storage",
        choices=("ide", "ahci", "virtio", "nvme"),
        default="ide",
        help="Storage controller for attached disk images",
    )
    parser.add_argument("--machine", choices=("pc", "q35"), default="pc", help="QEMU machine type")
    parser.add_argument("--firmware", choices=("bios", "uefi"), default="bios", help="Firmware type")
    parser.add_argument("--ovmf-code", default=None, help="Override path to OVMF_CODE.fd")
    parser.add_argument("--ovmf-vars", default=None, help="Override path to OVMF_VARS.fd")
    parser.add_argument("--smp", type=int, default=2, help="Number of vCPUs")
    parser.add_argument("--mem", default="256", help="Guest RAM in MiB")
    parser.add_argument("--fw-cfg", action="append", default=[], help="QEMU -fw_cfg spec (repeatable)")
    parser.add_argument("--cmdline", default=None, help="Kernel cmdline / fw_cfg cmdline string")
    parser.add_argument("--qmp-port", type=int, default=0, help="TCP port for QMP (0 = ephemeral free port)")
    parser.add_argument("--vnc", default=None, help="Optional QEMU VNC display (e.g. :1)")
    parser.add_argument("--out-dir", default="tmp-qemu-harness", help="Output directory for serial.log and PNGs")
    parser.add_argument("--timeout", type=float, default=60.0, help="Timeout in seconds for serial/exit waits")
    parser.add_argument("--boot-wait", type=float, default=2.0, help="Extra settle delay in seconds")
    parser.add_argument("--ready-serial", default=None, help="Serial pattern to wait for before sending input")
    parser.add_argument("--wait-serial", default=None, help="Serial pattern to assert")
    parser.add_argument("--regex", action="store_true", help="Treat --wait-serial / --ready-serial as regex")
    parser.add_argument("--wait-exit", action="store_true", help="Wait for guest poweroff / QEMU exit")
    parser.add_argument("--check-milestones", default=None, help="Path to milestones file to verify")
    parser.add_argument("--click", default=None, help="Click 'x,y' before typing in cmd action")
    parser.add_argument("--text", default=None, help="Text to send via QMP sendkey in cmd action")
    parser.add_argument("--auto-enter", action="store_true", help="Append newline if --text lacks trailing newline")
    parser.add_argument("--send-key", default=None, help="QEMU key chord to send (e.g. alt-spc)")
    parser.add_argument("--screenshot", default=None, help="Capture PNG screenshot with given basename")
    parser.add_argument("--echo-serial", action="store_true", help="Stream guest serial output to stdout")
    return parser


def main():
    parser = build_arg_parser()
    args = parser.parse_args()
    try:
        sys.exit(_run_cli(args))
    except Exception as exc:
        print(f"[qemu] ERROR: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
