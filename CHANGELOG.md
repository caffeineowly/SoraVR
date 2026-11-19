# Changelog

All notable changes to SoraVR. Format: [Keep a Changelog](https://keepachangelog.com/).
Add a line under *Unreleased* with every fix or pull request (with its number and credit).

## [Unreleased]

### Changed
- Game code is now found by byte signatures instead of fixed addresses, so SoraVR works on game
  versions 1.06.1 and 1.07 (and should survive small updates). `tools/sigcheck.py` checks the
  signatures against any `sora_1st.exe`.
- The log names the game build and where each patched piece of game code was found.

## [1.0-rc2] – 2026-11-19

First public source release. Works with game version 1.06.1.

### Added
- Real stereo VR camera with 6DOF head tracking (both eyes rendered every frame).
- Depth-corrected world elements (labels, markers, speech icons) and live toon-outline correction.
- Automatic wide field of view, locked against cutscene zooms.
- The game's interface on a floating panel, with the mouse cursor on it.
- VR controllers as a pointer (click and drag), shown as glowing orbs.
- Controllers push hair, cloth and ribbons aside.
- Live 3D world behind menus (blurred and dimmed) and 3D menu models with depth.
