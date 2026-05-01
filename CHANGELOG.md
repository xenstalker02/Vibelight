# Changelog

## [1.1.1] — 2026-04-30

### Fixed
- **Flatpak build unblocked** — `install.sh` now explicitly passes
  `--install-deps-from=flathub` to `flatpak-builder` so the `gamescope-wsi`
  extension resolves correctly on SteamOS. Previously the build failed with an
  unresolved extension dependency.
- **install.sh robustness** — added `set -euo pipefail` and an explicit Flatpak
  install step with `--noninteractive`; the script now exits cleanly on any
  unexpected error rather than silently continuing.
- **Mic capture clamp** — `miccapture` audio frame size is now clamped to the
  SDL callback buffer length; previously an over-length frame could overflow the
  encoder input buffer.

### Changed
- **Worktree gitlink removed** — stale `.claude` directory gitlink was left in the
  repo by a worktree operation, causing `git submodule update --init` to fail on
  the Deck. Removed; submodule init now runs cleanly.
- Log and dump file naming updated for consistency with Vibepollo conventions.

## [1.1.0] — 2026-04-01

### Added
- DTX (Discontinuous Transmission) enabled — Opus now sends no packets during silence,
  reducing upstream bandwidth from Deck to PC during quiet periods. Server-side PLC
  handles silence gaps gracefully.

### Fixed
- micBitrate default corrected from 96000 to 64000 to match documented intent and
  kDefaultBitrate constant.

### Changed
- install.sh now automatically sets PipeWire capture volume to 50% at install time,
  preventing built-in mic from overdriving the Opus encoder.
- Mic capture checkbox tooltip updated to reflect Steam Streaming Microphone as
  primary backend.

## [Initial Release]

### Added
- Mic passthrough — captures default audio input via SDL2 and sends as
  0x3003 control stream packets to Vibepollo host in real time
- Non-blocking atomic stop flag for SDL audio callback thread
- Stereo-to-mono downmix in handleAudioData for PipeWire/SteamOS compatibility
- Oversized packet guard (kMaxPacketSize=200) prevents SIGABRT on VBR peaks
- Deadline-based encoder pacing (20ms intervals with re-sync guard)
- `micCapture` preference — enable/disable mic passthrough in Settings
- `micDevice` preference — specify capture device name (empty = system default)
- `micBitrate` preference — Opus bitrate (default 64000, reduced from initial 96kbps for optimized VOIP tuning)
- Settings UI: microphone device name field (visible when mic capture enabled)
- install.sh: idempotent — safe to run multiple times without duplicate Steam shortcuts
- Disconnect modal guard: only shows first error per session

### Fixed
- SIGABRT crash when Opus VBR packet exceeded moonlight-common-c buffer limit
- Stereo samples fed to mono encoder causing garbled/pitch-shifted audio
- MicCapture disconnect hang — SDL_CloseAudioDevice now returns immediately
  because the callback checks m_StopFlag before processing audio

### Changed
- Steam library entry renamed from "Moonlight" to "Vibelight"
- Launch wrapper updated to use user-installed Flatpak (branch: master)
- System stock Moonlight Flatpak removed to avoid version conflicts
