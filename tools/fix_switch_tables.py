#!/usr/bin/env python3
"""Find jump-table switches that codegen cut short, and write [[switch_tables]] overrides.

ReXGlue guesses a jump table's size from the nearest `cmpwi rIndex, N` before the
bctr. When that compare is not really the bounds check (e.g. `if (i == 7) skip`),
the table gets truncated and the recompiled switch traps on the missing cases
("Switch case out of range" -> illegal instruction 0xC000001D at runtime).

This reads the real table out of the decrypted XEX image and, where it holds more
valid entries than codegen emitted, writes a full override.

Usage: fix_switch_tables.py <default.xex> <generated dir> <out switches.toml>
Prints "# N switch tables fixed" to stderr. Existing entries in the out file are kept.
"""
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xex_dump  # noqa: E402

BCTR = 0x4E800420
FUNC_RE = re.compile(r"^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?sub_([0-9A-F]{8})\)")
LABEL_RE = re.compile(r"^loc_([0-9A-F]+):")
SWITCH_RE = re.compile(r"^\tswitch \(ctx\.r(\d+)\.u32\) \{")
CASE_RE = re.compile(r"^\tcase (\d+):")
GOTO_RE = re.compile(r"^\t\tgoto loc_([0-9A-F]+);")
DECL_RE = re.compile(r"sub_([0-9A-F]{8})")


def parse_generated(gen_dir):
    """Yield (func_start, emitted_addrs, [switch dicts]) per function."""
    files = sorted(glob.glob(os.path.join(gen_dir, "*_recomp.*.cpp")),
                   key=lambda p: int(re.search(r"\.(\d+)\.cpp$", p).group(1)))
    for path in files:
        func = None
        addr = 0
        emitted = set()
        switches = []
        last_insn = None
        last_comments = []
        cur = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = FUNC_RE.match(line)
                if m:
                    if func is not None:
                        yield func, emitted, switches
                    func = int(m.group(1), 16)
                    addr = func
                    emitted = set()
                    switches = []
                    last_insn = None
                    last_comments = []
                    cur = None
                    continue
                if func is None:
                    continue
                if cur is not None:
                    m = CASE_RE.match(line)
                    if m:
                        cur["_case"] = int(m.group(1))
                        continue
                    m = GOTO_RE.match(line)
                    if m and "_case" in cur:
                        cur["cases"][cur.pop("_case")] = int(m.group(1), 16)
                        continue
                    if line.startswith("\t}"):
                        switches.append(cur)
                        cur = None
                    continue
                m = LABEL_RE.match(line)
                if m:
                    addr = int(m.group(1), 16)
                    continue
                if line.startswith("\t// "):
                    emitted.add(addr)
                    last_insn = addr
                    last_comments.append(line[4:].strip())
                    last_comments = last_comments[-12:]
                    addr += 4
                    continue
                m = SWITCH_RE.match(line)
                if m:
                    cur = dict(bctr=last_insn, reg=int(m.group(1)), cases={},
                               comments=list(last_comments))
        if func is not None:
            yield func, emitted, switches


def main():
    xex, gen_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    base, img = xex_dump.dump(xex, xex + ".img")

    def word(a):
        o = a - base
        if o < 0 or o + 4 > len(img):
            return None
        return struct.unpack(">I", img[o:o + 4])[0]

    # Function starts (sorted) for "next function" bounds.
    starts = set()
    for h in glob.glob(os.path.join(gen_dir, "*_funcs*.h")):
        with open(h, encoding="utf-8", errors="replace") as f:
            for m in DECL_RE.finditer(f.read()):
                starts.add(int(m.group(1), 16))
    starts = sorted(starts)
    import bisect

    def func_end(fs):
        i = bisect.bisect_right(starts, fs)
        return starts[i] if i < len(starts) else fs + 0x100000

    existing = {}
    if os.path.exists(out_path):
        with open(out_path, encoding="utf-8") as f:
            for m in re.finditer(r"address = 0x([0-9A-Fa-f]+)", f.read()):
                existing[int(m.group(1), 16)] = True

    fixes = []
    checked = mismatched = offset_types = 0
    for fs, emitted, switches in parse_generated(gen_dir):
        fe = func_end(fs)
        for sw in switches:
            b = sw["bctr"]
            if b is None or word(b) != BCTR:
                mismatched += 1
                continue
            checked += 1
            if not any(c.startswith("lwzx") for c in sw["comments"]):
                offset_types += 1
                continue
            n = len(sw["cases"])
            if n == 0 or sorted(sw["cases"]) != list(range(n)):
                continue
            targets = [sw["cases"][i] for i in range(n)]
            # Locate the table: nearest occurrence of the emitted entries.
            pat = b"".join(struct.pack(">I", t) for t in targets[:min(n, 4)])
            lo = max(0, b - base - 0x20000)
            off = img.find(pat, b - base)
            if off < 0 or off - (b - base) > 0x20000:
                off = img.find(pat, lo)
            if off < 0:
                continue
            table = base + off
            if any(word(table + 4 * i) != targets[i] for i in range(n)):
                continue
            full = list(targets)
            while len(full) < 1024:
                slot = table + 4 * len(full)
                if slot in emitted:
                    break
                v = word(slot)
                if v is None or v & 3 or not (fs <= v < fe):
                    break
                full.append(v)
            if len(full) > n and b not in existing:
                fixes.append((b, sw["reg"], fs, n, full))

    fresh = not os.path.exists(out_path) or os.path.getsize(out_path) == 0
    with open(out_path, "a", encoding="utf-8") as f:
        if fresh:
            f.write("# Jump tables codegen cut short (generated by tools/fix_switch_tables.py)\n")
        for b, reg, fs, n, full in fixes:
            f.write(f"\n# sub_{fs:08X}: codegen saw {n} cases, table has {len(full)}\n")
            f.write(f"[[switch_tables]]\naddress = 0x{b:08X}\nregister = {reg}\nlabels = [\n")
            for i in range(0, len(full), 6):
                f.write("  " + ", ".join(f"0x{t:08X}" for t in full[i:i + 6]) + ",\n")
            f.write("]\n")
    print(f"# checked {checked} switches ({offset_types} offset-type skipped, "
          f"{mismatched} unmatched); {len(fixes)} switch tables fixed", file=sys.stderr)
    for b, reg, fs, n, full in fixes:
        print(f"  0x{b:08X} in sub_{fs:08X}: {n} -> {len(full)} cases", file=sys.stderr)


if __name__ == "__main__":
    main()
