#!/usr/bin/env python3
"""Compile the fidelity shaders and pack them as Source engine .vcs files.

Two kinds of shaders, neither needs Valve's tool chain (perl, nmake,
shadercompile.exe). Each is compiled with Microsoft's D3DX9 HLSL compiler
(d3dx9_33.dll, the same one the Source SDK ships in src/devtools/bin) through
the small helper hlslcheck.exe and packed as a version 6 .vcs file:

- screen-space shaders (SHADERS): ps_2_b, no combos, run through
  SDK_screenspace_general - one LZMA block, exactly like Valve's combo-less files
- model shaders (MODEL_SHADERS): vs_3_0 / ps_3_0 for our own shader DLL
  (game_shader_dx6.dll). They list their combos in the header the Valve
  way ("// DYNAMIC: "NAME" "0..1"", "// SKIP: $A && !$B"); every combo is
  compiled against Valve's shader headers (sp/src/materialsystem/stdshaders).
  Only dynamic combos are allowed, so everything lives in static combo 0.
  The script also writes the matching .inc header the C++ shader includes
  (sp/src/materialsystem/fidelity/fxctmp9), like Valve's fxc_prep.pl.

Windows: run directly.  Linux/macOS: needs wine (32 bit).

    python tools/build_shaders.py                # d3dx9_33.dll from sp/src/devtools/bin
    python tools/build_shaders.py --verify      # only check the existing .vcs files

--verify needs no compiler: it decodes every .vcs file and checks that the
source checksum stored inside matches the current shader sources (including
the files they #include), i.e. that the generated files are up to date.

Output goes to game/shaders/fxc/ (and the .inc headers next to the shader DLL code).
"""

import argparse
import ast
import concurrent.futures
import lzma
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_SRC = os.path.join(ROOT, "shaders", "src")
OUTPUT_DIR = os.path.join(ROOT, "game", "shaders", "fxc")
HELPER_EXE = os.path.join(ROOT, "tools", "hlslcheck", "hlslcheck.exe")
VALVE_SHADER_DIR = os.path.join(ROOT, "..", "sp", "src", "materialsystem", "stdshaders")
INC_DIR = os.path.join(ROOT, "..", "sp", "src", "materialsystem", "fidelity", "fxctmp9")

# (output name, source file, extra defines)
SHADERS = [
    ("fidelity_ssao_ps20b", "fidelity_ssao_ps2x.fxc", {"NUM_PAIRS": "8"}),
    ("fidelity_ssao_low_ps20b", "fidelity_ssao_ps2x.fxc", {"NUM_PAIRS": "4"}),
    ("fidelity_ssao_blur_ps20b", "fidelity_ssao_blur_ps2x.fxc", {}),
    ("fidelity_ssao_composite_ps20b", "fidelity_ssao_blur_ps2x.fxc", {"COMPOSITE": "1"}),
    ("fidelity_grade_ps20b", "fidelity_grade_ps2x.fxc", {}),
    ("fidelity_fxaa_ps20b", "fidelity_fxaa_ps2x.fxc", {}),
    ("fidelity_godrays_mask_ps20b", "fidelity_godrays_ps2x.fxc", {"MASK": "1"}),
    ("fidelity_godrays_blur_ps20b", "fidelity_godrays_ps2x.fxc", {}),
    ("fidelity_godrays_composite_ps20b", "fidelity_godrays_ps2x.fxc", {"COMPOSITE": "1"}),
    ("fidelity_volumetric_blur_ps20b", "fidelity_volumetric_ps2x.fxc", {"BLUR": "1"}),
    ("fidelity_volumetric_composite_ps20b", "fidelity_volumetric_ps2x.fxc", {}),
    ("fidelity_flare_ps20b", "fidelity_flare_ps2x.fxc", {}),
]

PROFILE = "ps_2_b"
BASE_DEFINES = {"SHADER_MODEL_PS_2_B": "1"}

