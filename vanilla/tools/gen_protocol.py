"""Protocol Code Generator for Vanilla Display Server Binary IPC.

Reads message specifications from YAML files and emits:
- C header (enums, packed structs, static size assertions, dispatch table)
- Markdown wire-format documentation
"""
import argparse, os, re, sys

GPL = (
    "/*\n * Project Tsukasa — Auto-Generated Vanilla Display Server IPC Protocol\n"
    " *\n * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.\n"
    " *\n * Project Tsukasa was created and is maintained by frosty (@enafrosty).\n"
    " * This program is free software: you can redistribute it and/or modify it\n"
    " * under the terms of the GNU General Public License as published by the\n"
    " * Free Software Foundation, either version 3 of the License, or (at your\n"
    " * option) any later version. See the top-level LICENSE file.\n *\n"
    " * This program is distributed in the hope that it will be useful, but\n"
    " * WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n */\n"
)

def parse_yaml(path):
    msg, cur = {"fields": []}, None
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            s = raw.strip()
            if not s or s.startswith("#"): continue
            if not (raw.startswith(" ") or raw.startswith("\t")): cur = None
            if s.startswith("- "):
                cur = {}; msg["fields"].append(cur); s = s[2:].strip()
            if ":" in s:
                k, v = [x.strip().strip('"\'') for x in s.split(":", 1)]
                target = cur if (cur is not None and (raw.startswith(" ") or raw.startswith("\t"))) else msg
                if k in ("id", "version"): target[k] = int(v)
                elif k in ("v1_compat", "variable_payload"): target[k] = v.lower() in ("true", "1", "yes")
                elif k != "fields": target[k] = v
    msg.setdefault("version", 1); msg.setdefault("v1_compat", True)
    return msg

def field_size(ftype):
    if ftype == "uint8_t": return 1
    if ftype == "uint16_t": return 2
    if ftype in ("uint32_t", "int32_t"): return 4
    if ftype == "struct input_event": return 24
    m = re.match(r"^(char|uint8_t)\[(\d+)\]$", ftype)
    if m: return int(m.group(2))
    raise ValueError(f"Unknown type: {ftype}")

def struct_name(name):
    return f"vanilla_msg_{name[4:].lower() if name.startswith('MSG_') else name.lower()}_t"

def field_decl(f):
    m = re.match(r"^(char|uint8_t)\[(\d+)\]$", f["type"])
    return f"    {m.group(1):<8} {f['name']}[{m.group(2)}];" if m else f"    {f['type']:<8} {f['name']};"

def validate(messages):
    ids, names = set(), set()
    for m in messages:
        for req in ("id", "name", "direction", "fields"):
            if req not in m: sys.exit(f"Error: Missing field '{req}' in {m}")
        if m["id"] in ids: sys.exit(f"Error: Duplicate ID {m['id']}")
        if m["name"] in names: sys.exit(f"Error: Duplicate name {m['name']}")
        if m["id"] <= 0 or m["direction"] not in ("client_to_server", "server_to_client"):
            sys.exit(f"Error: Invalid metadata in {m['name']}")
        ids.add(m["id"]); names.add(m["name"])
        for f in m["fields"]:
            if "name" not in f or "type" not in f: sys.exit(f"Error: Incomplete field in {m['name']}")
            try: field_size(f["type"])
            except ValueError as e: sys.exit(f"Error in {m['name']}: {e}")

def emit_header(messages):
    c2s = [m for m in messages if m["direction"] == "client_to_server"]
    enums = "\n".join(f"    {m['name']:<24} = {m['id']}," for m in messages)
    sblocks = []
    for m in messages:
        sn = struct_name(m["name"])
        tot = sum(field_size(f["type"]) for f in m["fields"])
        f_decls = "\n".join(field_decl(f) for f in m["fields"])
        sblocks.append(f"/* {m['name']} payload ({m['direction'].replace('_', ' ')}, v{m['version']}+) */\n"
                       f"typedef struct {{\n{f_decls}\n}} __attribute__((packed)) {sn};\n\n"
                       f'_Static_assert(sizeof({sn}) == {tot},\n    "{sn} size mismatch; regenerate protocol");\n\n')
    protos = "\n".join(f"int handle_{m['name'].lower()}(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);" for m in c2s)
    def get_sz(m):
        return "0" if m.get("variable_payload") else f"sizeof({struct_name(m['name'])})"
    tbl = "\n".join(f"    {{ {m['name']}, {m['version']}, {get_sz(m)}, handle_{m['name'].lower()} }}," for m in c2s)
    return (
        f"{GPL}/* Auto-generated - do not edit. Regenerate with: make -C vanilla gen-protocol */\n\n"
        f"#ifndef _VANILLA_PROTOCOL_GENERATED_H\n#define _VANILLA_PROTOCOL_GENERATED_H\n\n"
        f"#include <stdint.h>\n#include <stddef.h>\n#include <sys/input.h>\n\n"
        f"typedef struct {{\n    uint32_t magic;\n    uint16_t msg_type;\n"
        f"    uint16_t payload_len;\n    uint32_t window_id;\n}} __attribute__((packed)) vanilla_msg_hdr_t;\n\n"
        f'_Static_assert(sizeof(vanilla_msg_hdr_t) == 12, "vanilla_msg_hdr_t size mismatch");\n\n'
        f"typedef enum {{\n{enums}\n}} vanilla_msg_type_t;\n\n"
        f"{''.join(sblocks)}"
        f"typedef struct vanilla_server vanilla_server_t;\n\n"
        f"typedef int (*vanilla_dispatch_fn_t)(vanilla_server_t *srv, int client_idx,\n"
        f"                                     const vanilla_msg_hdr_t *hdr,\n"
        f"                                     const uint8_t *payload);\n\n"
        f"typedef struct {{\n    uint16_t              msg_type;\n"
        f"    uint16_t              min_version;\n    size_t                payload_size;\n"
        f"    vanilla_dispatch_fn_t handler;\n}} vanilla_dispatch_entry_t;\n\n"
        f"{protos}\n\n#ifdef VANILLA_DISPATCH_TABLE_IMPL\n"
        f"const vanilla_dispatch_entry_t g_vanilla_dispatch_table[] = {{\n{tbl}\n}};\n"
        f"const size_t g_vanilla_dispatch_table_len = sizeof(g_vanilla_dispatch_table) / sizeof(g_vanilla_dispatch_table[0]);\n"
        f"#else\nextern const vanilla_dispatch_entry_t g_vanilla_dispatch_table[];\n"
        f"extern const size_t                   g_vanilla_dispatch_table_len;\n#endif\n\n"
        f"#endif /* _VANILLA_PROTOCOL_GENERATED_H */\n"
    )

