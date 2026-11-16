# Phorepoc Etesian (S60 port)

The [Etesian](https://github.com/etesian-design/design) design language (the N9's look, re-envisioned) for the Symbian^3 UI,
installable from the Catalogue as a SIS. It is **not** part of the base image and nothing in the phone needs it: remove it and
the stock theme is back.

Status: early. This repo will become `etesian-ui`, the full Etesian shell (own home/launcher/switcher on Avkon, installed as a shell through core's shell selector like orbit-ui and hildon-ui). So far it holds the skin asset: `etesian-x32.sis` is a data-only package with one skin ("Etesian", dark mode): Etesian colour
tables (white-on-black text, the default accent `#4591FF`, warning red) and flat black screen / status-bar backgrounds. Items it
does not define fall back to the base skin. Not yet booted on an image; see the port plan in the private notes for the phases
(light/dark variants, squircle frames, icons, Fira type, layout pack, shell companions).

Build (the pdk's package.yml does this from `package.conf`; locally in a `ghcr.io/phorepoc/base` container):

    PHE_PALETTE=<ThirdPartyBitmap.pal> PHOREPOC_PDK=<pdk> PHOREPOC_BUILD=<out> bash recipes/sis.sh

`tools/gen_descriptor.py` writes `skin/etesian-dark.txt` (UTF-16, the compiler's format) from the Etesian tokens;
`tools/build_skin.py` runs the host `akn_skin_desc_compiler` and `mifconv`.

Licence: Apache-2.0 (see `LICENSES/`, `NOTICE`). Etesian tokens are MIT.
