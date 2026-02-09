#!/usr/bin/env python3
"""
Build-time check: verify multiboot header is within first 8KB of kernel file.
GRUB only scans the first 8192 bytes for the multiboot magic (0x1BADB002).
Writes NDJSON to debug log for hypothesis evaluation.
"""
import struct
import sys
import json
import os

MB_MAGIC = 0x1BADB002
MB_MAGIC_BYTES = struct.pack("<I", MB_MAGIC)
LOG_PATH = os.path.join(os.path.dirname(__file__), ".cursor", "debug.log")
GRUB_SCAN_LIMIT = 8192

def main():
    if len(sys.argv) < 2:
        print("Usage: check_multiboot.py <myos.bin>", file=sys.stderr)
        sys.exit(1)
    bin_path = sys.argv[1]
    if not os.path.isfile(bin_path):
        print("File not found:", bin_path, file=sys.stderr)
        sys.exit(1)

    data = open(bin_path, "rb").read()
    first_8k = data[:GRUB_SCAN_LIMIT]
    magic_offset = first_8k.find(MB_MAGIC_BYTES)
    first_64_hex = data[:64].hex() if len(data) >= 64 else data.hex()

    # Parse ELF to get first PT_LOAD file offset (p_offset)
    first_pt_load_offset = None
    if len(data) >= 52 and data[:4] == b"\x7fELF":
        # ELF32: e_phoff at 0x1c, e_phnum at 0x2c, p_type at 0, p_offset at 4
        e_phoff = struct.unpack("<I", data[0x1c:0x20])[0]
        e_phnum = struct.unpack("<H", data[0x2c:0x2e])[0]
        e_phentsize = struct.unpack("<H", data[0x2a:0x2c])[0]
        for i in range(e_phnum):
            ph_start = e_phoff + i * e_phentsize
            if ph_start + 4 > len(data):
                break
            p_type = struct.unpack("<I", data[ph_start:ph_start + 4])[0]
            if p_type == 1:  # PT_LOAD
                p_offset = struct.unpack("<I", data[ph_start + 4:ph_start + 8])[0]
                first_pt_load_offset = p_offset
                break

    payload = {
        "id": "multiboot_check",
        "timestamp": __import__("time").time() * 1000,
        "location": "check_multiboot.py",
        "message": "Kernel binary multiboot header check",
        "data": {
            "bin_path": bin_path,
            "file_size": len(data),
            "magic_found_in_first_8k": magic_offset >= 0,
            "magic_file_offset": magic_offset if magic_offset >= 0 else None,
            "grub_scan_limit": GRUB_SCAN_LIMIT,
            "first_64_bytes_hex": first_64_hex,
            "first_pt_load_file_offset": first_pt_load_offset,
            "header_within_8k": magic_offset >= 0 and magic_offset < GRUB_SCAN_LIMIT,
        },
        "hypothesisId": "A" if magic_offset < 0 or magic_offset >= GRUB_SCAN_LIMIT else "OK",
    }
    log_dir = os.path.dirname(LOG_PATH)
    if log_dir and not os.path.isdir(log_dir):
        os.makedirs(log_dir, exist_ok=True)
    with open(LOG_PATH, "a", encoding="utf-8") as f:
        f.write(json.dumps(payload) + "\n")
    if magic_offset >= 0 and magic_offset < GRUB_SCAN_LIMIT:
        print("[OK] Multiboot header at file offset", magic_offset, "(within 8KB)")
    else:
        print("[FAIL] Multiboot header not in first 8KB; magic_offset=", magic_offset, file=sys.stderr)

if __name__ == "__main__":
    main()
