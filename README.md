cat > ~/dev/SpotPitch/README.md << 'EOF'
# SpotPitch

Real-time pitch shifting, speed control, and reverb for Spotify on Mac — no quality loss, no re-encoding.

Built with a C++ dylib injected directly into Spotify's audio pipeline, intercepting raw PCM before it hits your speakers. Pitch shifting via [Rubber Band Library](https://breakfastquay.com/rubberband/), reverb via Freeverb algorithm.

## Features

- **Pitch shift** — up to ±12 semitones with formant preservation
- **Speed control** — 0.5x to 3.5x without pitch change
- **Reverb** — adjustable room size and wet mix
- **Presets** — Normal, Slowed, Nightcore, Daycore
- **Real-time** — no latency, changes apply instantly

## Requirements

- macOS (Apple Silicon / arm64e)
- Spotify installed in `/Applications`

## Usage

Double click `SpotPitch.command` — it handles everything:
1. Closes Spotify if running
2. Relaunches Spotify with the audio hook
3. Opens the control panel

Drag the sliders. Done.

## How it works

SpotPitch injects a dylib into Spotify at launch using `DYLD_INSERT_LIBRARIES`. The dylib hooks `AudioDeviceCreateIOProcID` via [fishhook](https://github.com/facebook/fishhook), intercepting Spotify's audio IO callback. Every audio buffer passes through Rubber Band for pitch/time stretching, then through Freeverb for reverb, before reaching your output device.

No Spotify files are modified. No SpotX required. Spotify updates won't break it.

## Notes

- Spotify may show a timer desync when using speed control — this is cosmetic, audio plays correctly
- Re-run `SpotPitch.command` if Spotify is updated or restarted
- Currently Mac only (Apple Silicon). Intel and Linux support planned.

## Building from source
```bash