def emit_doc(messages):
    sum_rows = "\n".join(f"| {m['id']} | `{m['name']}` | {m['direction']} | {m['version']} | {sum(field_size(f['type']) for f in m['fields'])} B | {m['description']} |" for m in messages)
    details = []
    for m in messages:
        tot = sum(field_size(f["type"]) for f in m["fields"])
        f_rows, off = [], 0
        for f in m["fields"]:
            sz = field_size(f["type"])
            f_rows.append(f"| {off} | `{f['name']}` | `{f['type']}` | {sz} | {f.get('description', '')} |")
            off += sz
        details.append(f"### {m['name']} (ID {m['id']})\n\n- **Direction:** {m['direction']}\n- **Version:** {m['version']}\n"
                       f"- **Compatibility:** {'v1 compatible' if m['v1_compat'] else 'v2+ only'}\n- **Payload Size:** {tot} bytes\n"
                       f"- **Description:** {m['description']}\n\n| Offset | Field | Type | Size | Description |\n"
                       f"|--------|-------|------|------|-------------|\n" + "\n".join(f_rows))
    return (
        f"# Vanilla IPC Protocol Wire Format Specification\n\n"
        f"Authoritative binary protocol reference auto-generated from `vanilla/protocol/*.yaml`.\n\n"
        f"## Message Header Envelope\n\nEvery IPC packet begins with a fixed 12-byte packed header (`vanilla_msg_hdr_t`):\n\n"
        f"| Offset | Field | Type | Size | Description |\n|--------|-------|------|------|-------------|\n"
        f"| 0 | `magic` | `uint32_t` | 4 | Protocol magic `0x56414E49` ('VANI') |\n"
        f"| 4 | `msg_type` | `uint16_t` | 2 | Message type identifier |\n"
        f"| 6 | `payload_len` | `uint16_t` | 2 | Payload length in bytes following header |\n"
        f"| 8 | `window_id` | `uint32_t` | 4 | Target or source window ID (0 if server-wide) |\n\n"
        f"## Protocol Messages Summary\n\n| ID | Name | Direction | Version | Payload Size | Description |\n"
        f"|----|------|-----------|---------|--------------|-------------|\n{sum_rows}\n\n"
        f"## Message Payload Details\n\n" + "\n\n".join(details) + "\n"
    )

def main():
    p = argparse.ArgumentParser(description="Vanilla Protocol Code Generator")
    p.add_argument("--yaml-dir", default="protocol")
    p.add_argument("--out-header", default="include/protocol_generated.h")
    p.add_argument("--out-doc", default="../docs/agent/phase-2-protocol-motion/wire-format.md")
    p.add_argument("--version", type=int, default=2)
    p.add_argument("--print-structs", action="store_true")
    args = p.parse_args()

    files = [os.path.join(args.yaml_dir, f) for f in os.listdir(args.yaml_dir) if f.endswith(".yaml")]
    msgs = sorted([parse_yaml(f) for f in files], key=lambda x: x["id"])
    validate(msgs)

    if args.print_structs:
        for m in msgs:
            print(f"typedef struct {{\n" + "\n".join(field_decl(f) for f in m["fields"]) + f"\n}} __attribute__((packed)) {struct_name(m['name'])};\n")
        return

    os.makedirs(os.path.dirname(os.path.abspath(args.out_header)), exist_ok=True)
    with open(args.out_header, "w", encoding="utf-8") as f: f.write(emit_header(msgs))
    os.makedirs(os.path.dirname(os.path.abspath(args.out_doc)), exist_ok=True)
    with open(args.out_doc, "w", encoding="utf-8") as f: f.write(emit_doc(msgs))

if __name__ == "__main__":
    main()