# Model shaders of the fidelity shader DLL: file name without .fxc; the suffix
# picks the profile. Combos come from the file's header comments.
MODEL_SHADERS = [
    "fidelity_liquid_vs30",
    "fidelity_liquid_ps30",
    "fidelity_sky_vs30",
    "fidelity_sky_ps30",
    "fidelity_parallax_vs30",
    "fidelity_parallax_ps30",
    "fidelity_parallax_flashlight_vs30",
    "fidelity_parallax_flashlight_ps30",
    "fidelity_volumetric_vs30",
    "fidelity_volumetric_ps30",
    "fidelity_softparticle_vs30",
    "fidelity_softparticle_ps30",
    "fidelity_water_vs30",
    "fidelity_water_ps30",
]
MODEL_PROFILES = {
    "vs30": ("vs_3_0", {"SHADER_MODEL_VS_3_0": "1"}, bytes([0x00, 0x03, 0xFE, 0xFF])),
    "ps30": ("ps_3_0", {"SHADER_MODEL_PS_3_0": "1"}, bytes([0x00, 0x03, 0xFF, 0xFF])),
}
# Largest unpacked block the engine reads (MAX_SHADER_UNPACKED_BLOCK_SIZE in shader_vcs_version.h)
MAX_BLOCK = 1 << 17

VCS_VERSION = 6
BLOCK_LZMA = 0x40000000
BLOCK_END = 0xFFFFFFFF
# LZMA properties used by Valve's tools: lc=3 lp=0 pb=2, 256 KiB dictionary
LZMA_LC, LZMA_LP, LZMA_PB, LZMA_DICT = 3, 0, 2, 1 << 18


def source_crc(source, settings=""):
    """CRC32 over a shader source, every file it #includes (recursively; ours from shaders/src, then
    Valve's from stdshaders) and the build settings (profile, defines), so --verify also notices a
    changed define or a Mapbase update of Valve's headers.

    Line endings are normalized first, so a Windows checkout (CRLF) gives the same value.
    """
    crc = zlib.crc32(settings.encode("ascii")) if settings else 0
    seen = set()
    pending = [source]
    while pending:
        name = pending.pop(0)
        if name in seen:
            continue
        seen.add(name)
        path = next(
            (p for p in (os.path.join(SHADER_SRC, name), os.path.join(VALVE_SHADER_DIR, name)) if os.path.isfile(p)),
            None,
        )
        if path is None:
            continue  # a header of the compiler or the SDK that is not in this repository
        with open(path, "rb") as f:
            data = f.read().replace(b"\r\n", b"\n")
        crc = zlib.crc32(name.encode("ascii") + b"\0" + data, crc)
        pending += [m.decode("ascii") for m in re.findall(rb'^\s*#include\s+"([^"]+)"', data, re.M)]
    return crc & 0xFFFFFFFF


def build_settings(profile, defines):
    """The settings a shader is compiled with, as text for source_crc."""
    return "%s %s" % (profile, sorted(defines.items()))


def lzma_block(data):
    """Return a Valve 'LZMA' block: magic, actual size, packed size, 5 property bytes, raw stream."""
    filters = [{"id": lzma.FILTER_LZMA1, "lc": LZMA_LC, "lp": LZMA_LP, "pb": LZMA_PB, "dict_size": LZMA_DICT}]
    packed = lzma.compress(data, format=lzma.FORMAT_RAW, filters=filters)
    props = bytes([(LZMA_PB * 5 + LZMA_LP) * 9 + LZMA_LC]) + struct.pack("<I", LZMA_DICT)
    return b"LZMA" + struct.pack("<II", len(data), len(packed)) + props + packed


