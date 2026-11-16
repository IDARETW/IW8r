"""Offline proof: a built map whose surface shader reads the scene depth (iw8rSceneDepth) binds the game's scene depth.

Reads the built techset zone back with ACTS and checks, for every lit technique whose pixel shader loads from a
texture the technique gives a code image argument for:
  - the technique has exactly one code image argument (MTL_ARG_CODE_TEXTURE) for the pixel stage, and it names code
    image 0 (Replay's "Frontend FloatZ", research 104), not 79 (the thermal mask of the stock layout);
  - the pixel shader declares a texture at that argument's register and reads it with `ld` (a Load, as the stock
    effect shaders read the scene depth);
  - at least one lit technique does so (the map's shader is in the build).

Usage: py -3 -I tools/scene_depth_build_test.py <package folder> [--work <scratch folder>] [--expect-none] [--stock] [--min N]
  <package folder> holds techsets_<map>.ff (the editor's x64zt_output/<map>). --expect-none: the map has no such
  shader, every code image argument must still be 79.
"""
import argparse
import ctypes
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r"E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"
OODLE = r"E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"


def disassemble(blob):
    at = blob.find(b"DXBC")
    if at < 0:
        return None
    blob = blob[at:]
    dll = ctypes.WinDLL("d3dcompiler_47.dll")
    fn = dll.D3DDisassemble
    fn.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    fn.restype = ctypes.c_long
    buf = ctypes.create_string_buffer(blob, len(blob))
    out = ctypes.c_void_p()
    if fn(buf, len(blob), 0, None, ctypes.byref(out)) < 0:
        return None
    vt = ctypes.cast(ctypes.cast(out, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    p = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])(out)
    n = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])(out)
    return ctypes.string_at(p, n).decode("latin-1")


def walk_args(node, found):
    """Collects every argument object {type, shader, dest, index} below a technique."""
    if isinstance(node, dict):
        if "dest" in node and "index" in node and ("type" in node or "shader" in node):
            found.append(node)
        for v in node.values():
            walk_args(v, found)
    elif isinstance(node, list):
        for v in node:
            walk_args(v, found)


def arg_type(a):
    t = a.get("type")
    if isinstance(t, dict):
        return t.get("name", "")
    return str(t)


def techniques(node, out, name=""):
    """Yields (technique name, technique object): objects that hold an 'args' list and shader references."""
    if isinstance(node, dict):
        nm = node.get("name")
        if isinstance(nm, dict):
            nm = nm.get("string")
        if not isinstance(nm, str):
            nm = name
        if "args" in node:
            out.append((nm, node))
        for v in node.values():
            techniques(v, out, nm)
    elif isinstance(node, list):
        for v in node:
            techniques(v, out, name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("package")
    ap.add_argument("--work")
    ap.add_argument("--expect-none", action="store_true")
    ap.add_argument("--stock", action="store_true",
                    help="a rebuilt stock techset zone (File > Rebuild Stock Zone with a screen filter): its lit "
                         "techniques keep their other code images; those re-pointed to the scene depth are checked")
    ap.add_argument("--min", type=int, default=1, help="the least number of techniques that must read the scene depth")
    a = ap.parse_args()
    ffs = glob.glob(os.path.join(a.package, "techsets_*.ff"))
    if len(ffs) != 1:
        print("FAIL: need one techsets_*.ff in", a.package)
        return 2
    work = a.work or tempfile.mkdtemp(prefix="iw8r_sd_")
    out = os.path.join(work, "acts")
    shutil.rmtree(out, ignore_errors=True)
    subprocess.run([ACTS, "--noUpdater", "fastfile", "-r", "mw19replay", "-g", REPLAY, "--oodle", OODLE, "-a",
                    "techset,pixelshader", "-l", "-o", out, ffs[0]], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    zone = os.path.splitext(os.path.basename(ffs[0]))[0]
    zdir = os.path.join(out, "mw19replay", zone, "assets")
    shaders = {}
    for f in glob.glob(os.path.join(zdir, "pixelshader", "**", "*.cso"), recursive=True):
        shaders[os.path.basename(f)] = f
    sets = glob.glob(os.path.join(zdir, "techset", "**", "*.json"), recursive=True)
    if not sets or not shaders:
        print("FAIL: ACTS exported %d techset(s) and %d pixel shader(s)" % (len(sets), len(shaders)))
        return 2
    lit = bound = unread = own = 0
    bad = []
    for f in sets:
        j = json.load(open(f, encoding="utf-8", errors="replace"))
        techs = []
        techniques(j, techs)
        for name, t in techs:
            text = json.dumps(t)
            if "LIT_FORWARDPLUS" not in (name or "") and "LIT_FORWARDPLUS" not in text[:400]:
                continue
            args = []
            walk_args(t.get("args"), args)
            code = [x for x in args if "CODE_TEXTURE" in arg_type(x) and int(x.get("shader", 0)) & 16]
            if not code:
                continue
            lit += 1
            # The technique's pixel shader: the generated surface shader whose name starts with the technique set's
            # hash (tw/mw120r_<map>_<hash> -> mw120r_iw3_surface_<hash>...); stock-named shaders by their own hash.
            tail = re.findall(r"[0-9a-f]{12}", name or "")
            names = [k for k in shaders if k.split(".")[0] in text] or                     [k for k in shaders if tail and ("surface_" + tail[-1]) in k] or                     [k for k in shaders if any(m in k for m in re.findall(r"[0-9a-f]{32}", text))]
            asm = disassemble(open(shaders[names[-1]], "rb").read()) if names else None
            if not asm and not a.expect_none:
                unread += 1  # ACTS did not export this technique's pixel shader: the argument alone is checked
                if not a.stock and any(int(c["index"]) != 0 for c in code):
                    bad.append("%s: code image %s, not the scene depth" % (name, [int(c["index"]) for c in code]))
                continue
            if a.stock:
                # A rebuilt stock zone: only the scene depth arguments count (its shaders have other code images too).
                code = [c for c in code if int(c["index"]) == 0]
            for c in code:
                reg, index = int(c["dest"]), int(c["index"])
                loads = bool(asm) and re.search(r"\bld(_indexable)?\b[^\n]*\bt%d\b" % reg, asm) is not None
                if a.expect_none:
                    if index != 79:
                        bad.append("%s: code image %d at t%d (expected the stock 79)" % (name, index, reg))
                    continue
                if index == 0 and loads:
                    bound += 1
                elif index == 0 and a.stock:
                    own += 1  # a stock technique that had the scene depth before (it samples it its own way)
                elif index == 0:
                    bad.append("%s: code image 0 at t%d, but its pixel shader does not Load from it" % (name, reg))
                elif loads:
                    bad.append("%s: the pixel shader Loads t%d, which is code image %d, not the scene depth" % (name, reg, index))
    if not a.work:
        shutil.rmtree(work, ignore_errors=True)
    print("lit techniques with a code image argument: %d; reading the scene depth (code image 0, Load): %d; "
          "code image 0 with a pixel shader ACTS did not export: %d" % (lit, bound, unread))
    if a.stock:
        print("stock techniques that already read the scene depth their own way: %d" % own)
    for b in bad[:10]:
        print("  " + b)
    if bad or lit == 0 or (not a.expect_none and bound < a.min):
        print("FAIL")
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
