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
| `micDevice` | (empty) | Specific mic device name. Leave empty to use the default mic (built-in Steam Deck mic). Set to your Bluetooth or USB mic device name to use an external device. |
| `micBitrate` | `48000` | Opus bitrate in bps (6000-510000). 48000 is the default; raise to 96000 for higher quality. |
| `absoluteMouseMode` | `false` | Set false for Steam trackpad mouse compatibility |
| `mouseAcceleration` | `false` | Set false for consistent pointer feel |

---

## Using Bluetooth Headphones

Vibelight supports using Bluetooth headphones (e.g. Nothing Ear (1), Sony WH-1000XM5, or any
Bluetooth headset) as the microphone source:

1. Pair your headphones on the Steam Deck in Desktop Mode (Settings → Bluetooth)
2. In SteamOS, the Bluetooth headset mic appears as a PipeWire device
3. Find the device name:
   ```bash
   pactl list sources short | grep -i bluetooth
   ```
4. Set `micDevice` in Moonlight.conf to the device name shown (e.g.
   `bluez_input.XX_XX_XX_XX_XX_XX.0`)

If the named device is unavailable at session start, Vibelight automatically falls back
to the default microphone.

---

## AWAY Streaming (Remote / Tailscale)

Vibelight's Moonwake system (`moonlight_wake.sh`) supports streaming from outside your home network:

- **HOME path** (LAN): connects directly at full speed (150Mbps+, HDR). Host is labelled
  in Moonlight with your local address.
- **AWAY path** (Tailscale / remote): the script detects you are not on the home LAN,
  wakes the PC via WOL sent through a Raspberry Pi on the home network, then connects
  via Tailscale. The host entry is labelled **"hyp3r (AWAY)"** in Moonlight to indicate
  the remote path is active.

One tap in Steam Game Mode triggers the full wake-and-stream chain for both paths.

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
