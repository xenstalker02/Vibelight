# Vibelight

> **Vibelight** is a fork of
> [moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt)
> that adds **client-side microphone capture and passthrough** for use with
> [Vibepollo](https://github.com/xenstalker02/Vibepollo) on the Windows host.

[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

---

## What Is This?

Moonlight streams games from your PC to your Steam Deck but ignores the Steam Deck
microphone. Vibelight fixes that by capturing mic audio on the Steam Deck, encoding it
with Opus, and streaming it back to the host PC where it appears as a virtual microphone
that Discord, games, and voice chat apps can use normally.

Vibelight is the client side. It pairs with
**[Vibepollo](https://github.com/xenstalker02/Vibepollo)** on the Windows host.

---

## Features

- **Mic passthrough** — captures Steam Deck mic, Opus-encodes it, streams to host
- **Encrypted transport** — mic data rides the AES-GCM encrypted control stream.
  The host refuses unencrypted mic sessions.
- **Opus audio** — 64kbps VBR, complexity 10, FEC enabled, explicit 20ms frame duration.
  Optimized for voice quality and packet loss resilience.
- **Deadline-based send pacer** — sends frames at exactly 20ms intervals with re-sync
  guard. Eliminates jitter from SDL2 timer irregularities for clean audio.
- **12-frame buffer cap** — prevents audio backup during capture irregularities,
  always dropping oldest frames to keep latency low.
- **Device fallback** — if a named device fails, automatically falls back to default mic
- **Frame spec mismatch detection** — detects and logs SDL2 format mismatches without crashing
- **Moonwake integration** — one-tap wake-and-stream from Steam Game Mode with
  HOME/AWAY path detection (LAN vs Tailscale) and Raspberry Pi WOL chain
- **All Moonlight features** — video streaming, HDR, controller support, and everything
  from upstream Moonlight

---

## Requirements

- Steam Deck (SteamOS) — primary supported platform
- [Vibepollo](https://github.com/xenstalker02/Vibepollo) running on Windows host
- Linux desktop: planned (SDL2 is cross-platform, packaging needed)

---

## Installation (Steam Deck)

One command:

```bash
curl -sSL https://raw.githubusercontent.com/xenstalker02/Vibelight/master/install.sh | bash
```

Or clone and install:

```bash
git clone https://github.com/xenstalker02/Vibelight.git
cd Vibelight
bash install.sh
```

The installer builds and installs the Flatpak with mic capture enabled.
Safe to re-run on update — fully idempotent.

---

## Configuration

Edit the Moonlight config file at:
`~/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf`

| Option | Default | Description |
|--------|---------|-------------|
| `micCapture` | `true` | Enable mic capture and passthrough |
| `micDevice` | (empty) | Specific mic device name. Leave empty for default. |
| `micBitrate` | `64000` | Opus bitrate in bps (6000-510000) |
| `absoluteMouseMode` | `false` | Set false for Steam trackpad mouse compatibility |
| `mouseAcceleration` | `false` | Set false for consistent pointer feel |

---

## Security

Mic audio is sent over the AES-GCM encrypted Moonlight control stream.
The host (Vibepollo) refuses to render mic audio from clients that did not negotiate
an encrypted session. No plaintext mic audio is ever transmitted.

---

## How It Works

```
Steam Deck mic (or USB/Bluetooth mic)
→ SDL2 capture (48kHz, 16-bit, mono)
→ 12-frame ring buffer (jitter smoothing)
→ Opus encode (64kbps VBR FEC complexity-10 FRAMESIZE_20_MS)
→ Deadline-based pacer (20ms intervals, re-sync guard)
→ AES-GCM encrypted control stream
→ Vibepollo decodes and renders to Steam Streaming Microphone
→ Windows voice apps work normally
```

---

## Moonwake

Vibelight includes a wake-and-stream system (`moonlight_wake.sh`) that:
1. Detects if you are HOME (LAN) or AWAY (remote/Tailscale)
2. Sets the correct host address in Moonlight config automatically
3. Wakes the PC via WOL through a Raspberry Pi if it is asleep
4. Launches Moonlight automatically after PC is ready

One tap in Steam Game Mode triggers the whole chain.

---

## Platform Support

| Platform | Status |
|----------|--------|
| Steam Deck (SteamOS) | Supported |
| Linux desktop | Planned |
| Windows | Not planned |
| macOS | Not planned |

---

## Related Projects

| Project | Description |
|---------|-------------|
| [Vibepollo](https://github.com/xenstalker02/Vibepollo) | Companion server-side fork |
| [logabell/moonlight-qt-mic](https://github.com/logabell/moonlight-qt-mic) | Parallel client mic implementation |
| [moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) | Upstream Moonlight |
| [ClassicOldSong/Apollo](https://github.com/ClassicOldSong/Apollo) | Apollo (server) upstream |

---

## Acknowledgments

Mic passthrough was developed in parallel with
[logabell/moonlight-qt-mic](https://github.com/logabell/moonlight-qt-mic).
We adopted Opus encoder tuning (64kbps, FEC, VBR, complexity 10, FRAMESIZE_20_MS)
and the deadline-based send pacer from that work after comparing implementations.
