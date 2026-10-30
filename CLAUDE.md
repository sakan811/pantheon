# CLAUDE.md

Rules for the repository as a whole. Each plugin has its own CLAUDE.md in `plugins/<name>/` (Claude Code reads it when you
work in that folder); it owns that plugin's architecture, commands and invariants. This file owns only what crosses plugins.
The same rule as in the plugins applies here: every fact lives in one place, the others link to it.

## Layout and dependency direction

```
shared/ui/       the hardware-style interface kit (JUCE GUI only, header-only), target `pantheon_ui`
plugins/<name>/  one self-contained plugin each: its own CMakeLists.txt, core/, tests, README, CLAUDE.md
.github/         CI for the whole repository (workflows run per plugin with `working-directory`)
```

- Dependencies point one way: `plugins/*` → `shared/*`, never the reverse, and never from one plugin to another. If two
  plugins need the same code, it moves to `shared/` first and both use it from there.
- `shared/` knows nothing about any plugin: no plugin types, parameters, verdicts or words in it. What a plugin adds on top
  (its name, its number formats, its verdict colours) lives in that plugin; Themis' is `plugin/Source/ThemisUi.h`.
- A plugin builds from its own folder with the commands in its README (`cmake -S . -B build` there); it pulls `shared/` in
  by relative path. There is no root CMake project yet; add one when the second plugin joins.

## shared/ui

- It owns every pixel of the interface: [Hardware.h](shared/ui/include/pantheon/ui/Hardware.h) (`colours::`, fonts, `ui::hw`
  panels, glass, lamps, buttons, knobs), [HardwareLookAndFeel.h](shared/ui/include/pantheon/ui/HardwareLookAndFeel.h) and
  [MeterCell.h](shared/ui/include/pantheon/ui/MeterCell.h). Plugins call them; they do not draw their own panels, lamps or
  buttons. The namespaces (`colours`, `ui`, `ui::hw`) are kept short on purpose.
- Everything is drawn from code: no image or font files, nothing random at run time.
- **A change here changes every plugin that uses it.** Build all of them, and regenerate and look at each plugin's
  screenshots (the command is in that plugin's README). A look change that should be only one plugin's belongs in that
  plugin, not in the kit.

## Licence and CI

- AGPL-3.0-or-later throughout, source only, no binaries or releases of compiled plugins. New source files start with the
  `SPDX-License-Identifier` and `Copyright` lines; the existing ones say "The Themis authors", which is to be replaced by one
  name for the whole repository (open item).
- Third-party code needs a compatible licence and an entry in that plugin's NOTICE.md. Nothing is merged into `plugins/` from
  another repository before its licence is known.
- CI runs per plugin; workflows are SHA-pinned with read-only tokens. Do not add a release or artifact upload.
