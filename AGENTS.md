# cmus — C* Music Player Context Document

## Project Overview
**cmus** is a lightweight, fast, and highly customizable ncurses-based music player for Unix-like operating systems. It is written in C and designed with a decoupled architecture where the user interface, playback engine, and audio decoders/backends are separate modules.

Key features:
- Support for many audio formats via input plugins.
- Multiple output backends (ALSA, PulseAudio, JACK, etc.).
- Powerful library and playlist management.
- Remote control via a socket-based server (`cmus-remote`).
- Highly configurable with a vim-like command-mode.

---

## High-Level Architecture

The cmus codebase is organized into several distinct subsystems:

### 1. User Interface (`ui_curses.c`, `ui_curses.h`)
- **Main Entry Point**: The `main` function for the player is located in `ui_curses.c`.
- **Event Loop**: Uses an ncurses-based event loop (`main_loop`) to handle keyboard input and UI updates.
- **Views**: Supports multiple views:
  - Library (Tree and Sorted views)
  - Playlist
  - Play Queue
  - File Browser
  - Filters
  - Settings/Help
- **Sub-modules**:
  - `browser.c`: Handles the file system browser view.
  - `tree.c`: Manages the hierarchical tree view (Artist -> Album -> Track).
  - `editable.c`: Base logic for editable lists (Playlists, Queue).

### 2. Playback Engine (`player.c`, `player.h`)
- **Audio Thread**: Runs a dedicated thread to fetch data from input plugins and push it to output plugins.
- **State Management**: Tracks current track, status (playing/paused/stopped), position, and bitrate.
- **Buffering**: Uses `buffer.c` to manage audio data buffering between decoders and output backends.

### 3. Plugin System
cmus uses a modular plugin architecture for audio I/O.
- **Input Plugins (`ip/` directory)**: 
  - Decoders for various formats (FLAC, MP3, Vorbis, FFmpeg, etc.).
  - Each plugin implements the `input_plugin_ops` structure (defined in `ip.h`).
- **Output Plugins (`op/` directory)**:
  - Backends for audio output (ALSA, PulseAudio, OSS, JACK, etc.).
  - Each plugin implements the `output_plugin_ops` structure (defined in `op.h`).

### 4. Remote Control & Server (`server.c`, `main.c`)
- **Server**: `server.c` implements a socket server (Unix or TCP) that listens for commands.
- **Remote Client**: `main.c` is the entry point for the `cmus-remote` binary, which communicates with the running cmus instance.
- **MPRIS (`mpris.c`)**: Implements the MPRIS (Media Player Remote Interfacing Specification) D-Bus interface, allowing cmus to be controlled by standard desktop media controllers.

### 5. Data Management & Caching
- **Track Metadata**: `track_info.h` defines the central `track_info` structure containing all metadata and file info.
- **Library (`lib.c`)**: Manages the core music library.
- **Playlists (`pl.c`)**: Manages user-created playlists.
- **Cache (`cache.c`)**: Implements a metadata cache to speed up track loading and searching.

---

## Core Data Structures

### `struct track_info` (`track_info.h`)
The heart of the data model. It stores:
- File path and UID.
- Metadata (Artist, Album, Title, Genre, Track Number, etc.).
- Audio properties (Duration, Bitrate, Codec).
- ReplayGain information.

### `struct input_plugin_ops` (`ip.h`)
Function pointers for input plugins:
- `open`, `close`, `read`, `seek`, `read_comments`, `duration`, `bitrate`.

### `struct output_plugin_ops` (`op.h`)
Function pointers for output plugins:
- `init`, `exit`, `open`, `close`, `drop`, `write`, `buffer_space`, `pause`, `unpause`.

---

## Audio Data Representation

### Sample Format (`sf.h`)
cmus uses a bit-packed `sample_format_t` (unsigned int) to represent audio formats:
- Bits 0: Endianness (big/little).
- Bits 1: Signedness.
- Bits 2-20: Sample rate.
- Bits 21-23: Bits per sample (divided by 8).
- Bits 24-31: Number of channels.

Helper macros like `sf_get_rate(sf)`, `sf_get_channels(sf)`, and `sf_get_frame_size(sf)` are used throughout the codebase to decode these values.

### Channel Mapping (`channelmap.h`)
Defines how audio channels are mapped (e.g., Front Left, Front Right). This is crucial for correctly outputting multi-channel audio.

---

## Command & Configuration System

### Commands (`command_mode.c`)
Commands are defined in the `commands` array in `command_mode.c`. Each entry maps a command name to a handler function (e.g., `:quit` -> `cmd_quit`).
- **Command Parsing**: Handled in `cmdline.c`.
- **Key Bindings**: Managed in `keys.c`.

### Options (`options.c`)
User settings are defined and registered in `options.c` via the `options_add` function.
- Settings include UI preferences, playback behavior, and plugin-specific options.

---

## Background Tasks (Job System)
Background operations (like adding large directories to the library) are handled by:
- `job.c`: Schedules tasks like `JOB_TYPE_ADD`, `JOB_TYPE_UPDATE`, etc.
- `worker.c`: Implements the worker thread that executes these jobs to keep the UI responsive.

---

## File Organization Summary

| Path | Description |
| :--- | :--- |
| `ui_curses.c` | UI entry point and main loop |
| `player.c` | Core playback engine |
| `ip/` | Input (decoder) plugins |
| `op/` | Output (audio backend) plugins |
| `lib.c` / `pl.c` | Library and Playlist management |
| `cache.c` | Metadata caching logic |
| `server.c` | Command server implementation |
| `main.c` | Remote control client (`cmus-remote`) |
| `command_mode.c` | Command definitions and execution |
| `options.c` | Configuration and settings |
| `track_info.c` | Track metadata handling |
| `Doc/` | Documentation (manuals, tutorial) |
| `data/` | Themes and default RC files |

---

## Development Guidelines

### Coding Style
cmus follows the **Linux kernel coding style**:
- Use hard tabs (8 characters wide).
- Keep functions concise.
- Mimic existing naming conventions (snake_case).

### Adding a New Command
1. Define the command handler function in `command_mode.c`.
2. Register the command in the `commands` array.
3. If necessary, add a default key binding in `keys.c` (or via `data/rc`).

### Adding an Input/Output Plugin
1. Create a new `.c` file in `ip/` or `op/`.
2. Implement the required `ops` structure.
3. Update the build system (handled automatically by `configure` if standard patterns are followed).

### Build System
- **`configure`**: A custom shell script for feature detection and generating `config.mk`.
- **`Makefile`**: Standard GNU Makefile. Use `make` to build and `make install` to install.
