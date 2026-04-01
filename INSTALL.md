## Requirements
- Steam Deck with SteamOS 3.x
- [Vibepollo](https://github.com/xenstalker02/Vibepollo) running on a Windows PC

## Install
```bash
git clone https://github.com/xenstalker02/Vibelight ~/Vibelight
cd ~/Vibelight && bash install.sh
```

## Moonlight.conf Options

| Option | Default | Description |
|--------|---------|-------------|
| `micCapture=` | `false` | Set to `true` to enable mic passthrough |
| `micDevice=` | *(empty)* | SDL audio device name. Empty = system default |
| `micBitrate=` | `64000` | Opus bitrate in bps (default 64 kbps). Range: 6000--510000 |

Config file location: `~/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf`

## Pairing with Vibepollo
1. Open `https://<your-pc-ip>:47990` in a browser
2. In Vibelight, select your PC and choose **Send PIN**
3. Enter the PIN shown on your Steam Deck into the Vibepollo web UI

## Moonwake System
Vibelight integrates with a bash wake script (`moonlight_wake.sh`) that:
- Detects whether you are on the home LAN or a remote network
- Sets the correct server IP (LAN or Tailscale) in Moonlight.conf
- Sets a HOME or AWAY label on the server entry
- Wakes the PC via WOL if needed, then launches Moonlight

To use moonwake, point your Steam shortcut at `moonlight_wake.sh run` instead of launching Vibelight directly.

## Troubleshooting
See [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
