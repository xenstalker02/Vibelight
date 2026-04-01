# Changelog

## [Unreleased]

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
- Cross-platform SDL2 audio design — portable to Linux, macOS, and Android
- Non-blocking atomic stop flag for SDL audio callback thread

### Fixed
- MicCapture disconnect hang — SDL_CloseAudioDevice now returns immediately
  because the callback checks m_StopFlag before processing audio

### Changed
- Steam library entry renamed from 'Moonlight' to 'Vibelight'
- Launch wrapper updated to use user-installed Flatpak (branch: master)
- System stock Moonlight Flatpak removed to avoid version conflicts
