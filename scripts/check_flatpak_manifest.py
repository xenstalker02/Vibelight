#!/usr/bin/env python3
"""Validate Vibelight's Flatpak sandbox and source-pin contracts."""

import json
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST = REPO_ROOT / "vibelight.json"
SOURCE_URL = "https://github.com/xenstalker02/Vibelight.git"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(result.stderr.strip() or f"git {' '.join(args)} failed")
    return result.stdout.strip()


def main() -> None:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    finish_args = data.get("finish-args", [])

    if "--device=all" in finish_args:
        fail("Flatpak manifest still grants all host devices")
    for required in ("--device=dri", "--device=input"):
        if required not in finish_args:
            fail(f"Flatpak manifest is missing {required}")

    sources = [
        source
        for module in data.get("modules", [])
        for source in module.get("sources", [])
        if source.get("url") == SOURCE_URL
    ]
    if len(sources) != 1:
        fail(f"expected exactly one Vibelight source, found {len(sources)}")

    source_pin = sources[0].get("commit", "")
    if len(source_pin) != 40:
        fail("Vibelight source pin must be a full 40-character commit hash")

    git("cat-file", "-e", f"{source_pin}^{{commit}}")
    git("merge-base", "--is-ancestor", source_pin, "HEAD")
    commits_behind = int(git("rev-list", "--count", f"{source_pin}..HEAD"))
    if commits_behind > 1:
        fail(
            f"Vibelight source pin is {commits_behind} commits behind HEAD; "
            "it may only trail by the pin-only commit"
        )

    print("PASS: Flatpak manifest permissions and source pin are current")


if __name__ == "__main__":
    main()
