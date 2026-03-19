#!/bin/bash

DYLIB_PATH="$(dirname "$0")/spotpitch.dylib"
GUI_PATH="$(dirname "$0")/../spotpitch-gui/spotpitch-gui"

# Check dylib exists
if [ ! -f "$DYLIB_PATH" ]; then
    echo "Error: spotpitch.dylib not found at $DYLIB_PATH"
    exit 1
fi

# Check GUI exists
if [ ! -f "$GUI_PATH" ]; then
    echo "Error: spotpitch-gui not found at $GUI_PATH"
    exit 1
fi

# Kill Spotify if running
if pgrep -x "Spotify" > /dev/null; then
    echo "Closing Spotify..."
    killall -9 Spotify
    sleep 1
fi

# Clean up old socket and files
rm -f /tmp/spotpitch.sock
rm -f /tmp/spotpitch.log
rm -f /tmp/spotpitch-speed

# Launch Spotify with dylib
echo "Launching Spotify with SpotPitch..."
DYLD_INSERT_LIBRARIES="$DYLIB_PATH" \
    /Applications/Spotify.app/Contents/MacOS/Spotify &

SPOTIFY_PID=$!

# Wait for socket to appear (means dylib is ready)
echo "Waiting for SpotPitch to initialize..."
for i in $(seq 1 15); do
    if [ -S /tmp/spotpitch.sock ]; then
        break
    fi
    sleep 1
done

if [ ! -S /tmp/spotpitch.sock ]; then
    echo "Warning: SpotPitch may not have initialized. Try playing a song first."
fi

# Launch GUI
echo "Launching SpotPitch GUI..."
"$GUI_PATH"
