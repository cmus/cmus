# cmus-uos

> A small, fast and powerful console music player for Unix-like operating systems, optimized for UOS (统信操作系统)

[![Build Status](https://github.com/BennyPerumalla/cmus-uos/workflows/build/badge.svg)](https://github.com/BennyPerumalla/cmus-uos/actions)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub release](https://img.shields.io/github/release/BennyPerumalla/cmus-uos.svg)](https://github.com/BennyPerumalla/cmus-uos/releases)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20UOS%20%7C%20Unix-lightgrey.svg)](https://github.com/BennyPerumalla/cmus-uos)

## 🎵 Overview

cmus-uos is a specialized fork of the popular cmus (C* Music Player) designed specifically for optimal performance on UOS (UnionTech OS) and other Unix-like systems. This lightweight, terminal-based music player offers a rich feature set while maintaining minimal resource usage.

### ✨ Key Features

- **🎛️ Full-featured terminal interface** - Navigate your music collection with vim-like keybindings
- **🎵 Multiple audio formats** - Support for MP3, FLAC, OGG, AAC, and many more
- **🔀 Flexible playback modes** - Shuffle, repeat, and custom playlists
- **🎚️ Advanced audio controls** - Gapless playback, ReplayGain, and crossfading
- **🖥️ UOS optimized** - Special optimizations for UnionTech OS environment
- **⚡ Lightweight & fast** - Minimal memory footprint and CPU usage
- **🔌 Plugin architecture** - Extensible input and output plugin system

## 📸 Screenshots

```
┌─ Album ──────────────────────────────────────────────────────────────────┐
│ Pink Floyd - The Dark Side of the Moon                              1973 │
│  01 Speak to Me                                                     1:13 │
│> 02 Breathe (In the Air)                                            2:43 │
│  03 On the Run                                                      3:36 │
│  04 Time                                                            6:53 │
│  05 The Great Gig in the Sky                                        4:36 │
└──────────────────────────────────────────────────────────────────────────┘
```

## 🚀 Quick Start

### Prerequisites

Ensure you have the following dependencies installed:

```bash
# UOS/Debian/Ubuntu
sudo apt install build-essential pkg-config libncursesw5-dev

# For full audio format support
sudo apt install libfaad-dev libao-dev libasound2-dev libflac-dev \
                 libvorbis-dev libwavpack-dev libmpcdec-dev libmad0-dev
```

### Installation

#### Method 1: From Source (Recommended)

```bash
# Clone the repository
git clone https://github.com/BennyPerumalla/cmus-uos.git
cd cmus-uos

# Configure and build
./configure
make

# Install system-wide
sudo make install

# Or install to custom location
make install DESTDIR=/path/to/installation
```

#### Method 2: Quick Build Script

```bash
# Make the build script executable and run
chmod +x scripts/build.sh
./scripts/build.sh
```

### First Run

```bash
# Start cmus-uos
cmus

# Add music to library (in cmus)
:add ~/Music

# Basic controls
# Space: Play/Pause
# n: Next track
# p: Previous track
# q: Quit
```

## 📖 Usage Guide

### Navigation

| Key | Action |
|-----|--------|
| `Tab` | Switch between views |
| `1-7` | Jump to specific view |
| `j/k` | Move up/down |
| `Enter` | Play selected |
| `Space` | Play/Pause |

### Views

1. **Library** - Browse by artist/album
2. **Sorted** - All tracks sorted
3. **Playlist** - Custom playlists
4. **Play Queue** - Current play queue
5. **Browser** - File system browser
6. **Filters** - Search and filter
7. **Settings** - Configuration

### Advanced Features

#### Creating Playlists
```bash
# In cmus command mode (:)
:save ~/my-playlist.m3u
:load ~/my-playlist.m3u
```

#### Remote Control
```bash
# Control cmus from another terminal
cmus-remote -p          # Play/pause
cmus-remote -n          # Next track
cmus-remote -r          # Previous track
cmus-remote -C "vol +10%" # Volume up
```

## ⚙️ Configuration

cmus-uos configuration is stored in `~/.config/cmus/`. Key configuration files:

- `rc` - Main configuration
- `autosave` - Auto-saved settings
- `lib.pl` - Library playlist
- `playlists/` - Custom playlists

### Essential Settings

```bash
# Audio output (in cmus)
:set output_plugin=alsa
:set dsp.alsa.device=default

# ReplayGain
:set replaygain=track
:set replaygain_preamp=0.0

# Gapless playback
:set buffer_seconds=10
```

## 🔧 UOS-Specific Optimizations

This fork includes several optimizations for UOS:

- **Native UOS audio driver integration**
- **Optimized for UOS desktop environment**
- **Better Chinese font rendering in terminal**
- **Enhanced hardware acceleration support**
- **UOS system notification integration**

## 🐛 Troubleshooting

### Common Issues

**Audio not playing:**
```bash
# Check available output plugins
:set output_plugin=?

# Try different output
:set output_plugin=pulse
# or
:set output_plugin=alsa
```

**Library not updating:**
```bash
# Force library refresh
:update-cache
```

**Permission issues:**
```bash
# Add user to audio group
sudo usermod -a -G audio $USER
```

For more detailed troubleshooting, see [docs/troubleshooting.md](docs/troubleshooting.md).

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

### Development Setup

```bash
# Clone with development branch
git clone -b develop https://github.com/BennyPerumalla/cmus-uos.git
cd cmus-uos

# Set up development environment
./scripts/setup-dev.sh

# Build in debug mode
./configure DEBUG=2
make
```

### Code Style
- Follow Linux kernel coding style
- Use hard tabs (8 characters wide)
- Maximum line length: 80 characters
- Write descriptive commit messages

## 📄 License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Original [cmus](https://github.com/cmus/cmus) developers
- UnionTech for UOS platform support
- All contributors and users who make this project possible

## 📞 Support

- **Documentation**: [docs/](docs/)
- **Issues**: [GitHub Issues](https://github.com/BennyPerumalla/cmus-uos/issues)
- **Discussions**: [GitHub Discussions](https://github.com/BennyPerumalla/cmus-uos/discussions)

## 🔗 Related Projects

- [cmus](https://github.com/cmus/cmus) - Original cmus project
- [UOS](https://www.uniontech.com/) - UnionTech Operating System

---

<div align="center">

**[⬆ Back to Top](#cmus-uos)**

Made with ❤️ for the UOS community

</div>
