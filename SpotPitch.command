#!/bin/bash

# SpotPitch — Real-time pitch and speed control for Spotify
# Just double click this file to launch

DIR="$(cd "$(dirname "$0")" && pwd)"
DYLIB="$DIR/bin/spotpitch.dylib"
GUI="$DIR/bin/spotpitch-gui"

echo "================================"
echo "  SpotPitch"
echo "  Pitch & Speed control for Spotify"  
echo "================================"
echo ""

# Check Spotify is installed
if [ ! -d "/Applications/Spotify.app" ]; then
    echo "Error: Spotify not found in /Applications"
    echo "Please install Spotify first."
    read -p "Press enter to exit..."
    exit 1
fi

# Kill Spotify if running
if pgrep -x "Spotify" > /dev/null; then
    echo "Closing Spotify..."
    killall -9 Spotify
    sleep 1
fi

# Clean up
rm -f /tmp/spotpitch.sock /tmp/spotpitch.log /tmp/spotpitch-speed

# Launch Spotify with dylib
echo "Launching Spotify..."
DYLD_INSERT_LIBRARIES="$DYLIB" \
    /Applications/Spotify.app/Contents/MacOS/Spotify &

# Wait for initialization
echo "Initializing SpotPitch..."
for i in $(seq 1 15); do
    if [ -S /tmp/spotpitch.sock ]; then
        echo "SpotPitch ready!"
        break
    fi
    sleep 1
done

echo "Opening control panel..."
echo ""
"$GUI"

# When GUI closes, kill Spotify
echo "Closing..."
killall Spotify 2>/dev/null
