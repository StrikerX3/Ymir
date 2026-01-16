# Common troubleshooting steps

## Ymir fails to launch or crashes right away

1. Make sure to download a version compatible with your CPU. The AVX2 version requires newer, usually more powerful CPUs, so if you have a Core i3 or i5 from older generations (3xxx or less), a Pentium or a Celeron, the first thing to try is to test the SSE2 version instead.
2. For Windows users: install the [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vs/17/release/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vs/17/release/vc_redist.arm64.exe)) before launching Ymir. Installing other software might replace important system files with older versions that are incompatible with the emulator.
3. Ymir specifically requires the default audio output device to be present upon startup. If you set your headphones as the default device, make sure they're plugged in before launching the emulator.


## "No IPL ROM found" message when loading any game

IPL ROM is the Saturn BIOS ROM, which is required for Ymir to work. You need to place it in the `<profile>/roms/ipl` directory. The emulator will automatically detect and load the file as soon as you place it in the directory.

Most people that ask about this have skipped the Welcome dialog explaining this step. The Welcome screen also includes a clickable link for the path and will go away on its own as soon as a valid IPL ROM is placed in the directory. Remember: read *everything*!


## Ymir crashes with a big "fatal error" popup

If you're on Windows:
1. Make sure you're using the [latest nightly build](https://github.com/StrikerX3/Ymir/releases/tag/latest-nightly)
2. Leave the popup open
3. Download ProcDump: https://learn.microsoft.com/en-us/sysinternals/downloads/procdump
4. Open a Command Prompt window (cmd.exe) and run `procdump ymir-sdl3.exe`
5. Open the folder from which you ran the command. There should be a file named `ymir-sdl3.exe_<date>_<time>.dmp`. Compress that and share it. This file contains a minimal dump of the program which can be used by developers to figure out where exactly the emulator crashed.
   - For developers: the PDBs can be found attached to the [nightly release workflow](https://github.com/StrikerX3/Ymir/actions/workflows/nightly-release.yaml)


## My controller doesn't work or some buttons don't respond

Ymir currently works best with XInput controllers, that is, anything that behaves like an Xbox controller. Third-party controllers like 8bitdo sometimes offer a toggle or a way to enable XInput mode on their controllers which usually improves compatibility. There are plans to improve compatibility with these controllers in the future, but it's not high in the priority list.


## Ymir runs too slowly

Here are a few things you can try to improve performance, roughly in order of performance impact:
- Stop any background programs you're running, including things like Rainmeter.
- In the Debug menu, make sure tracing is disabled.
- In Settings > CD Block, disable low-level emulation if the game doesn't require it to work properly.
- In Settings > Tweaks, select the Best performance presets on both sections. This will change a few settings that may break compatibility with games, specifically the CD Block read speed.
- In Settings > Video, disable "Synchronize video in windowed mode" and "Use full refresh rate when synchronizing video". These are known to cause problems in cases where the reported refresh rate does not match the actual display refresh rate.
- In Settings > Video, enable "Include VDP1 rendering in VDP2 renderer thread". Note that this can break a few games too.
- In the Emulation menu, disable the Rewind Buffer.
- Use the AVX2 version if you can. If the emulator crashes right away, it's likely that your CPU doesn't support the instruction set, so you're stuck with the SSE2 version.

If Ymir still runs too slowly after this, your CPU might be too slow for the emulator. It's known to run fine on CPUs that score around 1500 points on the [CPUBenchmark single thread test](<https://www.cpubenchmark.net/single-thread>), but I recommend CPUs that score 2000 points or higher. A quad core CPU or better will help with threaded VDP2 rendering, threaded deinterlace and the rewind buffer.
