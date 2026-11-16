#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: AGPL-3.0-only
#
# sis.sh -- the pdk's package.yml runs it (package.conf): the Etesian skin as $PHOREPOC_BUILD/sis/etesian-<arch>.sis.
# A data-only package (no code, no ROM stub, no eclipsing): the skin (.skn), its icon/bitmap files and the licence texts.
# Removing it leaves the stock Symbian^3 theme (the base image carries its own skins). Needs: the ghcr.io/phorepoc/base
# host tools (akn_skin_desc_compiler, mifconv), $PHOREPOC_PDK (make_sis, svgtbinencode) and the S60 palette
# (PHE_PALETTE, from the baseline layer: ThirdPartyBitmap.pal).
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
: "${PHE_PALETTE:?set PHE_PALETTE to ThirdPartyBitmap.pal}"
export PHE_SVGTENC=${PHE_SVGTENC:-$PHOREPOC_PDK/tools/svgtenc/svgtbinencode}
UID_SKIN=E7016B31
OUT=$PHOREPOC_BUILD/skin-gen
python3 "$REPO/tools/build_skin.py" etesian-dark "$OUT"
FILES=()
FILES+=(--file "$OUT/etesian-dark.skn=!:/private/10207114/import/$UID_SKIN/etesian-dark.skn")
for ext in mif mbm; do
  [ -f "$OUT/etesian-dark.$ext" ] && FILES+=(--file "$OUT/etesian-dark.$ext=!:/resource/skins/$UID_SKIN/etesian-dark.$ext")
done
for f in "$REPO/LICENSES/AGPL-3.0-only.txt" "$REPO/NOTICE"; do
  FILES+=(--file "$f=!:/resource/etesian/licences/$(basename "$f")")
done
mkdir -p "$PHOREPOC_BUILD/sis"
python3 "$PHOREPOC_PDK/tools/make_sis.py" --data-only --uid 0xE7016B30 --name "Etesian theme" --vendor Phorepoc \
  --version "${PHOREPOC_PACKAGE_VERSION:-0.1.0}" --unsigned "${FILES[@]}" --out "$PHOREPOC_BUILD/sis/etesian-x32.sis"
