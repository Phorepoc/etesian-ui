# Etesian shell

An early second shell for the Symbian^3 UI in the [Etesian](https://github.com/etesian-design/design) design language (the N9's look,
re-envisioned): a home screen with an app grid, Open apps and an events page, and a dialler. It installs on top of the standard UI
(Catalogue: `etesian-ui`) and nothing in the phone needs it.

- Open the **Etesian shell** app, pick Etesian and restart. Pick Symbian^3 the same way to go back.
- If Etesian does not start twice in a row, the phone falls back to Symbian^3 by itself.
- Choosing the shell activates the **Etesian theme** and switching back restores the theme you had. The theme and its typeface (Fira Sans)
  are not part of this package: the Phorepoc OS images carry them
  ([etesian-theme](https://github.com/Phorepoc/etesian-theme), built into the ROM); on other phones install the "Etesian theme" package first.
- Removing the package removes the shell. Remove it while Symbian^3 is chosen if you can; if Etesian is running the phone falls back to Symbian^3.

Build (the pdk's package.yml does this from `package.conf`; `release.yml` publishes `etesian-ui-arm.sis` and `etesian-ui-x32.sis`):

    PHOREPOC_PDK=<pdk> PHOREPOC_BUILD=<out> bash recipes/sis.sh

Licence: Apache-2.0 (see `LICENSES/`, `NOTICE`).
