# Common troubleshooting steps

## Ymir fails to launch or crashes right away

- Make sure to download a version compatible with your CPU. The AVX2 version requires newer, usually more powerful CPUs, so if you have a Core i3 or i5 from older generations (3xxx or less), a Pentium or a Celeron, the first thing to try is to test the SSE2 version instead.
- For Windows users: install the [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vs/17/release/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vs/17/release/vc_redist.arm64.exe)) before launching Ymir. Installing other software might replace important system files with older versions that are incompatible with the emulator.
- Ymir specifically requires the default audio output device to be present upon startup. If you set your headphones as the default device, make sure they're plugged in before launching the emulator.


## Ymir crashes with a big "fatal error" popup

You encountered a critical bug in the application. The best course of action is to collect a memory dump which can pinpoint the exact piece of code that caused the problem.

### Windows

Follow these steps to collect and share a minidump:
1. Make sure you're using the [latest nightly build](https://github.com/StrikerX3/Ymir/releases/tag/latest-nightly). These instructions only work with the nightly builds as they include debug symbols, while stable releases do not.
2. Leave the fatal error popup open.
3. Download ProcDump: https://learn.microsoft.com/en-us/sysinternals/downloads/procdump.
4. Open a Command Prompt window (cmd.exe) and run `procdump ymir-sdl3.exe`.
5. Open the folder from which you ran the command (you can run `start .` from the Command Prompt to open an Explorer window on that directory). There should be a file named `ymir-sdl3.exe_<date>_<time>.dmp`. Compress that and share it. This file contains a minimal dump of the program which can be used by developers to figure out where exactly the emulator crashed.
   - For developers: the PDBs can be found attached to the [nightly release workflow](https://github.com/StrikerX3/Ymir/actions/workflows/nightly-release.yaml).

### Linux, macOS, FreeBSD

1. Enable core dumps temporarily (if you haven't already enabled them system-wide):
    ```sh
    ulimit -c unlimited
    ```
2. Run the emulator from the same shell session.
3. When the crash occurs, open a new shell and collect the dump:
    1. Find the PID of the process:

        ```sh
        pgrep ymir-sdl3
        ```
        or:
        ```sh
        ps a | grep ymir-sdl3
        ```
    2. Generate the core dump:

        Linux:
        ```sh
        gcore -o ymir.dmp <pid>
        ```
        FreeBSD:
        ```sh
        gcore -c ymir.dmp <pid>
        ```
        or:
        ```sh
        kill -6 <PID>
        ```
        NOTE: `kill -6` sends a `SIGABRT` signal to the process, causing the core dump to be saved to the default core dumps location in your system.
    3. Compress and upload the dump file.


## "No IPL ROM found" message when loading any game

IPL ROM is the Saturn BIOS ROM, which is required for Ymir to work. You need to place it in the `<profile>/roms/ipl` directory. The emulator will automatically detect and load the file as soon as you place it in the directory.

Most people that ask about this have skipped the Welcome dialog explaining this step. The Welcome screen also includes a clickable link for the path and will go away on its own as soon as a valid IPL ROM is placed in the directory. Remember: read *everything*!


## My controller doesn't work or some buttons don't respond

Ymir currently works best with XInput controllers, that is, anything that behaves like an Xbox controller. Third-party controllers like 8bitdo sometimes offer a toggle or a way to enable XInput mode on their controllers which usually improves compatibility.

There are plans to improve compatibility with other controllers in the future, but it's not high in the priority list.


## Game discs don't load with the Flatpak release

Flatpak uses sandboxing and restricts access to the file system by default. Ymir 0.2.1 and later grant access to your home directory, but earlier versions didn't grant any additional permissions.

If you're still unable to load disc images:
1. In **Flatseal**, find *Ymir*.
2. In the **Filesystem** section, do either of these:
   - **(Recommended)** Add a new entry in **Other files** and type the directory where you store your disc images (e.g. `~/Roms/Saturn`).
   - **(Less secure)** Enable **All user files** if your ROMs live in your home directory.


## Ymir runs too slowly

Here are a few things you can try to improve performance besides upgrading the CPU, roughly in order of performance impact:
- Stop any background programs you're running, including things like Rainmeter. Some RGB tools are also known for being CPU hogs.
- In the **Debug** menu, make sure **Enable tracing** is disabled. (Shortcut is F11 by default.)
- In **Settings > General**, check that the **Emulation speed** is set to **Primary** and it is at **100%**. Press **Reset** to restore the default speed.
- In **Settings > CD Block**, disable **Use low level CD Block emulation**. Most games work fine without it.
- In **Settings > System**, disable **Emulate SH-2 cache** if possible. Most games work fine without it.
  - This option is force-enabled with a few select games.
- In **Settings > Audio**, set **Emulation step granularity** to the minimum possible value of **0**, all the way to the left. It should read **Step size: 32 slots (1 sample)**.
- In **Settings > Video**:
  - Disable **Use full refresh rate when synchronizing video**. This is known to cause problems in cases where the reported refresh rate does not match the actual display refresh rate.
  - Disable **Synchronize video in windowed mode** and/or **Synchronize video in full screen mode**.
  - Enable **Threaded VDP2 renderer**.
  - Enable **Use dedicated thread for deinterlaced rendering**.
  - Try enabling or disabling **Threaded VDP1 renderer**.
  - Disable the **Deinterlace video** enhancement.
  - Disable the **Transparent meshes** enhancement.
- In **Settings > General**:
  - Disable the **Rewind Buffer**. (Shortcut is F8 by default.)
    - You can also find this option in the **Emulation** menu.
    - If you wish to use the Rewind Buffer, try lowering the **Compression level** in **Settings > General**. This will increase memory usage.
  - Enable **Boost process priority**.
  - Enable **Boost emulator thread priority**.
- If you're experiencing stutters during gameplay:
  - Use an uncompressed disc image (anything other than CHD).
  - Load disc images from a fast local disk, preferably an SSD.
  - Go to **Settings > General** and enable **Preload disc images to RAM**. This will increase memory usage and hang the emulator for a while when loading discs, but should eliminate all stutters.
- Use the AVX2 version if you can. If the emulator crashes right away, it's likely that your CPU doesn't support the instruction set, so you're stuck with the SSE2 version.

If Ymir still runs poorly after trying these, your CPU might be too slow for the emulator. It's known to run fine on CPUs that score around 1500 points on the [CPUBenchmark single thread test](<https://www.cpubenchmark.net/single-thread>), but I recommend CPUs that score 2000 points or higher.
A quad core CPU or better will help with threaded VDP1/VDP2 rendering, threaded deinterlace and the rewind buffer.


## General issues on Linux

For typical Linux systems:
- The system needs to provide an XDG-compliant desktop environment for file and folder selection dialogs. Without it, your only option to load discs is by dragging files onto the window or launching the emulator with a disc path via the command line.
- Ymir needs access to the filesystem to load game discs. The Flatpak release in particular is prone to causing problems due to its sandboxing rules.
- The ALSA driver (if present) must be set up correctly for the emulator to launch. Some systems or setups have broken ALSA drivers or incorrect file access permissions causing the application to crash.
- Some SDL3 crashes can be worked around by launching the application with the `SDL_AUDIODRIVER` environment variable set to `alsa`.

If you're using a Linux distribution that deviates too much from a typical Debian/Fedora/Arch setup, you'll likely encounter various issues launching or using the emulator.
- If you're on NixOS, try using `steam-run` to launch the emulator.
- If you encounter any issues not listed here, you're on your own. Feel free to open a PR to include new troubleshooting instructions here!
