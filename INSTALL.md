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

## Troubleshooting
See [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
