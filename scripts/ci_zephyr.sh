#!/usr/bin/env bash
set -euo pipefail

APP=$1
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$REPO_DIR/apps/$APP"

echo "REPO_DIR: $REPO_DIR"

if [ -d /opt/docker30 ]; then
    for f in /opt/docker30/*.yml; do
        echo "=== $f ==="
        cat "$f"
    done
fi

cd "$APP_DIR"
echo "Running Zephyr build for $APP_DIR..."
rm -rf build
west build -b native_sim .
build/${APP}/zephyr/zephyr.exe
