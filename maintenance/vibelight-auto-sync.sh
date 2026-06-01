#!/usr/bin/env bash
# vibelight-auto-sync.sh
# Deployed to ~/vibelight-auto-sync.sh on the Deck via stage-5-build-vl.ps1.
# Runs at every boot via @reboot cron — rebuilds Vibelight if the upstream pipeline
# pushed new commits since the last local build. Requires no manual input.
set -euo pipefail

REPO="$HOME/vibelight"
LOG="$HOME/vibelight-auto-sync.log"

# Rotate log — keep last 500 lines so it never grows unbounded
if [ -f "$LOG" ] && [ "$(wc -l < "$LOG")" -gt 500 ]; then
    tail -n 500 "$LOG" > "${LOG}.tmp" && mv "${LOG}.tmp" "$LOG"
fi

exec >> "$LOG" 2>&1
echo ""
echo "=== $(date '+%Y-%m-%d %H:%M:%S') vibelight-auto-sync start ==="

# Wait for network — up to 3 minutes (30 × 6s). Deck may boot before Wi-Fi is ready.
echo "Checking network..."
NETWORK_UP=0
for i in $(seq 1 30); do
    if curl -sf --max-time 5 https://api.github.com > /dev/null 2>&1; then
        NETWORK_UP=1
        echo "Network ready (attempt $i)"
        break
    fi
    sleep 6
done

if [ "$NETWORK_UP" -eq 0 ]; then
    echo "Network not available after 3 minutes — skipping this boot"
    echo "=== done (no network) ==="
    exit 0
fi

if [ ! -d "$REPO/.git" ]; then
    echo "Repo not found at $REPO — skipping"
    echo "=== done (no repo) ==="
    exit 0
fi

cd "$REPO"

# Fetch to see if origin/master is ahead of local HEAD
git fetch origin --quiet

LOCAL=$(git rev-parse HEAD 2>/dev/null || echo "none")
REMOTE=$(git rev-parse origin/master 2>/dev/null || echo "none")

if [ "$LOCAL" = "$REMOTE" ]; then
    echo "Already at $(git rev-parse --short HEAD 2>/dev/null) — no build needed"
    echo "=== done (up to date) ==="
    exit 0
fi

echo "Local HEAD:  $(git rev-parse --short HEAD 2>/dev/null || echo '?')"
echo "Remote HEAD: $(git rev-parse --short origin/master 2>/dev/null || echo '?')"
echo "New commits:"
git log --oneline HEAD..origin/master | head -20

echo "Pulling and rebuilding Vibelight..."
git pull origin master

bash install.sh

FLATPAK_COMMIT=$(flatpak info --show-commit com.moonlight_stream.Moonlight 2>/dev/null | head -1 || echo "unknown")
echo "New Flatpak commit: $FLATPAK_COMMIT"
echo "=== done (rebuilt successfully) ==="
