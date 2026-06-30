# Changelog

## [1.1.7] — 2026-06-30

### Fixed
- **Install failed on a fresh Steam Deck** — `install.sh` ran `flatpak run org.flatpak.Builder`
  without installing it first, so a clean Deck failed with `app/org.flatpak.Builder/x86_64/… not
  installed`. The installer now adds the Flathub remote and installs `org.flatpak.Builder` before
  building. (Reported in #29.)

## [1.1.6] — 2026-05-04

### Fixed
- **HOME/AWAY conf update silently skipped** — `moonlight_wake.sh` set `MOONLIGHT_CONF` to `Vibelight.conf`, but the installed Flatpak binary uses `QCoreApplication::setApplicationName("Moonlight")` (pre-rename), so Qt writes all settings — including `srvcert` after pairing — to `Moonlight.conf`. Both `is_paired()` and `update_moonlight_conf()` were reading/writing the wrong file. `is_paired()` always saw an empty cert and returned false, so HOME/AWAY hostname labels and `mdns=false` were never applied. Fix: changed `MOONLIGHT_CONF` to `Moonlight.conf`.
- **Fallback HOME detection false-positive on external networks** — `detect_network` stored-IP fallback checked `dev != tailscale0` to confirm same-LAN, but from hotspot/hotel/coffee shop the route for the stored home LAN IP goes via the external default gateway (also on `wlan0`). Script falsely declared HOME, sent unnecessary WoL, then waited 60 seconds before falling back to AWAY. Fix: added `route_line != *" via "*` guard — a gateway hop in the route means you're not on the same subnet. Direct home-LAN routes never have `via`. WoL-while-asleep-at-home still works (same-subnet route even when PC is sleeping).
- **`localaddress` never inserted on fresh conf** — `update_moonlight_conf` updated `1\localaddress` if already present, but never inserted it. Fallback detection requires it. Fix: insertion now runs when HOME and key is absent.

## [1.1.5] — 2026-05-04

### Fixed
- **Vibelight exits immediately when launched from Steam** — `moonlight_wake.sh` forwarded `"$@"` to `flatpak run`, passing Steam's spurious `"run"` LaunchOptions argument to Moonlight, which treated it as an unknown subcommand, printed usage, and exited with a non-zero code. Steam displayed this as a crash. Fix: removed `"$@"` from the `exec flatpak run` line in both `moonlight_wake.sh` and the canonical wrapper written by `install.sh`. Root cause found via systemd journal (`Usage: moonlight [options] <action>` on every Steam launch).
- **`--device=dri` breaks gamepad input** — 1.1.3 changed `--device=all` to `--device=dri` to reduce Flatpak device exposure. `--device=dri` does not include `/dev/input/*`, which SDL3's libudev backend requires for controller detection on Steam Deck. Reverted to `--device=all`. A future `--device=dri` + `--device=input` split requires Flatpak ≥ 1.15.6 and is deferred.

## [1.1.4] — 2026-05-04

### Fixed
- **`install.sh` micCapture written to wrong config file** — `CONF` was pointing at
  `Moonlight.conf` but Vibelight reads `Vibelight.conf` (Qt QSettings uses the app
  name for the filename). The mic-capture enable on install was silently a no-op.
  Changed `CONF` to `Vibelight.conf`; mic passthrough now actually activates on fresh
  install.
- **`moonlight_wake.sh` HOME/AWAY renaming silently broken** — `MOONLIGHT_CONF` was
  still set to `Moonlight.conf` (doesn't exist after app rename). Every launch hit
  `moonlight_conf_missing` and returned without updating the IP or hostname label.
  Fixed the path to `Vibelight.conf` and added bak-file pruning (keep 5 most recent).

## [1.1.3] — 2026-05-04

### Security
- **Flatpak `--device=all` → `--device=dri`** — removed over-broad device access.
  `--device=all` granted raw `/dev/input`, `/dev/snd`, and video capture devices
  beyond what GPU rendering requires. `--device=dri` is the minimal grant needed
  for VAAPI/Vulkan hardware decode.

## [1.1.2] — 2026-05-02

### Fixed
- **kMaxPacketSize raised 200 → 248** — true safe ceiling is 252 bytes (ENet tempBuffer)
  minus 4-byte header = 248. Previous 200-byte cap caused VBR bitrate spikes to silently
  drop frames at the encoder boundary without any log output.
- **Encoder thread join ordering** — encoder thread is now joined BEFORE
  `opus_encoder_destroy()` in `MicCapture::start()`. Previous ordering destroyed the
  encoder while the thread could still be encoding, causing a data race on reconnect.
- **install.sh abort on pactl failure** — PipeWire volume set now uses `|| echo`
  fallback instead of aborting under `set -euo pipefail`. Gaming Mode and devices
  with no default source no longer abort the install mid-run.
- **DECK_HOME hardcode removed** — `DECK_HOME="/home/deck"` changed to
  `DECK_HOME="${HOME:-/home/deck}"` to support non-standard Steam Deck user setups.

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
