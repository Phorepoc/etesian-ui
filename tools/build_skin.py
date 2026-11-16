#!/usr/bin/env python3
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0
"""build_skin -- compile skin/<name>.txt (+ the SVGs next to it) into <out>/<name>.{skn,mif,mbm} with the pdk toolchain's host tools
(akn_skin_desc_compiler, mifconv; both in the ghcr.io/phorepoc/base image under /opt/symbian-hosttools), the way pdk tools/theme_gen.py does.
    PHE_PALETTE=<ThirdPartyBitmap.pal> PHE_SVGTENC=<svgtbinencode> python3 build_skin.py <name> <outdir>
(in CI both come from the pdk / the layer checkout; see recipes/sis.sh)."""
import os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = os.path.join(HERE, "..", "skin")
def which(n): return shutil.which(n) or os.path.join("/opt/symbian-hosttools", n)
def main(name, out):
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(); stage = os.path.join(tmp, "src"); shutil.copytree(SKIN, stage)
    # the application icons of the theme: an APPICON block per application UID of skin/icons/mapping.txt (guide: SDCReader.cpp ParseAppIcon,
    # "APPICON UID=<app uid>" then one bitmap source per line, "c16 <file>.svg SOFTMASK"); the item is EAknsMajorAppIcon / <uid>, what
    # AknsUtils::CreateAppIconLC looks up first
    mp = os.path.join(stage, "icons", "mapping.txt")
    if os.path.isfile(mp):
        blocks = ""
        for line in open(mp, encoding="utf-8"):
            parts = line.split()
            if len(parts) == 2 and not line.startswith("#") and int(parts[0], 16) != 0 and os.path.isfile(os.path.join(stage, "icons", parts[1] + ".svg")):
                blocks += "\nAPPICON UID=0x%s\n\tc16 icons/%s.svg SOFTMASK\nEND\n" % (parts[0], parts[1])
        d = os.path.join(stage, name + ".txt")
        raw = open(d, "rb").read()
        enc = "utf-16" if raw[:2] in (bytes([0xFF, 0xFE]), bytes([0xFE, 0xFF])) else "latin-1"
        open(d, "w", encoding=enc).write(raw.decode(enc) + blocks)
    os.environ.setdefault("EPOCROOT", tmp + "/")
    os.makedirs(os.path.join(tmp, "epoc32", "build", "s60", "icons", "temp", "tmp"), exist_ok=True)
    r = subprocess.run([which("akn_skin_desc_compiler"), "-m" + out, "-t" + out, name + ".txt", name], cwd=stage, capture_output=True, text=True, errors="replace")
    print(r.stdout[-400:], r.stderr[-400:])
    if r.returncode or not os.path.isfile(os.path.join(out, name + ".skn")): return 1
    lst = open(os.path.join(out, name + "_MIFList.txt"), encoding="latin-1").read()
    lst = re.sub(r"(?m)(^|\s)/(?=\w)", lambda m: m.group(1) + "-", lst).replace("\\", "/")
    open(os.path.join(tmp, "list.txt"), "w", encoding="latin-1", newline="\n").write(lst)
    shutil.copy(os.environ["PHE_PALETTE"], os.path.join(tmp, "palette.pal"))
    r = subprocess.run([which("mifconv"), os.path.join(tmp, name + ".mif"), "-T" + tmp, "-P" + os.path.join(tmp, "palette.pal"),
                        "-S" + os.path.dirname(os.environ["PHE_SVGTENC"]), "-F" + os.path.join(tmp, "list.txt")],
                       cwd=stage, capture_output=True, text=True, errors="replace")
    print("\n".join(l for l in (r.stdout + r.stderr).splitlines() if not l.startswith("Loading"))[-600:])
    for ext in (".mif", ".mbm"):
        if os.path.isfile(os.path.join(tmp, name + ext)): shutil.copy(os.path.join(tmp, name + ext), out)
    ok = os.path.isfile(os.path.join(out, name + ".mif"))
    for f in sorted(os.listdir(out)): print(f, os.path.getsize(os.path.join(out, f)))
    return 0 if ok else 1
if __name__ == "__main__": sys.exit(main(sys.argv[1], sys.argv[2]))
