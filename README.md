# SpotPitch

Real-time pitch shifting, speed control, and reverb for Spotify on Mac — no quality loss, no re-encoding.

Built with a C++ dylib injected directly into Spotify's audio pipeline, intercepting raw PCM before it hits your speakers. Pitch shifting via [Rubber Band Library](https://breakfastquay.com/rubberband/), reverb via Freeverb algorithm.

## Features

- **Pitch shift** — up to ±12 semitones with formant preservation
- **Speed control** — 0.5x to 3.5x without pitch change
- **Reverb** — adjustable room size and wet mix
- **Presets** — Normal, Slowed, Nightcore, Daycore
- **Real-time** — changes apply instantly

## Requirements

- macOS Apple Silicon (M1/M2/M3/M4)
- Spotify installed in `/Applications`

## Installation
```bash
git clone https://github.com/HyunLee8/SpotPitch.git
cd SpotPitch
```

Then double click `SpotPitch.command`. That's it.

## Usage

SpotPitch handles everything automatically:
1. Closes Spotify if running
2. Relaunches Spotify with the audio hook
3. Opens the control panel

Drag the sliders to adjust pitch, speed, and reverb in real time.

## Presets

| Preset | Pitch | Speed | Reverb |
|--------|-------|-------|--------|
| Normal | 0 | 1.0x | Off |
| Slowed | -3 semitones | 0.8x | On |
| Nightcore | +4 semitones | 1.25x | Off |
| Daycore | -4 semitones | 0.9x | On |

## How it works

SpotPitch injects a dylib into Spotify at launch using `DYLD_INSERT_LIBRARIES`. The dylib hooks `AudioDeviceCreateIOProcID` via [fishhook](https://github.com/facebook/fishhook), intercepting Spotify's audio IO callback. Every audio buffer passes through Rubber Band for pitch/time stretching, then Freeverb for reverb, before reaching your speakers.

No Spotify files are modified. No SpotX required. Spotify updates won't break it.

## Notes

- Speed control may show a timer desync in Spotify's progress bar — audio plays correctly
- Re-run `SpotPitch.command` if Spotify is restarted normally
- Apple Silicon only for now — Intel and Linux support coming

## Building from source

See [BUILDING.md](BUILDING.md) for instructions.
