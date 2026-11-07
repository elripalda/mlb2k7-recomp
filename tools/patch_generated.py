#!/usr/bin/env python3
"""Fix instructions ReXGlue 0.10.1 translates wrongly, directly in the generated code.

vpkd3d128 / vupkd3d128 type 6 (NORMPACKED64, 4:20:20:20) are emitted with the wrong
number format, wrong element order, wrong source half and a broken sign extension.
Games use them for compressed vertex/animation data, so models come out torn apart.
This rewrites each one to match Xenia's behaviour (3.0+X float form, guest elements
2-3, saturation, QNaN on negative overflow).

Safe to run repeatedly. Keeps exactly one `// insn` comment per instruction so the
other tools' address tracking still works.

Usage: patch_generated.py <generated dir>
Exit code 3 means files were changed (rebuild needed), 1 means a failure.
"""
import glob
import os
import re
import sys

V = r"(?:ctx\.)?v\d+"

UNPACK = re.compile(
    r"(?P<head>\t// vupkd3d128 v\d+,v\d+,2[4-7]\n)"
    r"\tvTemp\.u64\[0\] = (?P<src>" + V + r")\.u64\[1\];\n"
    r"\ttemp\.s32 = \(int32_t\(vTemp\.u64\[0\] << 44\) >> 44\);\n"
    r"\t(?P<dst>" + V + r")\.f32\[0\] = float\(temp\.s32\);\n"
    r"\ttemp\.s32 = \(int32_t\(vTemp\.u64\[0\] << 24\) >> 44\);\n"
    r"\t(?P=dst)\.f32\[1\] = float\(temp\.s32\);\n"
    r"\ttemp\.s32 = \(int32_t\(vTemp\.u64\[0\] << 4\) >> 44\);\n"
    r"\t(?P=dst)\.f32\[2\] = float\(temp\.s32\);\n"
    r"\t(?P=dst)\.f32\[3\] = float\(vTemp\.u64\[0\] >> 60\);\n")

PACK = re.compile(
    r"(?P<head>\t// vpkd3d128 v\d+,v\d+,6,(?P<mask>\d),(?P<shift>\d)\n)"
    r"(?P<pre>(?:\t(?!vTemp)[^\n]*\n)?)"
    r"\tvTemp\.u64\[0\] = 0;\n"
    r"\ttemp\.s32 = int32_t\((?P<src>" + V + r")\.f32\[0\]\);\n"
    r"\tvTemp\.u64\[0\] = uint64_t\(temp\.s32 & 0xFFFFF\);\n"
    r"\ttemp\.s32 = int32_t\((?P=src)\.f32\[1\]\);\n"
    r"\tvTemp\.u64\[0\] \|= uint64_t\(temp\.s32 & 0xFFFFF\) << 20;\n"
    r"\ttemp\.s32 = int32_t\((?P=src)\.f32\[2\]\);\n"
    r"\tvTemp\.u64\[0\] \|= uint64_t\(temp\.s32 & 0xFFFFF\) << 40;\n"
    r"\ttemp\.s32 = int32_t\((?P=src)\.f32\[3\]\);\n"
    r"\tvTemp\.u64\[0\] \|= uint64_t\(temp\.s32 & 0xF\) << 60;\n"
    r"\t(?P<dst>" + V + r")\.u64\[\d\] = vTemp\.u64\[0\];\n")


def unpack_repl(m):
    s, d = m["src"], m["dst"]
    c = lambda n: f"(int32_t(int64_t(_p << {n}) >> 44))"
    return (m["head"] +
            f"\t{{ /* rexfix: NORMPACKED64 unpack */ const uint64_t _p = {s}.u64[0];\n"
            f"\tconst int32_t _x = {c(44)}, _y = {c(24)}, _z = {c(4)};\n"
            f"\t{d}.u32[3] = _x == -524288 ? 0x7FC00000u : uint32_t(0x40400000 + _x);\n"
            f"\t{d}.u32[2] = _y == -524288 ? 0x7FC00000u : uint32_t(0x40400000 + _y);\n"
            f"\t{d}.u32[1] = _z == -524288 ? 0x7FC00000u : uint32_t(0x40400000 + _z);\n"
            f"\t{d}.u32[0] = 0x3F800000u + uint32_t(_p >> 60); }}\n")


def pack_repl(m):
    s, d = m["src"], m["dst"]
    mask, shift = int(m["mask"]), int(m["shift"])
    # Where the packed 64 bits land (host u32 index = 3 - guest element), per Xenia's permutes.
    if shift == 0:
        place = f"\t{d}.u32[1] = uint32_t(_v >> 32); {d}.u32[0] = uint32_t(_v);"
    elif shift == 1:
        place = f"\t{d}.u32[2] = uint32_t(_v >> 32); {d}.u32[1] = uint32_t(_v);"
    elif shift == 2:
        place = f"\t{d}.u32[3] = uint32_t(_v >> 32); {d}.u32[2] = uint32_t(_v);"
    elif mask == 3:
        place = f"\t{d}.u32[0] = uint32_t(_v >> 32);"
    else:
        place = f"\t{d}.u32[3] = uint32_t(_v);"
    sat = ("auto _s = [](float f, uint32_t lo, uint32_t hi) -> uint64_t { "
           "const float l = __builtin_bit_cast(float, lo), h = __builtin_bit_cast(float, hi); "
           "f = !(f >= l) ? l : (f > h ? h : f); return __builtin_bit_cast(uint32_t, f); };")
    return (m["head"] + m["pre"] +
            f"\t{{ /* rexfix: NORMPACKED64 pack */ {sat}\n"
            f"\tconst uint64_t _v = (_s({s}.f32[3], 0x40380001u, 0x4047FFFFu) & 0xFFFFF)"
            f" | ((_s({s}.f32[2], 0x40380001u, 0x4047FFFFu) & 0xFFFFF) << 20)"
            f" | ((_s({s}.f32[1], 0x40380001u, 0x4047FFFFu) & 0xFFFFF) << 40)"
            f" | ((_s({s}.f32[0], 0x40400000u, 0x4040000Fu) & 0xF) << 60);\n"
            f"{place} }}\n")


def main():
    gen = sys.argv[1]
    files = changed = n_un = n_pk = 0
    for path in glob.glob(os.path.join(gen, "*_recomp.*.cpp")):
        files += 1
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        crlf = "\r\n" in text
        if crlf:
            text = text.replace("\r\n", "\n")
        new, a = UNPACK.subn(unpack_repl, text)
        new, b = PACK.subn(pack_repl, new)
        if a or b:
            n_un += a
            n_pk += b
            changed += 1
            if crlf:
                new = new.replace("\n", "\r\n")
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new)
    # Anything left unpatched means the generated format changed; say so loudly.
    left = 0
    for path in glob.glob(os.path.join(gen, "*_recomp.*.cpp")):
        with open(path, encoding="utf-8", errors="replace") as f:
            t = f.read()
        left += len(re.findall(r"// vupkd3d128 v\d+,v\d+,2[4-7]\s", t)) - t.count("rexfix: NORMPACKED64 unpack")
        left += len(re.findall(r"// vpkd3d128 v\d+,v\d+,6,\d,\d\s", t)) - t.count("rexfix: NORMPACKED64 pack")
    print(f"# patched {n_un} unpack + {n_pk} pack instructions in {changed} files; {left} left unpatched",
          file=sys.stderr)
    # Exit codes: 1 = something couldn't be patched, 3 = files were patched, 0 = nothing to do.
    sys.exit(1 if left else (3 if changed else 0))


if __name__ == "__main__":
    main()
