# JAMPG — Image-to-Rhythm Sequencer

**WeirdSynths / Corpus** for VCV Rack 2

Your image is a drum machine. JamPG reads pixels as rhythm.

## How It Works

Load any JPEG, PNG, or BMP image. Each clock pulse advances a playhead through the image pixel-by-pixel, left-to-right, top-to-bottom. The Red, Green, and Blue channels of each pixel become three independent voices, each with a gate output and a CV output.

Bright pixels fire gates. Dark pixels stay silent. The image *is* the sequence.

## Inputs

| Jack | Function |
|------|----------|
| CLK | Clock — advances playhead one step per pulse |
| RST | Reset — returns playhead to top-left pixel |
| R CV | Red level CV modulation (0–10V) |
| G CV | Green level CV modulation (0–10V) |
| B CV | Blue level CV modulation (0–10V) |

## Outputs

| Jack | Function |
|------|----------|
| R GATE / G GATE / B GATE | Gate high when channel exceeds threshold |
| R CV / G CV / B CV | 0–10V proportional to pixel brightness × level |
| X POS | Playhead X position as 0–10V |
| Y POS | Playhead Y position as 0–10V |
| EOL | Trigger at end of each row |
| EOF | Trigger at end of entire image (wraps to start) |

## Controls

| Knob | Range | Function |
|------|-------|----------|
| RED / GRN / BLU | 0–100% | Attenuator for each color channel |
| THRESH | 0–100% | Gate firing threshold — higher = only bright pixels trigger |
| STEP | 1–32 px | Pixels to advance per clock pulse |

## Quick Start

1. Add JamPG to your patch
2. Click the LOAD button (or the display area) to open an image
3. Patch a clock source into the CLK input
4. Connect R/G/B gate outputs to drum triggers or envelope gates
5. Use R/G/B CV outputs for filter cutoff, pitch, or any modulation target

## Tips

- Photographs make organic, evolving sequences
- Pixel art and sprites make tight, repetitive patterns
- Use STEP to skip pixels for sparser rhythms
- EOL trigger is great for resetting other sequencers per row
- Feed position outputs (X/Y) into other modules for image-synced modulation

## Building from Source

```
git clone https://github.com/YOUR_REPO/Corpus.git
cd Corpus
export RACK_DIR=/path/to/Rack-SDK
make
make install
```

## License

GPL-3.0-only — see LICENSE file.

Copyright (c) 2026 Millie — WeirdSynths
