# Pantheon

A monorepo of mastering and music plugins named after Greek gods, built with [JUCE](https://juce.com) 8 and licensed
under the AGPL-3.0-or-later. It holds source only: no binaries or downloads.

| Plugin | What it is |
|---|---|
| [Themis Verdict](plugins/themis/README.md) | A mastering check suite: tonal balance, translation, phase, clipping, loudness and a plan of what to do next. |

The other plugins (Hephaestus, Harmonia) are being moved in. Each plugin keeps its own README, build commands and
contributor rules in `plugins/<name>/`; this file only lists what is in the repository. CI lives in
[.github/](.github) and runs per plugin.
