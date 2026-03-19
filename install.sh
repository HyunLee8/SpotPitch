#!/bin/bash
echo "================================"
echo "  SpotPitch"  
echo "================================"
echo ""

if ! command -v git &>/dev/null; then
    echo "Error: Install Xcode Command Line Tools first:"
    echo "  xcode-select --install"
    exit 1
fi

if ! command -v node &>/dev/null; then
    echo "Node.js not found. Installing..."
    curl -fsSL https://fnm.vercel.app/install | bash
    export PATH="$HOME/.fnm:$PATH"
    eval "$(fnm env --use-on-cd)"
    fnm install --lts
    fnm use lts-latest
fi

if [ ! -d "/Applications/Spotify.app" ]; then
    echo "Error: Spotify not found. Install Spotify first."
    exit 1
fi

if [ -d "SpotPitch" ]; then
    echo "Updating..."
    cd SpotPitch && git pull && cd ..
else
    echo "Downloading SpotPitch..."
    git clone https://github.com/HyunLee8/SpotPitch.git
fi

echo "Installing GUI dependencies..."
cd SpotPitch/gui && npm install --silent && cd ../..

echo "Launching..."
bash SpotPitch/SpotPitch.command
