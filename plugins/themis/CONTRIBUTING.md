# Contributing

Issues and pull requests are welcome. Please read [CLAUDE.md](CLAUDE.md) first: it describes the principles the code follows
(Themis only measures and never processes the audio, advice is computed from measurements and never invented, one place
for every threshold) and the commands to build and test.

- `make test` must pass, and the plugin target must stay warning-free in Themis' own sources.
- A change that touches the interface needs refreshed `docs/screenshot.png` and `docs/graphs.png` (see README, "Develop").
- Only submit code you wrote yourself, or that comes with a licence compatible with the AGPL; do not paste code, fonts or
  images from other products. Do not add presets or curves taken from commercial products.
- By submitting a contribution you agree that it is licensed under the same terms as the project,
  **AGPL-3.0-or-later** (see [LICENSE](LICENSE)).
