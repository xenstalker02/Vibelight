# Changelog

## [Unreleased]

### Added
- Mic passthrough — captures default audio input via SDL2 and sends as
  0x3003 control stream packets to Vibepollo host in real time
- Cross-platform SDL2 audio design — portable to Linux, macOS, and Android
- Non-blocking atomic stop flag for SDL audio callback thread
- Stereo-to-mono downmix in handleAudioData for PipeWire/SteamOS compatibility
- Oversized packet guard (kMaxPacketSize=200) prevents SIGABRT on VBR peaks
- Deadline-based encoder pacing (20ms intervals with re-sync guard)
- `micCapture` preference — enable/disable mic passthrough in Settings
- `micDevice` preference — specify capture device name (empty = system default)
- `micBitrate` preference — Opus bitrate (default 96000)
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