def lzma_unblock(block):
    """Decode a Valve 'LZMA' block (inverse of lzma_block)."""
    magic, actual, packed = struct.unpack("<4sII", block[:12])
    if magic != b"LZMA":
        raise ValueError("block is not LZMA")
    props = block[12:17]
    lc = props[0] % 9
    lp = (props[0] // 9) % 5
    pb = props[0] // 45
    dict_size = struct.unpack("<I", props[1:5])[0]
    dec = lzma.LZMADecompressor(
        format=lzma.FORMAT_RAW,
        filters=[{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": dict_size}],
    )
    data = dec.decompress(block[17 : 17 + packed], max_length=actual)
    if len(data) != actual:
        raise ValueError("LZMA block decodes to %d bytes, expected %d" % (len(data), actual))
    return data


def write_vcs(path, bytecode, crc):
    """Write a combo-less shader: one static combo holding one dynamic combo."""
    write_vcs_combos(path, [(0, bytecode)], 1, crc)


def write_vcs_combos(path, combos, dynamic_count, crc):
    """Write static combo 0 with the given (dynamic combo id, bytecode) entries.

    Entries are packed into LZMA blocks of at most MAX_BLOCK unpacked bytes
    (an entry never spans two blocks), like Valve's shadercompile.
    """
    blocks = []
    current = b""
    for combo_id, bytecode in combos:
        entry = struct.pack("<II", combo_id, len(bytecode)) + bytecode
        if len(entry) > MAX_BLOCK:
            raise ValueError("combo %d is larger than a block the engine reads (%d bytes)" % (combo_id, len(entry)))
        if current and len(current) + len(entry) > MAX_BLOCK:
            blocks.append(current)
            current = b""
        current += entry
    blocks.append(current)
    body = b"".join(struct.pack("<I", BLOCK_LZMA | len(b)) + b for b in (lzma_block(raw) for raw in blocks))

    header_size = 7 * 4
    records_size = 2 * 8
    first_block = header_size + records_size + 4
    end = first_block + len(body) + 4

    out = struct.pack("<iiiIIII", VCS_VERSION, dynamic_count, dynamic_count, 0, 0, 2, crc)
    out += struct.pack("<II", 0, first_block)  # static combo 0
    out += struct.pack("<II", 0xFFFFFFFF, end)  # sentinel
    out += struct.pack("<I", 0)  # no duplicate (alias) records
    out += body
    out += struct.pack("<I", BLOCK_END)
    assert len(out) == end
    with open(path, "wb") as f:
        f.write(out)


def parse_combos(source):
    """Dynamic combos [(name, count)] and skip expressions of a model shader, from its header comments.

    Same notation as Valve's shaders: // DYNAMIC: "NAME" "0..3"   // SKIP: ($A == 0) && ($B != 0)
    """
    with open(os.path.join(SHADER_SRC, source), encoding="utf-8") as f:
        text = f.read()
    if re.search(r"^\s*//\s*STATIC:", text, re.M):
        raise ValueError("%s: model shaders may only have DYNAMIC combos" % source)
    combos = []
    for name, low, high in re.findall(r'^\s*//\s*DYNAMIC:\s*"(\w+)"\s*"(\d+)\.\.(\d+)"', text, re.M):
        if int(low) != 0:
            raise ValueError("%s: combo %s must start at 0" % (source, name))
        combos.append((name, int(high) + 1))
    skips = [m.strip() for m in re.findall(r"^\s*//\s*SKIP:(.*)$", text, re.M)]
    return combos, skips


def combo_table(combos, skips):
    """[(combo index, {name: value})] for every combo that is not skipped, in index order.

    The first combo has stride 1, the next the product of the counts before it (as in Valve's .inc files).
    """
    names = [name for name, _ in combos]
    expressions = []
    for skip in skips:
        expr = skip.replace("&&", " and ").replace("||", " or ")
        expr = re.sub(r"!(?!=)", " not ", expr)
        expressions.append(expr)
    total = 1
    for _, count in combos:
        total *= count
    table = []
    for index in range(total):
        values = {}
        rest = index
        for name, count in combos:
            values[name] = rest % count
            rest //= count
        variables = {"$" + n: values[n] for n in names}
        skipped = False
        for expr in expressions:
            filled = re.sub(r"\$(\w+)", lambda m, v=variables: str(v["$" + m.group(1)]), expr)
            if eval_skip(filled):
                skipped = True
                break
        if not skipped:
            table.append((index, values))
    return table, total


def eval_skip(expression):
    """Value of a filled-in SKIP expression (numbers, comparisons, and/or/not, brackets only)."""
    tree = ast.parse(expression.strip(), mode="eval")
    allowed = (ast.Expression, ast.BoolOp, ast.And, ast.Or, ast.UnaryOp, ast.Not, ast.Compare, ast.Constant)
    allowed += (ast.Eq, ast.NotEq, ast.Lt, ast.LtE, ast.Gt, ast.GtE)
    for node in ast.walk(tree):
        if not isinstance(node, allowed) or (isinstance(node, ast.Constant) and not isinstance(node.value, int)):
            raise ValueError("unexpected SKIP expression: %s" % expression)
    return bool(eval(compile(tree, "<skip>", "eval"), {"__builtins__": {}}, {}))  # noqa: S307 - checked above


def inc_text(name, combos):
    """The C++ header for a model shader with dynamic combos only (as Valve's fxc_prep.pl writes it)."""
    prefix = "psh" if name.endswith("ps30") else "vsh"
    lines = ['#include "shaderlib/cshader.h"']
    lines += ["// Generated by fidelity/tools/build_shaders.py from %s.fxc - do not edit." % name]
    lines += ["class %s_Static_Index" % name, "{", "public:", "\t%s_Static_Index( ) {}" % name]
    lines += ["\tint GetIndex() { return 0; }", "};", "#define shaderStaticTest_%s 0" % name]
    lines += ["class %s_Dynamic_Index" % name, "{", "private:"]
    lines += ["\tint m_n%s;" % combo for combo, _ in combos]
    lines += ["public:"]
    for combo, count in combos:
        lines += ["\tvoid Set%s( int i ) { Assert( i >= 0 && i <= %d ); m_n%s = i; }" % (combo, count - 1, combo)]
        lines += ["\tvoid Set%s( bool i ) { m_n%s = i ? 1 : 0; }" % (combo, combo)]
    lines += ["\t%s_Dynamic_Index()" % name, "\t{"]
    lines += ["\t\tm_n%s = 0;" % combo for combo, _ in combos]
    lines += ["\t}", "\tint GetIndex()", "\t{"]
    terms = []
    stride = 1
    for combo, count in combos:
        terms.append("( %d * m_n%s )" % (stride, combo))
        stride *= count
    lines += ["\t\treturn %s + 0;" % " + ".join(terms) if terms else "\t\treturn 0;", "\t}", "};"]
    tests = " + ".join("%s_forgot_to_set_dynamic_%s" % (prefix, combo) for combo, _ in combos)
    lines += ["#define shaderDynamicTest_%s %s + 0" % (name, tests or "0")]
    return "\n".join(lines) + "\n"


def read_vcs(path):
    """Parse a .vcs file and return a list of (static id, dynamic id, bytecode)."""
    with open(path, "rb") as f:
        data = f.read()
    version, total, dynamic, flags, centroid, num_static, crc = struct.unpack("<iiiIIII", data[:28])
    if version != VCS_VERSION:
        raise ValueError("unsupported vcs version %d" % version)
    records = [struct.unpack("<II", data[28 + 8 * i : 36 + 8 * i]) for i in range(num_static)]
    if records[-1][0] != 0xFFFFFFFF or records[-1][1] != len(data):
        raise ValueError("bad sentinel record")
    pos = 28 + 8 * num_static
    (num_alias,) = struct.unpack("<I", data[pos : pos + 4])
    result = []
    for static_id, offset in records[:-1]:
        pos = offset
        while True:
            (header,) = struct.unpack("<I", data[pos : pos + 4])
            pos += 4
            if header == BLOCK_END:
                break
            size = header & 0x3FFFFFFF
            block = data[pos : pos + size]
            pos += size
            if header & BLOCK_LZMA:
                raw = lzma_unblock(block)
            elif header & 0x80000000:
                raw = block
            else:
                raise ValueError("unsupported block type 0x%08x" % header)
            r = 0
            while r < len(raw):
                combo_id, length = struct.unpack("<II", raw[r : r + 8])
                result.append((static_id, combo_id, raw[r + 8 : r + 8 + length]))
                r += 8 + length
    return {"total": total, "dynamic": dynamic, "aliases": num_alias, "crc": crc, "combos": result}


def to_windows_path(path):
    """Path as the Windows helper sees it (wine maps the Linux root to drive Z:)."""
    if os.name == "nt":
        return path
    return "Z:" + os.path.abspath(path).replace("/", "\\")


def compile_shader(source, defines, d3dx, profile=PROFILE, source_dir=SHADER_SRC):
    """Compile one shader with D3DX9 and return the bytecode."""
    workdir = tempfile.mkdtemp(prefix="fidelity_fxc_")
    try:
        # hlslcheck.exe loads d3dx9_33.dll from its own folder
        shutil.copy(HELPER_EXE, workdir)
        shutil.copy(d3dx, os.path.join(workdir, "d3dx9_33.dll"))
        out_file = os.path.join(workdir, "out.bin")
        args = [os.path.join(workdir, "hlslcheck.exe"), source, profile]
        base = BASE_DEFINES if profile == PROFILE else {}
        args += ["%s=%s" % kv for kv in sorted({**base, **defines}.items())]
        if os.name != "nt":
            args = ["wine"] + args
        env = dict(os.environ, HLSL_OUT=to_windows_path(out_file), WINEDEBUG="-all")
        proc = subprocess.run(args, cwd=source_dir, env=env, capture_output=True, text=True)
        log = (proc.stdout + proc.stderr).strip()
        if proc.returncode != 0 or not os.path.exists(out_file):
            raise RuntimeError("%s failed:\n%s" % (source, log))
        warnings = [line for line in log.splitlines() if "warning" in line.lower()]
        with open(out_file, "rb") as f:
            return f.read(), warnings
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def find_d3dx(explicit):
    """Locate d3dx9_33.dll: the --d3dx argument, a copy next to the helper or the one in the mod's devtools."""
    candidates = [explicit] if explicit else []
    candidates += [
        os.path.join(ROOT, "tools", "hlslcheck", "d3dx9_33.dll"),
        os.path.join(ROOT, "..", "sp", "src", "devtools", "bin", "d3dx9_33.dll"),
    ]
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    sys.exit("d3dx9_33.dll not found - pass --d3dx sp/src/devtools/bin/d3dx9_33.dll")


PS_2_B_TOKEN = bytes([0x01, 0x02, 0xFF, 0xFF])
# What reading a damaged or cut-off .vcs can raise: reported as "unreadable", no traceback
UNREADABLE = (ValueError, struct.error, IndexError, lzma.LZMAError)


def model_profile(name):
    """(profile, defines, version token) from the name's suffix (_vs30 / _ps30)."""
    return MODEL_PROFILES[name.rsplit("_", 1)[1]]


def build_model_shader(name, d3dx):
    """Compile every combo of a model shader; write its .vcs and .inc."""
    source = name + ".fxc"
    combos, skips = parse_combos(source)
    table, total = combo_table(combos, skips)
    profile, defines, _ = model_profile(name)

    # Valve's headers and ours side by side, so #include "common_vs_fxc.h" resolves.
    stage = tempfile.mkdtemp(prefix="fidelity_model_")
    try:
        for folder in (VALVE_SHADER_DIR, SHADER_SRC):
            for entry in os.listdir(folder):
                if entry.endswith((".h", ".fxc")):
                    shutil.copy(os.path.join(folder, entry), stage)

        def one(item):
            index, values = item
            combo_defines = dict(defines, **{k: str(v) for k, v in values.items()})
            bytecode, warnings = compile_shader(source, combo_defines, d3dx, profile, stage)
            return index, bytecode, warnings

        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            results = sorted(pool.map(one, table))
    finally:
        shutil.rmtree(stage, ignore_errors=True)

    write_vcs_combos(
        os.path.join(OUTPUT_DIR, name + ".vcs"),
        [(i, b) for i, b, _ in results],
        total,
        source_crc(source, build_settings(profile, defines)),
    )
    os.makedirs(INC_DIR, exist_ok=True)
    with open(os.path.join(INC_DIR, name + ".inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write(inc_text(name, combos))
    size = sum(len(b) for _, b, _ in results)
    print("compiled %-32s %3d combos %6d bytes" % (name, len(results), size))
    for warning in sorted({w for _, _, ws in results for w in ws}):
        print("    " + warning)


def verify_model_outputs():
    """Check the .vcs and .inc files of every model shader; returns [(name, problem or None)]."""
    results = []
    for name in MODEL_SHADERS:
        source = name + ".fxc"
        path = os.path.join(OUTPUT_DIR, name + ".vcs")
        inc = os.path.join(INC_DIR, name + ".inc")
        if not os.path.isfile(path) or not os.path.isfile(inc):
            results.append((name, "missing - run tools/build_shaders.py"))
            continue
        try:
            info = read_vcs(path)
            combos, skips = parse_combos(source)
            table, total = combo_table(combos, skips)
        except UNREADABLE as e:
            results.append((name, "unreadable: %s" % e))
            continue
        profile, defines, token = model_profile(name)
        ids = [combo_id for static_id, combo_id, _ in info["combos"] if static_id == 0]
        with open(inc, encoding="utf-8") as f:
            inc_current = f.read() == inc_text(name, combos)
        if info["crc"] != source_crc(source, build_settings(profile, defines)):
            results.append((name, "out of date - %s changed, run tools/build_shaders.py" % source))
        elif ids != [index for index, _ in table] or info["dynamic"] != total:
            results.append((name, "combos do not match the header of %s" % source))
        elif any(code[:4] != token for _, _, code in info["combos"]):
            results.append((name, "holds code for another shader model"))
        elif not inc_current:
            results.append((name, "%s.inc is out of date - run tools/build_shaders.py" % name))
        else:
            results.append((name, None))
    return results


def verify_outputs():
    """Check every generated .vcs file; returns [(name, problem or None)]."""
    results = []
    for name, source, defines in SHADERS:
        path = os.path.join(OUTPUT_DIR, name + ".vcs")
        if not os.path.isfile(path):
            results.append((name, "missing - run tools/build_shaders.py"))
            continue
        try:
            info = read_vcs(path)
        except UNREADABLE as e:
            results.append((name, "unreadable: %s" % e))
            continue
        combos = info["combos"]
        if len(combos) != 1 or combos[0][2][:4] != PS_2_B_TOKEN:
            results.append((name, "does not hold exactly one ps_2_b shader"))
        elif info["crc"] != source_crc(source, build_settings(PROFILE, defines)):
            results.append((name, "out of date - %s changed, run tools/build_shaders.py" % source))
        else:
            results.append((name, None))
    return results


def main():
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--d3dx", help="path to d3dx9_33.dll (Mapbase: src/devtools/bin/d3dx9_33.dll)")
    parser.add_argument("--verify", action="store_true", help="only check the existing .vcs files")
    parser.add_argument("--only", nargs="+", metavar="NAME", help="compile only these shaders (output names)")
    args = parser.parse_args()
    wanted = set(args.only or [])
    unknown = wanted - {name for name, _, _ in SHADERS} - set(MODEL_SHADERS)
    if unknown:
        parser.error("unknown shader(s): " + ", ".join(sorted(unknown)))

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    if not args.verify:
        d3dx = find_d3dx(args.d3dx)
        for name, source, defines in SHADERS:
            if wanted and name not in wanted:
                continue
            crc = source_crc(source, build_settings(PROFILE, defines))
            bytecode, warnings = compile_shader(source, defines, d3dx)
            write_vcs(os.path.join(OUTPUT_DIR, name + ".vcs"), bytecode, crc)
            print("compiled %-32s %5d bytes" % (name, len(bytecode)))
            for w in warnings:
                print("    " + w)

        for name in MODEL_SHADERS:
            if not wanted or name in wanted:
                build_model_shader(name, d3dx)

    problems = verify_outputs() + verify_model_outputs()
    for name, problem in problems:
        print("verified %-32s %s" % (name, problem or "ok"))
    if any(problem for _, problem in problems):
        sys.exit(1)


if __name__ == "__main__":
    main()
