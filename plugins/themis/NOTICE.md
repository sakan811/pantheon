# Notice

## Copyright and licence

Copyright (C) 2026 The Themis authors.

Themis is free software: you can redistribute it and/or modify it under the terms of the **GNU Affero General Public
License, version 3 or (at your option) any later version**. The full text is in [LICENSE](LICENSE).

Themis is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the licence for details. In particular, Themis only measures
audio and gives advice from those measurements; it is a monitoring aid, not a certification or a guarantee that a master
will pass any delivery specification, and you are responsible for the decisions you take with it.

## This repository is source only

No compiled plugin is published from this repository. Releases (version tags such as v1.0.0, with notes) are source only: the
source archives GitHub generates for a tag, and no attached binaries. To use Themis, build it yourself (see
[README.md](README.md), "Build and install"). If you give a build to somebody else, the AGPL applies to you: you must offer them the
corresponding source code of what you built, including your changes.

## Third-party software

Themis' own source (everything under `core/`, `plugin/` and `tests/`) is original work. No third-party source code, fonts,
images, samples or presets are copied into this repository: the interface is drawn from code, and the built-in target
curves and device responses are hand-authored approximations.

**JUCE** (<https://juce.com>) is downloaded at configure time by CMake (`FetchContent`, tag 8.0.6) and is **not**
redistributed in this repository. JUCE is dual-licensed under the AGPLv3 and a commercial licence; Themis uses it under the
AGPLv3, which is why Themis is itself licensed under the AGPLv3. If you want to use Themis' code in a closed-source
product you need a JUCE commercial licence and the permission of Themis' copyright holders. JUCE bundles further components
(the VST3 SDK, the Audio Unit SDK, FLAC, Ogg Vorbis, zlib, HarfBuzz and others) under their own licences; they are listed in
JUCE's own [LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) and apply to any binary you build.

## Trademarks

Product and company names are the property of their owners and are used here only to describe what Themis works with or
what a statement is about. Themis is **not affiliated with, endorsed by or sponsored by** any of them.

- VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
- Audio Units, macOS and Apple Silicon are trademarks of Apple Inc.
- JUCE is a trademark of Raw Material Software Limited.

"Themis Verdict" is the name of this plugin ("Themis" for short in the code). If you publish a fork or a build, give it a name of your own where that could be
confused with another product.
