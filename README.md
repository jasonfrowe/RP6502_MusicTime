# MusicTime

MusicTime is a VGM music player for the Picocomputer RP6502 platform, focused on
OPL2 (YM3812) playback with a fast in-device file browser.

## Features

- OPL2 VGM playback on RP6502
- File browser with paging and key-hold auto-repeat navigation
- GD3 metadata display (title + author)
- Loop mode toggle (`VGMTagLoop`) that is global and persists across tracks
- Track navigation controls (restart/prev/next)
- Seek forward/backward while keeping audio stable
- Ignores macOS `._` resource-fork files in directory listings

## Controls

### Keyboard

- `Up` / `Down`: Move selection
- `Shift+Up` / `Shift+Down`: Page up/down
- `Enter`: Select/open
- `Backspace`: Go to parent directory
- `Left`: Restart current track
- `Left` (double-press): Previous track
- `Right`: Next track
- `Space`: Play/Pause
- `S`: Stop
- `L`: Toggle `VGMTagLoop` on/off
- `F`: Seek forward
- `R`: Seek backward
- `Q` or `Esc`: Quit

### Gamepad (active pad)

- D-Pad Up/Down: Move selection
- `A`: Select/open
- `B`: Back/parent
- `Start`: Play/Pause
- `Select`: Stop
- `L1` / `R1`: Seek backward/forward
- `Home`: Quit

## Supported VGM Data

MusicTime currently supports a practical OPL2-focused subset of VGM commands.

### Implemented playback commands

- `0x5A`: YM3812 write
- `0x61`: Wait n samples
- `0x62`: Wait 735 samples (60 Hz)
- `0x63`: Wait 882 samples (50 Hz)
- `0x70` to `0x7F`: Short waits
- `0x66`: End of data (loop to tag if `VGMTagLoop` is on and a loop tag exists)

### Parsed/skipped safely

- `0x67`: Data block (skipped)
- `0x4F`, `0x50`: 1-byte payload commands (skipped)
- `0x51`, `0x52`, `0x53`, `0x54`, `0x55`, `0x58`, `0x59`: 2-byte payload commands (skipped)
- `0xE0`: 4-byte payload command (skipped)

### Metadata support

- GD3 tag parsing for English title and author
- UTF-16 LE GD3 strings are converted to ASCII for display

### Notes

- Input files should be `.vgm` (not compressed `.vgz`)
- Unsupported commands are ignored with a warning status line when encountered
- Browser/file loading uses fixed-size internal buffers: filenames longer than 63 characters or full paths longer than 256 characters may be truncated and then fail to open correctly
- For better compatibility, keep `.vgm` basenames short; the included `tools/vgz_clean.sh` script caps cleaned names to 60 characters before the `.vgm` extension

## Build

This project uses CMake + LLVM-MOS SDK for `rp6502`.

From project root:

```sh
cmake --preset default
cmake --build --preset default
```

Output ROM:

- `build/target-native/MusicTime.rp6502`

## Upload

Use the included RP6502 utility:

```sh
python3 ./tools/rp6502.py upload build/target-native/MusicTime.rp6502
```

## Project Structure

- `src/main.c`: App loop, transport/state logic, rendering orchestration
- `src/browser.c`: Directory browsing and selection/navigation logic
- `src/vgm.c`: VGM stream parser/decoder
- `src/opl.c`: OPL2 hardware writes and mute/shadow behavior
- `src/input.c`: Keyboard/gamepad mapping and action gating
- `src/ui.c`: Text-mode UI renderer
