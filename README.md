# Ymir

[![Stable release](https://github.com/StrikerX3/Ymir/actions/workflows/stable-release.yaml/badge.svg)](https://github.com/StrikerX3/Ymir/actions/workflows/stable-release.yaml) [![Nightly release](https://github.com/StrikerX3/Ymir/actions/workflows/nightly-release.yaml/badge.svg)](https://github.com/StrikerX3/Ymir/actions/workflows/nightly-release.yaml) <a href="https://discord.gg/NN3A7n5dzn">![Discord Shield](https://discord.com/api/guilds/1368676375627694341/widget.png?style=shield)</a> <a href="https://patreon.com/StrikerX3">![Patreon Shield](https://img.shields.io/badge/Patreon-F96854?style=flat&logo=patreon&logoColor=white)</a>

A work-in-progress Sega Saturn emulator.

Join the [Discord community](https://discord.gg/NN3A7n5dzn).

Find the official compatibility list [here](https://docs.google.com/spreadsheets/d/1SLZzL9LelSlpEmTKy8cjaQnE7mew2uW1rfCgcekO58Q/edit?usp=sharing).

Grab the latest release: [stable](https://github.com/StrikerX3/Ymir/releases/latest), [nightly](https://github.com/StrikerX3/Ymir/releases/latest-nightly).

> [!IMPORTANT]
> Windows users: install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vs/17/release/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vs/17/release/vc_redist.arm64.exe)) before running the emulator.
> This is **mandatory** to avoid crashes on startup.

<div class="grid" markdown>
  <img width="49.5%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/cd-player.png"/>
  <img width="49.5%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/sonic-r.png"/>
  <img width="49.5%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/virtua-fighter-2.png"/>
  <img width="49.5%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/radiant-silvergun.png"/>
  <img width="49.5%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/panzer-dragoon-saga.png"/>
  <img width="49.5%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/nights-into-dreams.png"/>
  <img width="100%" src="https://github.com/StrikerX3/Ymir/blob/main/docs/images/debugger.png"/>
</div>


## Features

- Load games from MAME CHD, BIN+CUE, IMG+CCD, MDF+MDS or ISO files
- Automatic IPL (BIOS) ROM detection
- Automatic region switching
- Up to two players with a variety of controllers on both ports
- Fully customizable keybindings
- Backup RAM, DRAM and ROM cartridges (more to come)
- Integrated backup memory manager to import and export saves, and transfer between internal and cartridge RAM
- Forwards-compatible save states
- Rewinding (up to one minute at 60 fps), turbo speed, frame step (forwards and backwards)
- Full screen mode with VRR support and low input lag
- Graphics enhancements such as optional deinterlaced/progressive rendering of high resolution modes and transparent mesh polygon rendering
- Optional low level CD block emulation
- A work-in-progress feature-rich debugger

Ymir runs on Windows 10 or later, macOS 13 (Ventura) or later, most modern Linux distributions and FreeBSD, and supports x86-64 (Intel, AMD) and ARM CPUs.


## Usage

Grab the latest release [here](https://github.com/StrikerX3/Ymir/releases/latest).
Check the [Releases](https://github.com/StrikerX3/Ymir/releases) page for previous versions.

Ymir does not require installation. Simply download it to any directory and run the executable.
On Windows you might also need to install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vs/17/release/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vs/17/release/vc_redist.arm64.exe)).

The program accepts command-line arguments. Invoke `ymir-sdl3 --help` to list the options:

```
Ymir - Sega Saturn emulator
Usage:
  Ymir [OPTION...] path to disc image

  -d, --disc arg     Path to Saturn disc image (.ccd, .chd, .cue, .iso,
                     .mds)
  -p, --profile arg  Path to profile directory
  -u, --user         Force user profile
  -h, --help         Display help text
  -f, --fullscreen   Start in fullscreen mode
  -P, --paused       Start paused
  -D, --debug        Start with debug tracing enabled
  -E, --exceptions   Capture all unhandled exceptions
```

The options are case-sensitive -- lowercase `-p` sets the profile path, uppercase `-P` makes the emulator start paused.

Pass a path to a valid Saturn disc image as an argument to `ymir-sdl3` to launch the emulator with the disc. `-d`/`--disc` is optional.

Use `-p <profile-path>` to point to a separate set of configuration and state files, useful if you wish to have different user profiles (hence the name).

The `-u` option forces usage of the OS's user profile folder (e.g. `C:\Users\<username>\AppData\Roaming\StrikerX3\Ymir` on Windows or `/home/<username>/.local/share/Ymir` on Linux).

`-f` forces the emulator to start in fullscreen mode, ignoring the preference in Ymir.toml.

`-D` starts the emulator with debug tracing enabled.

`-E` captures all unhandled exceptions, which can be useful to troubleshoot crashes or failure to start the emulator.

Note that the Windows version does not output anything to the console, but it does honor the command line parameters. You can pipe the output of the command to a file:

```sh
ymir-sdl3 > out.txt
```

Ymir requires an IPL (BIOS) ROM to work. You can place the ROMs under the `roms\ipl` directory created alongside the executable on the first run.
The emulator will scan and automatically select the IPL ROM matching the loaded disc. If no disc is loaded, it will use a ROM matching the first preferred region. Failing that, it will pick whatever is available.
You can override the selection on Settings > IPL.

Ymir can load game disc images from MAME CHD, BIN+CUE, IMG+CCD, MDF+MDS or ISO files. It does not support injecting .elf files directly at the moment.

When using low level CD block emulation (LLE), Ymir also requires the CD block ROM to be placed in `roms\cdb`.


## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) if the emulator crashes or misbehaves.


## Compiling

See [COMPILING.md](COMPILING.md).

Please note that the author (@StrikerX3) works primarily with Windows and Linux systems and only provides support for other platforms on a best-effort basis.
For Linux specifically, only the .tar.xz package is officially supported by the author. The project relies on community support for other packages and platforms.


## Support my work

If you enjoy my projects and want to help me keep developing them, consider supporting me:
- [Patreon](https://www.patreon.com/StrikerX3) for ongoing support
- PIX for one-time donations in Brazil: ask me on Discord.

Your support is completely optional but genuinely appreciated. It helps me dedicate more time and energy to these passion projects while keeping everything open-source and free for everyone. Thank you!
