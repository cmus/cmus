# cmus - C* Music Player

> A small, fast and powerful console music player for Unix-like operating systems

[![Build Status](https://github.com/cmus/cmus/actions/workflows/build.yml/badge.svg)](https://github.com/cmus/cmus/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Latest Release](https://img.shields.io/github/release/cmus/cmus.svg)](https://github.com/cmus/cmus/releases/latest)
[![IRC Channel](https://img.shields.io/badge/IRC-%23cmus-1e72ff.svg?style=flat)](https://web.libera.chat/#cmus)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20BSD%20%7C%20Unix-lightgrey.svg)](https://github.com/cmus/cmus)

## 🎵 Overview

cmus is a lightweight, terminal-based music player that brings the power of a full-featured audio player to your command line. Built with efficiency and usability in mind, it offers gapless playback, ReplayGain support, MP3 and Ogg streaming, live filtering, instant startup, customizable key-bindings, and vi-style default key-bindings.

### ✨ Key Features

- **🎛️ Intuitive Interface** - Clean, ncurses-based UI with multiple view modes
- **🎵 Extensive Format Support** - MP3, MPEG, WMA, ALAC, Ogg Vorbis, FLAC, WavPack, Musepack, Wav, TTA, SHN and MOD
- **🔀 Advanced Playback** - Gapless playback, which means that there is no pause or delay between tracks
- **🎚️ Audio Enhancement** - ReplayGain support and crossfading
- **📱 Remote Control** - Control via `cmus-remote` from scripts or other terminals
- **🔌 Plugin Architecture** - Extensible input/output plugin system
- **⚡ Performance** - Faster start-up with thousands of tracks
- **🎨 Customizable** - Configurable keybindings, colors, and display formats
- **📡 Streaming** - Internet radio and streaming support

## 📸 Interface Preview

```
┌─ Library ─────────────────────────────────────────────────────────────────┐
│ ▸ Pink Floyd                                                              │
│ ▾ Radiohead                                                               │
│   ▸ OK Computer                                                       1997│
│   ▾ Kid A                                                             2000│
│     01 Everything in Its Right Place                                  4:11│
│   ▸ 02 Kid A                                                          4:44│
│     03 The National Anthem                                            5:51│
│   ▸ In Rainbows                                                       2007│
└───────────────────────────────────────────────────────────────────────────┘
```

## 🚀 Installation

### 📦 Package Managers

<details>
<summary><strong>🐧 Linux Distributions</strong></summary>

```bash
# Ubuntu/Debian
sudo apt install cmus

# Fedora/RHEL/CentOS
sudo dnf install cmus

# Arch Linux
sudo pacman -S cmus

# openSUSE
sudo zypper install cmus

# Alpine Linux
sudo apk add cmus
```
</details>

<details>
<summary><strong>🍎 macOS</strong></summary>

```bash
# Homebrew
brew install cmus

# MacPorts
sudo port install cmus
```
</details>

<details>
<summary><strong>🔧 BSD Systems</strong></summary>

```bash
# FreeBSD
sudo pkg install cmus

# OpenBSD
sudo pkg_add cmus

# NetBSD
sudo pkgin install cmus
```
</details>

### 🛠️ Build from Source

#### Dependencies

| Distro | Required Packages (Always) | Optional/Plugin Packages (Select as needed) |
|--------|---------------------------|---------------------------------------------|
| **Debian/Ubuntu** | `pkg-config libncursesw5-dev` | `libfaad-dev libao-dev libasound2-dev libcddb2-dev libcdio-cdda-dev libdiscid-dev libavformat-dev libavcodec-dev libswresample-dev libflac-dev libjack-dev libmad0-dev libmodplug-dev libmpcdec-dev libsystemd-dev libopusfile-dev libpulse-dev libsamplerate0-dev libsndio-dev libvorbis-dev libwavpack-dev` |
| **Fedora/RHEL** | `pkgconfig(ncursesw)` | `pkgconfig(alsa) pkgconfig(ao) pkgconfig(libcddb) pkgconfig(libcdio_cdda) pkgconfig(libdiscid) pkgconfig(libavformat) pkgconfig(libavcodec) pkgconfig(libswresample) pkgconfig(flac) pkgconfig(jack) pkgconfig(mad) pkgconfig(libmodplug) libmpcdec-devel pkgconfig(libsystemd) pkgconfig(opusfile) pkgconfig(libpulse) pkgconfig(samplerate) pkgconfig(vorbisfile) pkgconfig(wavpack)` |
| **+ RPMFusion** | - | `faad2-devel libmp4v2-devel` |
| **Arch Linux** | `pkg-config ncurses libiconv` | `faad2 alsa-lib libao libcddb libcdio-paranoia libdiscid ffmpeg flac jack libmad libmodplug libmp4v2 libmpcdec systemd opusfile libpulse libsamplerate libvorbis wavpack` |
| **Alpine** | `pkgconf ncurses-dev gnu-libiconv-dev` | `alsa-lib-dev libao-dev libcddb-dev ffmpeg-dev flac-dev jack-dev libmad-dev libmodplug-dev elogind-dev opus-dev opusfile-dev pulseaudio-dev libsamplerate-dev libvorbis-dev wavpack-dev` |
| **Termux** | `ncurses libiconv` | `libandroid-support ffmpeg libmad libmodplug opusfile pulseaudio libflac libvorbis libwavpack` |
| **Homebrew** | `pkg-config ncurses` | `faad2 libao libcddb libcdio libdiscid ffmpeg flac jack mad libmodplug mp4v2 musepack opusfile libsamplerate libvorbis wavpack` |

#### Build Steps

```bash
# Clone the repository
git clone https://github.com/cmus/cmus.git
cd cmus

# Configure build (auto-detects features)
./configure

# View configuration options
./configure --help

# Build
make

# Install system-wide
sudo make install

# Or install to custom location
make install DESTDIR=/path/to/installation

# On BSD systems, use gmake instead of make
gmake && sudo gmake install
```

#### Build Configuration

```bash
# Enable debug mode
./configure DEBUG=2

# Specify installation prefix
./configure --prefix=/usr/local

# Check configured features
cat config.mk
```

## 📖 Usage Guide

### 🎯 Quick Start

```bash
# Start cmus
cmus

# First-time setup
:add ~/Music           # Add music directory
:update-cache          # Update library
:save                  # Save current state
```

### 🎹 Essential Keybindings

#### Navigation & Control
| Key | Action | Description |
|-----|--------|-------------|
| `Tab` | Switch views | Cycle between different interface modes |
| `1-7` | Jump to view | Direct access to specific views |
| `j/k` | Move down/up | Navigate through lists |
| `Space` | Play/Pause | Toggle playback |
| `Enter` | Play selected | Start playing highlighted track |
| `n/p` | Next/Previous | Skip tracks |
| `-/+` | Volume down/up | Adjust volume by 10% |
| `m` | Mute/Unmute | Toggle audio mute |
| `q` | Quit | Exit cmus |

#### Advanced Controls
| Key | Action | Description |
|-----|--------|-------------|
| `s` | Toggle shuffle | Randomize track order |
| `r` | Toggle repeat | Repeat current track/playlist |
| `f` | Toggle follow | Follow current track in library |
| `C` | Continue mode | Continue playback after current track |
| `M` | Show current track | Jump to playing track |
| `I` | Show track info | Display detailed track information |

#### Library Management
| Key | Action | Description |
|-----|--------|-------------|
| `a` | Add to library | Add selected tracks/albums |
| `y` | Add to playlist | Add to current playlist |
| `D` | Remove from library | Delete selected items |
| `e` | Edit tags | Modify track metadata |
| `u` | Update cache | Refresh library database |

### 🎪 Interface Views

#### 1. 📚 Library View (Tree)
Browse your music collection organized by artist and album.
```
Press '1' to access
```

#### 2. 📝 Sorted Library View
All tracks displayed in a sortable list format.
```
Press '2' to access
```

#### 3. 🎵 Playlist View
Manage and play custom playlists.
```
Press '3' to access
```

#### 4. 🎭 Play Queue
Current play queue and upcoming tracks.
```
Press '4' to access
```

#### 5. 📁 Browser View
File system browser for adding music.
```
Press '5' to access
```

#### 6. 🔍 Filter View
Search and filter your music collection.
```
Press '6' to access
```

#### 7. ⚙️ Settings View
Configuration and preferences.
```
Press '7' to access
```

### 🎛️ Command Mode

Access command mode with `:` and use these commands:

#### Playback Control
```bash
:play                  # Start playback
:pause                 # Pause playback
:stop                  # Stop playback
:next                  # Next track
:prev                  # Previous track
:seek +30              # Seek forward 30 seconds
:seek -10              # Seek backward 10 seconds
```

#### Library Management
```bash
:add ~/Music           # Add directory to library
:add file.mp3          # Add single file
:clear                 # Clear current view
:update-cache          # Update library database
:save                  # Save current state
:load playlist.m3u     # Load playlist
```

#### Settings
```bash
:set shuffle=true      # Enable shuffle
:set repeat=track      # Repeat current track
:set vol_left=50       # Set left channel volume
:set vol_right=50      # Set right channel volume
:set output_plugin=alsa # Set audio output plugin
```

### 🎚️ Advanced Features

#### Remote Control
Control cmus from other terminals or scripts:

```bash
# Basic controls
cmus-remote -p         # Play/pause
cmus-remote -s         # Stop
cmus-remote -n         # Next track
cmus-remote -r         # Previous track

# Volume control
cmus-remote -v +10%    # Increase volume
cmus-remote -v -5%     # Decrease volume
cmus-remote -v 50%     # Set absolute volume

# Queue management
cmus-remote -q file.mp3 # Queue track
cmus-remote -c         # Clear queue

# Status and information
cmus-remote -Q         # Show status
cmus-remote -C status  # Detailed status
```

#### Playlist Management
```bash
# In cmus command mode
:save ~/my-playlist.m3u     # Save current playlist
:load ~/my-playlist.m3u     # Load playlist
:pl-create "Rock Classics"  # Create new playlist
:pl-rename "New Name"       # Rename playlist
```

#### Streaming and Internet Radio
```bash
# Add internet radio streams
:add http://stream.example.com/radio.mp3

# Add to favorites for easy access
:save ~/radio-stations.m3u
```

## ⚙️ Configuration

### 📁 Configuration Files
- `~/.config/cmus/` - Main configuration directory
- `~/.config/cmus/rc` - Configuration settings
- `~/.config/cmus/autosave` - Auto-saved state
- `~/.config/cmus/lib.pl` - Library playlist
- `~/.config/cmus/playlists/` - Custom playlists

### 🔧 Essential Settings

#### Audio Configuration
```bash
# Set audio output plugin
:set output_plugin=alsa        # ALSA (Linux)
:set output_plugin=pulse       # PulseAudio
:set output_plugin=oss         # OSS
:set output_plugin=ao          # libao

# Configure audio device
:set dsp.alsa.device=hw:0,0    # ALSA device
:set dsp.pulse.server=localhost # PulseAudio server
```

#### Playback Enhancement
```bash
# ReplayGain settings
:set replaygain=track          # Track-based ReplayGain
:set replaygain=album          # Album-based ReplayGain
:set replaygain_preamp=0.0     # PreAmp adjustment

# Gapless playback
:set buffer_seconds=10         # Buffer size for gapless playback
:set pregap=0                  # Pre-gap for gapless playback
```

#### Display and Interface
```bash
# Format strings
:set format_current= %a - %l - %t %= %y 
:set format_playlist= %-21%a %3n. %t%= %y %d 
:set format_title=%a - %l - %t (%y)

# Color scheme
:colorscheme default           # Use default colors
:set color_cmdline_bg=default  # Command line background
:set color_cmdline_fg=default  # Command line foreground
```

### 🎨 Custom Keybindings
```bash
# Bind keys in command mode
:bind -f common q quit         # Quit with 'q'
:bind -f common ^L refresh     # Refresh with Ctrl+L
:bind -f common h seek -5      # Seek backward with 'h'
:bind -f common l seek +5      # Seek forward with 'l'
```

## 🐛 Troubleshooting

### 🔊 Audio Issues

**No sound output:**
```bash
# Check available output plugins
:set output_plugin=?

# Try different outputs
:set output_plugin=pulse
:set output_plugin=alsa
:set output_plugin=ao

# Check audio device
:set dsp.alsa.device=default
```

**Crackling or distorted audio:**
```bash
# Increase buffer size
:set buffer_seconds=20

# Adjust sample rate
:set dsp.alsa.rate=44100
```

### 📚 Library Issues

**Library not updating:**
```bash
# Force manual update
:update-cache

# Clear and rebuild library
:clear
:add ~/Music
:update-cache
```

**Missing files:**
```bash
# Check file permissions
ls -la ~/Music/

# Verify file formats are supported
cmus --help
```

### 🎵 Playback Issues

**Gapless playback not working:**
```bash
# Ensure proper buffer settings
:set buffer_seconds=10
:set pregap=0

# Check audio format compatibility
# Some formats don't support gapless playback
```

**Seeking problems:**
```bash
# Some formats have limited seeking support
# Try converting to a different format
# Check if the file is corrupted
```

### 🔧 General Troubleshooting

**cmus won't start:**
```bash
# Check terminal compatibility
echo $TERM

# Verify ncurses installation
pkg-config --exists ncursesw && echo "OK" || echo "Missing ncursesw"

# Run with debug output
cmus --debug
```

**Configuration issues:**
```bash
# Reset to defaults
mv ~/.config/cmus ~/.config/cmus.backup
cmus  # Start fresh

# Check for syntax errors in config
:source ~/.config/cmus/rc
```

**Performance issues:**
```bash
# Reduce library size
# Check available memory
free -h

# Disable expensive features
:set replaygain=disabled
:set format_current=%f
```

## 🤝 Contributing

We welcome contributions to make cmus even better! Here's how you can help:

### 🚀 Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/yourusername/cmus.git
   cd cmus
   ```
3. **Create a feature branch**:
   ```bash
   git checkout -b feature/amazing-feature
   ```

### 📝 Development Guidelines

#### Code Style
- Follow the [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
- Use **hard tabs** (8 characters wide)
- Maximum line length: **80 characters**
- Keep style consistent with existing code

#### Building for Development
```bash
# Build with debug symbols
./configure DEBUG=2
make

# Run tests (if available)
make test

# Check for memory leaks
valgrind --tool=memcheck ./cmus
```

#### Commit Guidelines
- Write clear, descriptive commit messages
- Use present tense ("Add feature" not "Added feature")
- Keep commits focused and atomic
- Reference issue numbers when applicable

### 🐛 Reporting Issues

Before creating an issue:
1. Search existing issues to avoid duplicates
2. Test with the latest version
3. Gather debug information:
   ```bash
   ./configure DEBUG=2
   make
   # Run cmus and reproduce the issue
   # Check ~/cmus-debug.txt for details
   ```

### 🔍 Types of Contributions

- **🐛 Bug fixes** - Fix crashes, memory leaks, or incorrect behavior
- **✨ New features** - Add new functionality or improve existing features
- **📚 Documentation** - Improve README, man pages, or code comments
- **🎨 UI/UX** - Enhance the user interface or experience
- **🔧 Build system** - Improve compilation, packaging, or dependencies
- **🧪 Testing** - Add tests or improve test coverage

### 📬 Submitting Changes

1. **Test your changes** thoroughly
2. **Update documentation** if needed
3. **Submit a pull request** with:
   - Clear description of changes
   - Reference to related issues
   - Screenshots for UI changes
   - Test results

## 📄 License

This project is licensed under the **GNU General Public License v2.0** - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **Timo Hirvonen** - Original author and maintainer
- **Various Authors** - Continued development and maintenance
- **Community Contributors** - Bug reports, patches, and improvements
- **Audio library developers** - For the excellent libraries cmus depends on

## 📞 Support & Community

### 💬 Get Help
- **📖 Documentation**: `man cmus` and `man cmus-tutorial`
- **🐛 Bug Reports**: [GitHub Issues](https://github.com/cmus/cmus/issues)
- **💡 Feature Requests**: [GitHub Issues](https://github.com/cmus/cmus/issues)
- **🗨️ Discussions**: [GitHub Discussions](https://github.com/cmus/cmus/discussions)

### 🌐 Community
- **IRC Channel**: `#cmus` on [Libera.Chat](https://web.libera.chat/#cmus)
- **Reddit**: [r/commandline](https://reddit.com/r/commandline)
- **Matrix**: `#cmus:matrix.org`

### 🔗 Related Projects
- **[cmus-osx](https://github.com/PhilipTrauner/cmus-osx)** - macOS integration
- **[cmus-notify](https://github.com/dcx86r/cmus-notify)** - Desktop notifications
- **[cmus-lyrics](https://github.com/ok-borg/cmus-lyrics)** - Lyrics integration

---

<div align="center">

**[🎵 Start Your Musical Journey](#-installation) • [📖 Learn the Basics](#-usage-guide) • [🤝 Join the Community](#-support--community)**

*Made with ❤️ by the cmus community*

[![Star this repository](https://img.shields.io/github/stars/cmus/cmus.svg?style=social)](https://github.com/cmus/cmus/stargazers)

</div>
