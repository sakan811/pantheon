# Contributing

Themis is maintained by one person, who writes and merges all the code. **Suggestions are welcome; code contributions are
not accepted.** That keeps the project reviewable by a single maintainer and keeps the principles in [CLAUDE.md](CLAUDE.md)
intact (Themis only measures and never processes the audio, advice is computed from measurements and never invented, one
place for every fact and threshold).

## How to suggest something

Open an issue. Bug reports, ideas for a new check, a limit that looks wrong on your material, wording that confused you and
questions are all fine. A useful suggestion says:

- **What you saw or want**, and what you expected instead. For a bug: the host and version, macOS version, plugin format
  (VST3 / AU) and the steps to reproduce it.
- **For a measurement or a limit:** the audio or the numbers behind it, described rather than attached unless you own the
  rights to the file. Themis' limits are rules of thumb, so a case where one is wrong is exactly the feedback that helps.
- **Why it belongs here:** a feature fits if it helps you *judge* a master (a measurement, a verdict, a graph, advice); it does
  not fit if it *processes* the signal (README, "Core philosophy").

Found a vulnerability? Report it privately, see [SECURITY.md](SECURITY.md).

## Pull requests

Pull requests are not merged. You may open one to show an idea, but expect the maintainer to implement it separately, or to
close it with a note. Please do not spend time polishing one.

If you paste code, a curve or any other material into an issue, do it only if you wrote it yourself or it comes with a licence
compatible with the AGPL; do not paste code, fonts, images, presets or curves from other products. Anything you post that
ends up in Themis is used under the same terms as the project, **AGPL-3.0-or-later** (see [LICENSE](LICENSE)).

## For the maintainer

The checks before a change goes in are in [CLAUDE.md](CLAUDE.md): `make test` passes, the plugin target stays warning-free in
Themis' own sources, the README is updated with any change to what a check does, a limit or a default, and
`docs/screenshot.png` and `docs/graphs.png` are regenerated after a change to the interface (commands: README,
"Development"). Workflow changes keep actions pinned to a commit SHA and the token permissions read-only (README,
"Continuous integration").
