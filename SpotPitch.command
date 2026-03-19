#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
DYLIB="$DIR/bin/spotpitch.dylib"
GUI="$DIR/gui"

if [ ! -d "/Applications/Spotify.app" ]; then
    echo "Spotify not found in /Applications"
    exit 1
fi

if pgrep -x "Spotify" > /dev/null; then killall -9 Spotify; fi
rm -f /tmp/spotpitch.sock /tmp/spotpitch.log

DYLD_INSERT_LIBRARIES="$DYLIB" \
    /Applications/Spotify.app/Contents/MacOS/Spotify &

sleep 2
cd "$GUI" && npm start
killall Spotify 2>/dev/null
