Etesian is the Etesian design language (the N9's look, re-envisioned) as a second shell for the Symbian^3 UI. This is an early release.

It installs on top of the standard UI and changes nothing in it. After installing, open the "Etesian shell" app, pick Etesian and
restart: you get the Etesian Home with its app grid, Open apps and events pages, and a dialler; the standard applications keep working
under it. Pick the standard shell the same way to go back. If Etesian does not start, the phone falls back to the standard shell by itself.

Choosing the shell also switches to the Etesian theme (dark, with its own application icons and the Fira Sans typeface); switching back
restores your theme. The theme is built into Phorepoc OS images. Removing the package removes the shell.

Apache-2.0. The licences are installed in `\resource\etesian\ui\licences\`.
