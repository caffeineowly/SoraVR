# Changelog

All notable changes to SoraVR. Format: [Keep a Changelog](https://keepachangelog.com/).
Add a line under *Unreleased* with every fix or pull request (with its number and credit).

## [Unreleased]

### Fixed
- Crash (and, with an intermediate fix, a freeze) on the first frame when the game is started from
  Steam: the Steam overlay is then loaded before SoraVR and had already hooked Present. SoraVR now
  chains its Present hook in front of an existing one instead of changing the swapchain vtable, so
  both work. Starting from the game folder was not affected.
- The proxy d3d11.dll now forwards all of the system d3d11.dll's exports, not only the two the game uses.
- Controller physics is only enabled in `sora_1st.exe` (2nd Chapter's physics interface differs and crashed).

### Added
- `LICENSE` (GPL-3.0-or-later) and SPDX headers in the sources.
- `tools/release.sh`: builds the release zip with the official Khronos OpenXR loader 1.1.65 (checksum
  verified) and writes SHA256SUMS.txt. Zip layout: `d3d11.dll` + `openxr_loader.dll` at the top, docs
  and licences in a `SoraVR` folder - extract into the game folder.
- *Trails in the Sky 2nd Chapter* demo starts in VR (experimental: alternate-eye, no physics).

### Changed
- Game code is now found by byte signatures instead of fixed addresses, so SoraVR works on game
  versions 1.06.1 and 1.07 (and should survive small updates). `tools/sigcheck.py` checks the
  signatures against any `sora_1st.exe`.
- The log names the game build and where each patched piece of game code was found.
- Shaders SoraVR replaces or watches (world labels, letterbox sprites, toon outlines) are recognised
  by their code instead of their hash, and the replacements adapt to the game's data layout. Fixes
  floating labels/markers and too-thick outlines on 1.07. `tools/shaderprint.py` fingerprints a
  hunting dump.
- Default ini rewritten for readability: every setting has its explanation right above it,
  sections have clear headers, old experiments removed. Hotkeys can use names like `Insert` or
  `PageUp`, and some keys have clearer names (`WorldScaleUp`, `Panel`, ...; old names still work).
- Only the values the hotkeys adjust are saved (world scale, FOV, panel size/distance/mode, eye
  swap). On/off keys like F12 (FOV lock) no longer stick between sessions, and the ini no longer
  fills up with old experimental settings.
- README: volumetric lighting named correctly; DLSS/FSR (game 1.07) should be off; tested game versions.

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
