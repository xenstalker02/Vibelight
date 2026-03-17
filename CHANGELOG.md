# Changelog

## [Unreleased]

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
