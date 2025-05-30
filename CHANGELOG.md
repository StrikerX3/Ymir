# Ymir changelog

## Version 0.1.4

### New features and improvements

- App: Changed background color around screen to black on windowed mode.
- CD Block: Implement Put Sector command, used by After Burner II. (#78)
- Core: Performance improvements, especially for ARM builds. (@Wunkolo)
- Debug: Simple CD Block commmand tracer window.
- Input: Implemented 3D Control Pad. (#28)
- Media: Preliminary support for CHD files. (#48)
- Media: Support multi-indexed audio tracks (BIN/CUE only). (#58)
- SH-2: Simulate fetch and decode pipeline stages for improved accuracy.
- SMPC: Set SF=0 on unimplemented commands so that games can move forward.
- VDP: Rewrite VDP2 frame composition code to use SIMD on x86 and ARM for improved performance. (@Wunkolo)

### Fixes

- App: Automatically adjust scaling when system-wide DPI is changed. (@Wunkolo)
- App: Customized profile paths are now created at the specified location instead of the default. (#119, #126; @lvsweat)
- CD Block: Don't clear the file system when opening the tray.
- CD Block: Fix audio track sector sizes. Fixes some CD audio track playback glitches with certain images (particularly MDF/MDS).
- CD Block: Fix Delete Sector end position when sector count is FFFF. Fixes some game boot issues.
- CD Block: Fix directory indexing. Fixes one of Assault Suit Leynos 2 crashes on startup. (#127)
- CD Block: Free last buffer from partition when ending a Get Then Delete Sector transfer when the last sector isn't fully read. Fixes some game boot issues.
- IPL: Automatically load IPL ROM when switching disc images. (#128)
- M68K: Soft reset CPU when executing the `RESET` instruction. Fixes OutRun getting stuck on its own SEGA logo.
- SCSP: Don't mirror sound RAM on 5A8'0000-5AF'FFFF. Fixes After Burner II audio and M68K crashes.
- SCU: Rework interrupt handling. Fixes Rayman inputs. (#59)
- SH-2: Fix PC offsets for exceptions, interrupts, TRAPA and RTE. Fixes some game boot issues.
- SH-2: FRT fixes and accuracy improvements (thanks to @celeriyacon). Fixes freezes in Daytona USA. (#7)
- SMPC: Various INTBACK handling adjustments. Partially fixes Assault Suit Leynos 2 no-boot issues.
- VDP1/2: Fix handling of 16-bit sprite data from VDP1 when VDP2 uses 8-bit sprite types. Fixes sprites in I Love Mickey Mouse/Donald Duck.
- VDP2: Allow 8-bit reads and writes to VDP2 registers.
- VDP2: Don't increment vertical mosaic counter if mosaic is disabled. Fixes text boxes and character portraits in Grandia. (#91)
- VDP2: Fix bitmap base address for RBGs. Fixes several graphics glitches on menus and in-game in Need for Speed.
- VDP2: Fix line screen scroll in double-density interlace mode. Fixes stretched videos in Grandia. (#91)
- ymdasm: Fix file length when using a non-zero initial offset with the default length.


## Version 0.1.3

### New features and improvements

- Cartridge: Added 16 Mbit ROM cartridge for Ultraman: Hikari no Kyojin Densetsu and The King of Fighters '95. (#71)
- Cartridge: Added option to automatically load cartridges required by some games. (#98)
- Input: Categorize some actions as "triggers" (one-shot actions, optionally repeatable) to differentiate them from "buttons" (a binary state). This allows frame step to be repeated by holding the keyboard key bound to it.
- Input: Added a "Turbo speed (hold)" input bind that toggles turbo on and off. (#103)
- System: Automatically switch to PAL or NTSC based on auto-selected region.
- Save states: Automatically load IPL ROM matching the one used in a save state.
- Debugger: Added VDP2 layer toggles to Debug menu and in a new window.
- App: Allow customizing all profile paths. (#74)
- App: Add IPL ROM list sorting. (#92) (@Wunkolo)
- App: Add full screen mode (default shortcut: `Alt+Enter`) and command-line override `-f`. (#47)
- App: Improve frame pacing for a smooth full screen experience. (#97)
- App: Mitigate input lag in every mode (#101)
- App: Display reverse, rewind, fast-forward and pause indicators on the top-right corner of the viewport. (#103)
- Build: Added macOS builds. (huge thanks to @Wunkolo!)
- Core: Several performance and stability improvements. (@Wunkolo)

### Fixes

- VDP1: Preserve EWDR, EWLR and EWRR on reset. Fixes some graphics glitches on Capcom games. (#67)
- VDP2: RBGs would render incorrectly when starting the emulator with threaded VDP rendering disabled. (#77)
- VDP2: Honor access cycle settings (CYCA0/A1/B0/B1 registers) to fix vertical cell scroll effect. (#76)
- VDP2: Disable NBGs 1 to 3 if NBG0 or NBG1 use high color formats. (#76)
- VDP2: Apply mid-frame scroll effects properly. (#72)
- VDP2: Use the MSB from the final color value instead of the raw sprite data MSB, which fixes background priority bugs on Dragon Ball Z - Shinbutouden (#69)
- SCSP: More accuracy improvements and bug fixes (thanks to @celeriyacon)
- SCU: Fix repeated indirect DMA transfers when the write address update flag is enabled. Fixes a crash when going in-game on Shinobi X. (#84)
- Input: Assigning keys to connected controllers will no longer unbind keys from disconnected controllers.
- Rewind: Fix rare crash due to a race condition when resetting the rewind buffer.
- App: Fix handling of UTF-8 paths. (#88)
- Backup memory manager: Fix crash when loading an image with less files than the current image while having selected files at positions past the new image's file count.


## Version 0.1.2

### New features and improvements

- Input: Improved support for gamepads. You can now configure triggers as buttons, map analog sticks to the D-Pad, adjust deadzones, and more. (#36, #37, #54)
- Input: Added one more slot for input binds

### Fixes

- Input: Keys no longer get stuck when focusing windows, menus, etc.
- Input: Keys bound to controller on port 2 (by default: arrow keys, numpad and navigation block) should no longer prevent keys bound to the controller on port 1 from working. (#50)
- Several stability improvements (@Wunkolo)


## Version 0.1.1

### New features and improvements

- App: New logo and icon (thanks to @windy3862 on Discord!)
- App: Added Welcome window with instructions for first-time users
- App: Set initial window size based on display resolution
- App: Scale GUI based on system DPI scaling (#45)

### Fixes

- VDP1: Truncate polygon coordinates to 13 bits, fixing a short freeze in Virtua Fighter 2
- SCSP: Various accuracy improvements and bug fixes (thanks to @celeriyacon)
- CD Block: Fix errors when loading homebrew discs containing a single file
- Input: Properly handle gamepad buttons when binding inputs
- ymdasm: Fix disassembly skipping the very last instruction in files


## Version 0.1.0

Initial release.
