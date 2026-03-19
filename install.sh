#!/bin/bash

if [ -d "SpotPitch" ]; then
    echo "SpotPitch already installed, updating..."
    cd SpotPitch && git pull && bash SpotPitch.command
else
    git clone https://github.com/HyunLee8/SpotPitch.git && cd SpotPitch && bash SpotPitch.command
fi
