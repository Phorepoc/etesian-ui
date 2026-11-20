#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: AGPL-3.0-only
#
# sis.sh -- the pdk's package.yml runs it after the components (package.conf): the Etesian shell as $PHOREPOC_BUILD/sis/etesian-ui-<arch>.sis (arm or x32).
# Contents: Home (etesianhome.exe + its resources and registration), the launcher (etesianlaunch.exe), the shell description file that
# core's shell selector reads (\resource\phe\shells\etesian.txt), the Etesian skin, the licences. Nothing in an image; nothing that
# replaces a stock file (so no \resource\phe\eclipse\ list: the safe-boot guard has nothing to disable). Removing the package removes the
# shell file; a choice that is not there falls back to S^3.
# Needs the ghcr.io/phorepoc/base host tools (akn_skin_desc_compiler, mifconv), $PHOREPOC_PDK (make_sis, svgtbinencode) and the S60
# palette (PHE_PALETTE, ThirdPartyBitmap.pal from the baseline layer) for the skin.
set -eu
ARCH=${PHOREPOC_ARCH:-x32}
case "$ARCH" in arm) AFL=--arm;; x32) AFL=;; *) echo "ARCH must be arm or x32 (got $ARCH)"; exit 1;; esac
REPO=$(cd "$(dirname "$0")/.." && pwd)
# the S60 palette from the Symbian source the build already has (PHOREPOC_SYMBIAN); PHE_PALETTE overrides
PHE_PALETTE=${PHE_PALETTE:-${PHOREPOC_SYMBIAN:-}/symbian-2010/oss.FCL.sf.mw.uitools/uitools_pub/third_party_bitmap_palette_api/inc/ThirdPartyBitmap.pal}
[ -f "$PHE_PALETTE" ] || { echo "no ThirdPartyBitmap.pal at $PHE_PALETTE (set PHE_PALETTE)"; exit 1; }
export PHE_PALETTE
export PHE_SVGTENC=${PHE_SVGTENC:-$PHOREPOC_PDK/tools/svgtenc/svgtbinencode}
UID_SKIN=E7016B31
OUT=$PHOREPOC_BUILD/skin-gen
python3 "$REPO/tools/build_skin.py" etesian-dark "$OUT"
FILES=()
FILES+=(--file "$REPO/shell/etesian.txt=!:/resource/phe/shells/etesian.txt")
for t in apps hides assoc; do
  FILES+=(--file "$REPO/shell/etesian.$t=!:/resource/phe/shells/etesian.$t")
done
# the default theme's icon sources and their licence stay in the repository; the compiled theme is the skin below
FILES+=(--file "$OUT/etesian-dark.skn=!:/private/10207114/import/$UID_SKIN/etesian-dark.skn")
for ext in mif mbm; do
  [ -f "$OUT/etesian-dark.$ext" ] && FILES+=(--file "$OUT/etesian-dark.$ext=!:/resource/skins/$UID_SKIN/etesian-dark.$ext")
done
# Fira Sans (SIL OFL 1.1, Mozilla and Telefonica), unmodified, with its licence: the font store loads the fonts folder of every drive at start-up
for f in FiraSans-Light FiraSans-Regular FiraSans-Bold; do
  FILES+=(--file "$REPO/fonts/$f.ttf=!:/resource/fonts/$f.ttf")
done
FILES+=(--file "$REPO/fonts/OFL.txt=!:/resource/etesian/licences/OFL-FiraSans.txt")
for f in "$REPO/LICENSES/AGPL-3.0-only.txt" "$REPO/NOTICE"; do
  FILES+=(--file "$f=!:/resource/etesian/licences/$(basename "$f")")
done
mkdir -p "$PHOREPOC_BUILD/sis"
python3 "$PHOREPOC_PDK/tools/make_sis.py" etesianhome etesianlaunch etesianshell etesianphone $AFL --unsigned --name "Etesian" --vendor Phorepoc \
  --version "${PHOREPOC_PACKAGE_VERSION:-0.1.0}" "${FILES[@]}" --out "$PHOREPOC_BUILD/sis/etesian-ui-$ARCH.sis"
