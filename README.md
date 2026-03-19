# Vibelight

Steam Deck Moonlight client fork with mic passthrough. Fork of [Moonlight Game Streaming](https://github.com/moonlight-stream/moonlight-qt).

**Status:** Working on Steam Deck. Multi-platform support planned.

Pairs with **[Vibepollo](https://github.com/xenstalker02/Vibepollo)** on Windows.

Built with [Claude Code](https://claude.ai/claude-code).

---

## Features

- All standard Moonlight streaming features (H.264/HEVC/AV1, HDR, gamepad, mouse/keyboard)
- **Mic passthrough**: captures mic audio from the Steam Deck, encodes with Opus, and streams it to Vibepollo as a virtual microphone input on the host PC
- Works with any audio input device set as the Steam Deck system default: internal mic, headset mic, USB mic, Bluetooth mic — no configuration required
- Cross-platform SDL2 audio capture: the same code works on Linux, Windows, macOS, and Android (SDL2 handles platform differences transparently)

---

## Requirements

- Steam Deck (other Linux platforms may work but are untested)
- [Vibepollo](https://github.com/xenstalker02/Vibepollo) running on a Windows PC

---

## Installation (Flatpak build from source)

Vibelight is distributed as a Flatpak built from the `vibelight.json` manifest.

### Prerequisites

```bash
flatpak install flathub org.flatpak.Builder
```

### Build and install

```bash
# Clone the repo
git clone --recurse-submodules https://github.com/xenstalker02/Vibelight.git
cd ..

# Build and install as user Flatpak
flatpak run org.flatpak.Builder --user --install --force-clean vibelight-build vibelight.json
```

### Add to Steam as a non-Steam game

1. In Steam (Desktop Mode), click **Add a Game** > **Add a Non-Steam Game**
2. Click **Browse**, navigate to `/home/deck/.local/share/flatpak/exports/bin/`
3. Select `com.moonlight_stream.Moonlight`
4. In **Launch Options** add: `XDG_RUNTIME_DIR=/run/user/1000 %command%`

---

## Usage

1. Open Vibelight from your Steam library
2. Vibelight discovers Vibepollo on your local network automatically
3. Add the host and pair when prompted
4. Launch a streaming app — mic passthrough activates automatically
5. Disconnect cleanly from the in-game overlay; no force-quit required

---

## Mic Passthrough

The mic capture system uses SDL2's default capture device — whatever is set as the system default audio input in Steam Deck settings. This includes:

- Steam Deck internal microphone
- USB headset microphone
- Bluetooth headset microphone (when connected and set as default)
- Any other SDL2-supported capture device

No configuration is needed. If you want to use a different mic, set it as the default in the Steam Deck audio settings before starting a stream.

On the Windows side, Vibepollo writes the decoded audio into VB-Audio Virtual Cable, which appears as a standard microphone to any Windows application.

---

## Known Issues / WIP

- Audio quality: occasional glitches under investigation (buffer tuning on host side)
- Clean disconnect fix: `std::atomic` stop flag prevents SDL_CloseAudioDevice hang on teardown
- Only tested on Steam Deck; other Linux platforms may need SDL2 audio configuration

---

## Future Plans

- Windows client support (WASAPI native path or SDL2)
- macOS client support (CoreAudio via SDL2 — likely already works)
- Android client support (AAudio/OpenSL via SDL2)
- Per-platform mic quality profiles


---

## What's Different from Moonlight-Qt

| Feature | Moonlight-Qt | Vibelight |
|---------|-------------|-----------|
| Mic passthrough | No | Yes (Opus-encoded, sent via control stream) |
| HOME/AWAY labeling | No | Yes (via moonlight_wake.sh) |
| Auto WOL + IP switching | No | Yes (LAN vs Tailscale detection) |
| micDevice config | No | Yes (select specific SDL capture device) |
| micBitrate config | No | Yes (configurable Opus bitrate) |

---

## Configuration Options

Add these to `~/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf`:

| Option | Default | Description |
|--------|---------|-------------|
| `micCapture=` | `false` | Set to `true` to enable mic passthrough |
| `micDevice=` | *(empty)* | SDL audio device name. Empty = system default |
| `micBitrate=` | `96000` | Opus bitrate in bps. Range: 6000--510000 |

---

## Quick Install

```bash
git clone https://github.com/xenstalker02/Vibelight ~/Vibelight
cd ~/Vibelight && bash install.sh
```

See [INSTALL.md](INSTALL.md) for full instructions.

---

## Moonwake

The `moonlight_wake.sh` script provides automatic server discovery and wake-on-LAN:
- Detects home LAN vs remote network (e.g., Tailscale)
- Sets the correct server IP in Moonlight.conf
- Labels the server as HOME or AWAY
- Sends Wake-on-LAN packet if the PC is asleep

Point your Steam shortcut at `moonlight_wake.sh run` for the full experience.

---

## Platform Support

| Platform | Status |
|----------|--------|
| Steam Deck (SteamOS) | Tested, primary target |
| Linux (other) | Should work, untested |
| Windows | Planned (SDL2 or WASAPI path) |
| macOS | Planned (CoreAudio via SDL2) |
| Android | Planned (AAudio via SDL2) |


## Related Projects
- [logabell/moonlight-qt-mic](https://github.com/logabell/moonlight-qt-mic) -- parallel mic passthrough client (deadline pacer and Opus tuning reference)
- [moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) -- upstream Moonlight client
- [Vibepollo](https://github.com/xenstalker02/Vibepollo) -- companion server fork for Vibelight
