# Changelog

All notable changes to the Corpus plugin will be documented in this file.

## [2.0.0] - 2026-03-05

### Added
- **JAMPG** — Image-to-rhythm sequencer. Load any image, scan pixels left-to-right/top-to-bottom, and convert RGB values into gates and CV. Features threshold control, variable scan width, per-channel level knobs, and EOL/EOF triggers for structural events.
- CRT/PCB panel aesthetic with outlined vector typography
- Full save/load state: image path and playhead position persist across sessions

### Existing Modules
- BLINK — Eye blink trigger/gate sequencer
- STILL — Inverted activity detector
- TIDE — Long-form trend analyzer
- GAZE — Face position/size/velocity tracker
- MAW — Mouth tracker (jaw openness, lip width)
- MASK — Expression tracker (eyebrow raise, facial asymmetry)
- NOD — Head gesture recognizer (nod, shake, tilt)
- SIGHT — Real-time wireframe face visualization
- MIDI GATEWAY — Body-tracking CV to MIDI CC/notes/pitch bend
- PULSE — Bidirectional 2-channel CV over Meshcore radio
- SWARM — Multi-node Meshcore hub monitor
