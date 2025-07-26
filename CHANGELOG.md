# Ymir changelog

## Version 0.1.7

### New features and improvements

- App: Added a button to copy the version string from the About window.
- App: Added option to automatically load most recently loaded game disc image on startup.
- App: Auto-center About window whenever it is opened.
- Build: Nightly builds are now available [here](https://github.com/StrikerX3/Ymir/releases/latest-nightly).
- Core: Improve manual reset event performance by using OS-specific implementations based on cppcoro.
- Debugger: Added SH-2 exception vector list view.
- Debugger: Introduced debug break signal that can be raised from just about anywhere.
- Input: Categorized gamepad triggers and sticks as absolute axes. Absolute axes output fixed values at specific positions.
- Input: Categorized gamepad triggers as monopolar axes (having values ranging from 0.0 to 1.0) and gamepad sticks as bipolar axes (-1.0 to +1.0).
- Input: Implemented Arcade Racer peripheral. (#29)
- Input: Implemented Mission Stick peripheral with toggleable three-axis and six-axis modes. (#30)
- Video: Added hotkeys to rotate screen clockwise and counterclockwise. (#318)
- Video: Added option to reduce input lag by adjusting GUI frame rate to the largest multiple of the emulator's target frame rate that's not greater than the display's refresh rate.
- Video: Avoid frame skipping on slow refresh rate monitors by disabling VSync if the target frame rate exceeds the display's refresh rate.
- Video: Simplify frame rate control in full screen mode.
- Video: Switch from SDL3 Renderer to SDL3 GPU API.

### Fixes

- CD Block: Fix directory indexing for ReadDirectory and ChangeDirectory commands. Fixes Sega Rally Championship Plus (Japan) not booting.
- Media: Add support for CD-ROM Mode 2 tracks. Fixes Last Bronx not booting. (#238)
- Media: Fix handling of pregap in data tracks in single BIN+CUE dumps. Hopefully fixes some Last Bronx dumps not booting.
- Media: Realign data offset to hunks between tracks in CHDs. Fixes Last Bronx CHD dumps not booting.
- Save states: Added CD Block file system state to save state data.
- Scheduler: Ensure events are executed in chronological order.
- SCU: Fix A-Bus external interrupt handling.
- SMPC: Fixed TL reporting on SH-2 direct mode.
- VDP1: Fix bad transparency caused by "illegal" RGB 5:5:5 color data (0x0001..0x7FFE). Fixes transparency in Sonic X-treme.


## Version 0.1.6

### New features and improvements

- App: Added display rotation options for TATE mode games. (#256)
- App: Added frame rate OSD and hotkeys to toggle it and change positions.
- App: Added menu actions to resize window to specific scales.
- App: Added new 3:2 and 16:10 forced aspect ratio options.
- App: Added option to remember window position and size. (#4)
- App: Added save states to File menu.
- App: Added simple message overlay system to display some basic notifications. (#288)
- App: Display emulation speed in title bar and under speed indicator, and add a new indicator for slow motion. (#16)
- App: Improve full screen frame pacing even further by spin-waiting for up to 1 ms before the frame presentation target.
- App: Include timestamp on save states.
- App: Notify about loading/saving save states or switching save state slots.
- App: Smooth out frame interval adjustments in full screen mode.
- Backup Manager: Export "Vmem"-type BUP files by default.
- Backup Manager: Make all columns sortable.
- Backup Manager: Show logical block usage (matching BIOS numbers) + header blocks. (#294)
- Debugger: Added basic VDP1 registers inspector window.
- Input: Added new keybinds for frame rate limit control: increase/decrease speed, switch between primary/alternate speed, reset speed. (#16)
- Input: Changed default keybinds for Pause/Resume action from "Pause, Ctrl+P" to "Pause, Spacebar".
- Input: Removed Return from default binds to Port 1 Start button to avoid conflict with full screen hotkey (Alt+Enter).
- SCSP: Various micro optimizations.
- Settings: Added "Clear all" button to controller configuration window to clear all binds. (#288)
- Settings: Automatically create/suggest a backup RAM file if no path is specified when inserting the cartridge.
- SH2: Improve cache emulation performance by avoiding byte-swapping cache lines.
- SH2: Improve overall emulation performance by simplifying interrupt checks.
- System: Map 030'0000-03F'FFFF memory area.
- System: Map simple arrays directly as pointers into the Bus struct to improve overall performance.
- VDP2: Add dedicated thread for deinterlaced rendering if VDP2 threading is enabled. Significantly lessens performance impact of the deinterlace enhancement on quad-core CPUs or better.
- Video: Implemented frame rate limiter. (#16)

### Fixes

- App: Disable emulator-GUI thread syncing when not in full screen mode. Fixes emulator slowing down when running at 100% speed on displays with refresh rate lower than 60 Hz.
- App: Fix frame pacing and speed limiter on 50 and 60 Hz displays.
- CD Block: Fix handling of "no change" PlayDisc parameters. Fixes X-Men: Children of the Atom CDDA tracks not resuming after pausing. (#274)
- Debugger: Indirect SCU DMA transfers were being traced with the updated indirect table address.
- Input: Fix inability to bind keyboard combos.
- Input: Modifier keys can now be used correctly as controller input binds and will no longer interfere with other controller inputs. (#282)
- Media: Allow loading CUE files with PREGAP and INDEX 00 on the same TRACK.
- Media: Don't bother detecting silence in pregap area; trust the CUE files.
- Media: Skip blank lines in CUE files.
- Save states: Read/write missing SCSP field to save state object. Fixes occasional application crashes when using the rewind buffer in conjunction with save states.
- SCSP: Use EG level instead of total level in MSLC reads. Fixes missing/truncated SFX on various games, including Sonic R, Akumajou Dracula X and Daytona USA CCE.
- SCU: Allow SCU DSP program and data RAM reads or writes while the program is paused (thanks to @celeriyacon).
- SCU: DSP data RAM reads should return 0xFFFFFFFF while program is running (thanks to @celeriyacon).
- SCU: HBlank IN DMA transfers should not be gated by timers. Fixes non-scrolling Shinobi-X cityscape background. (#193)
- SCU: Improve HBlank IN, VBlank IN and VBlank OUT interrupt signal handling.
- SCU: Increment DMA source address by 4 after performing DMA transfers with no increment. Fixes background priority issues in Street Fighter - Real Battle on Film. (#168)
- SCU: Interleave SCU DSP DMA transfers with program execution when not writing to Program RAM or accessing the CT used by DMA (thanks to @celeriyacon).
- SCU: Rework SCU DMA transfers. Fixes displaced tile data in Steam-Heart's. (#278)
- SCU: Run all pending DMA transfers instead of just the highest priority.
- SCU: Split up MSH2/SSH2 interrupt handling.
- SCU: Various fixes to SCU DSP DMA transfers to DSP Program RAM (thanks to @celeriyacon).
- Settings: Reverse IPL column sorting order.
- SH2: Fix cache LRU AND update mask. Fixes FMV glitches on Capcom games, WipEout and Mr. Bones when SH-2 cache emulation is enabled. (#202, #247, #270)
- SH2: TAS.B read should bypass cache.
- SH2: The nIVECF pin of the SSH2 is disconnected, disallowing it from doing external interrupt vector fetches.
- SMPC: Delay all commands for slightly longer to allow Quake (EU) to boot with normal CD read speed (2x).
- SMPC: Fix automatic switch to PAL or NTSC to match area code more consistently.
- System: Only hard reset if SMPC area code actually changed.
- System: Tighten synchronization between SCU and SH-2 CPUs. Improves stability on WipEout (USA). (#202)
- VDP1: Double horizontal erase area when drawing low-resolution sprites with 8-bit data. Fixes right half of sprite graphics not cleaning up in Resident Evil's options menu. (#180)
- VDP1: Extend line clipping to the left and top edges by one pixel to compensate for some inaccuracies.
- VDP1: Fix end codes for 64 and 128 color sprites. Fixes white sprite outlines in Scud - The Disposable Assassin and broken sprites in Primal Rage. (#268, #280)
- VDP1: Hack in VDP1 command processing delay on VRAM writes. Fixes glitched sprites on Mega Man X3. (#244)
- VDP1: Include source color MSB when rendering polygons in half-luminance mode. Fixes intro FMV background on Crows - The Battle Action. (#107)
- VDP1: Mask CMDCOLR bits 0..3 in 4bpp banked sprite mode. Fixes palette issues in Steam-Heart's and Dragon Ball Z - Shinbutouden. (#69, #278)
- VDP1: Properly handle DIE/DIL in single-density interlaced mode. Fixes text drawn twice as tall in Resident Evil options menu. (#180)
- VDP2: Adjust character data offset for 2x2 characters in RGB 8:8:8 color format. Fixes garbled FMV in Crusader - No Remorse. (#108)
- VDP2: Apply character pattern delay based on first pattern name access, not all of them. Fixes shifted UI elements in Battle Arena Toshinden Remix. (#306)
- VDP2: Apply per-dot special color calculations to bitmap BGs. Fixes translucent UI in The Story of Thor. (#152)
- VDP2: Don't update line/back screen color, line screen scroll or rotation parameters when the display is disabled. Fixes blank screen during Sega Rally Championship boot up.
- VDP2: Fix per-dot special priority function. Fixes BG priority issues in Waku Waku Puyo Puyo Dungeon.
- VDP2: Fix single-density interlaced mode not actually interlacing the image.
- VDP2: Fix sprite layer display when rotation mode is enabled. Fixes sliding 3D graphics on Hang-On GP and Highway 2000. (#167, #208, #277)
- VDP2: Fix transparent VDP1 color data handling. Fixes missing graphics in Rayman's level select screens and Bubble Bobble's sky in the title screen. (#262)
- VDP2: Fix window short-circuiting logic. Fixes missing ground in Final Fight Revenge and incorrect UI elements in Sakura Taisen. (#104, #253)
- VDP2: Halve sprite layer width when drawing 8-bit sprite layer in low-resolution VDP2 modes. Fixes text drawn twice as wide in Resident Evil options menu. (#180)
- VDP2: Handle bad window parameters set by Snatcher on the "Act 1" title screen (and probably many other places). (#259)
- VDP2: Honor TVMD.BDCLMD when the display is disabled. Fixes screen transitions in Sega Rally Championship.
- VDP2: Ignore vertical cell scroll read cycles for NBGs that have the effect disabled. Fixes wavy background effect on stage 2 of Magical Night Dreams - Cotton 2. (#255)
- VDP2: Implemented rules for bitmap VRAM access delay. Fixes sliced images in Capcom Generation - Dai-5-shuu Kakutouka-tachi art gallery. (#254)
- VDP2: Latch BG scroll registers earlier (at VBlank OUT) and latch vertical scroll registers (SCY[ID]Nn). Fixes bad vertical offset in Shinobi-X's NBG2 layer. (#193)
- VDP2: Read first vertical cell scroll entry on bitmap backgrounds. Fixes misplaced lines in Street Fighter - Real Battle on Film FMVs. (#291)
- VDP2: The first vertical cell scroll entry read does not update the address. Fixes background offset on the first Rayman boss stage.
- VDP2: Update line screen scroll address at Y=0. Fixes line glitches in Rayman's backgrounds and Sonic Jam's Sonic 2 special stage graphics.
- VDP2: Update line screen scroll offsets only at the specified boundaries. Very slightly improves performance and fixes text slicing issues in Sega Rally Championship's Records and Options screens.
- VDP2: Update vertical cell scroll every 8 cell dots correctly when the background is zoomed in.
- VDP2: Update vertical scroll registers (SCY[ID]Nn) when written. Fixes background distortion effect of Shuma Gorath's Chaos Dimension super move in Marvel Super Heroes vs. Street Fighter. (#72)
- VDP: Fix handling of VDP1 threading flag when VDP2 threading is disabled.
- ymdasm: Fix reversed SCU DSP DMA immediate/data RAM operand decoding.
- ymdasm: Mask and translate several SCU DSP immediates.


## Version 0.1.5

### New features and improvements

- App: Added command-line option `-P` to force emulator to start paused.
- App: Added new Tweaks tab to Settings window consolidating all accuracy, compatibility and enhancement settings.
- App: Added option to create internal backup RAM files per game. (#99)
- App: Added option to override UI scale. (#251)
- App: Added option to toggle fullscreen by double-clicking the display. (#197)
- App: Added recent games list to File menu. (#196)
- App: Automatically center Settings window when opening it. (#251)
- App: Close windows when pressing B or Circle on gamepads while nothing is focused. (#251)
- App: Enable gamepad navigation on GUI elements. (#251)
- App: Store relative paths in Ymir.toml. (#207)
- App: Use window-based DPI to adjust UI scale, allowing the UI to adapt to displays with different DPI settings. (#221; @Wunkolo)
- Backup RAM: Support interleaved backup image formats such as the ones produced by Yaba Sanshiro or the MiSTer core. (#87)
- Backup RAM: Support standard BUP backup files. (#87)
- SCSP: Added option to increase emulation granularity for improved timing accuracy (thanks to @celeriyacon).
- SCSP: Double-buffer DSP MIXS memory (thanks to @celeriyacon).
- SCSP: Implemented MIDI In and Out. (#258; @GlaireDaggers)
- SCSP: Interleave DSP execution and slot processing (thanks to @celeriyacon).
- VDP1: Added option to replace meshes with 50% transparency.
- VDP1: Clip sprites to visible area to speed up rendering, especially of very large sprites.
- VDP: Added option to deinterlace video. (#66)
- VDP: Moved VDP1 rendering to the emulator thread to improve compatibility with some games (e.g. Grandia) and added an option to move it back to the VDP rendering thread. (#233)

### Fixes

- App: Fix rare crash when loading a backup memory image in the Backup Memory Manager.
- App: Fix window scaling on macOS Retina displays when using HiDPI mode. (#221, #266; @Wunkolo)
- App: Prevent loading internal backup memory image as backup RAM cartridge image.
- CD Block: Start new playbacks from starting FAD when previous playback has ended. Fixes WipEout freeze after SEGA logo and many other freezes, no-boots and softlocks.
- Media: Fix pregap handling in single BIN images.
- SCSP: Apply DAC18B to output (thanks to @celeriyacon). Fixes quiet audio in many games. (#237)
- SCSP: Fix loss of accuracy on MIXS send level calculation (thanks to @celeriyacon).
- SCSP: Fix send level, panning and master volume calculations.
- SCSP: Fix slot output processing order (thanks to @celeriyacon).
- SCSP: Fix swapped DAC18B and MEM4MB bits (thanks to @celeriyacon).
- SCSP: Run one additional DSP step to fix FRC issues (thanks to @celeriyacon).
- SCU, SH-2, SMPC, SCSP, VDP: Numerous fixes to interrupt handling (thanks to @celeriyacon). Fixes intermittent Rayman inputs and some audio glitches.
- SCU: Various DSP accuracy fixes (thanks to @celeriyacon).
- SH2: More fixes to FRT, WDT and DIVU (thanks to @celeriyacon).
- SMPC: Cancel scheduled command processing event when resetting SMPC. Fixes a long hang after hard resetting in some cases.
- SMPC: Change fixed bits from 111 to 100 in TH/TR control mode responses for the first data byte of the Control Pad and 3D Control Pad peripherals. Fixes Golden Axe booting back to BIOS. (#231)
- SMPC: Eliminate spurious INTBACK interrupts.
- SMPC: Prevent COMREG writes when a command is in progress. Fixes some boot issues leading to the "Disc unsuitable for this system" message. (#219)
- SMPC: Prevent optimized INTBACK report from occurring unless a continue request was sent. Fixes input issues with Yaul-based homebrew.
- SMPC: Prioritize INTBACK continue requests over break requests.
- System: Tighten synchronization between the two SH-2 CPUs and remove artificial timeslice limit. Improves performance and fixes Fighters Megamix and Sonic Jam intermittent boot issues. (#236, #242)
- VDP1: Lower command limit to work around problematic games that don't set up a terminator in the command table. (#213, #216)
- VDP1: Significantly slow down command execution when running the VDP1 renderer on the emulator thread. Fixes Dragon Ball Z - Shinbutouden freeze after SEGA logo. (#233)
- VDP2: Apply horizontal mosaic effect to rotation background layer. Fixes missing effect on Race Drivin' Time Warner logo. (#267)
- VDP2: Apply window effect to sprite layer. Fixes graphics going out of bounds in many games. (#173)
- VDP2: Check for invalid access patterns to determine if NBG characters should be delayed. Fixes background offsets in many games. (#169, #190, #226)
- VDP2: Disable NBG1-3 only if both RBG0 and RBG1 are enabled simultaneously.
- VDP2: Honor access cycles and VRAM bank allocations to restrict pattern name and character pattern accesses. Fixes garbage graphics in Panzer Dragoon Saga, Sonic 3D Blast and Street Fighter Alpha/Zero 2. (#203, #213)
- VDP2: Invert back screen color calculation ratio. Fixes black background on Sakura Taisen FMVs. (#241)
- VDP2: Move existing VCounter into VDP2 VCNT register. Fixes Assault Suit Leynos 2 freeze when going in-game and King of Fighters '95 not booting. (#75)
- VDP2: Synchronize background enable events with the renderer thread. Fixes FMV slicing issues on slow machines on Sakura Taisen.
- ymdasm: Fix SCU DSP unconditional JMP disassembly.


## Version 0.1.4+1

### New features and improvements

- App: You can now drag and drop CCD, CHD, CUE, ISO or MDS files into the emulator window to load games.

### Fixes

- VDP2: Fix black screen on SSE2 builds.


## Version 0.1.4

### New features and improvements

- App: Added option to pause emulator when the window loses focus. (#181)
- App: Added shadow under playback indicators to make them visible on white backgrounds.
- App: Automatically adjust scaling when system-wide DPI is changed. (@Wunkolo)
- App: Changed background color around screen to black on windowed mode.
- CD Block: Implement Put Sector command, used by After Burner II. (#78)
- Core: Performance improvements, especially for ARM builds. (@Wunkolo)
- Debug: Simple CD Block commmand tracer window.
- Input: Implemented 3D Control Pad. (#28)
- Media: Preliminary support for CHD files. (#48)
- Media: Support multi-indexed audio tracks (BIN/CUE only). (#58)
- SMPC: Set SF=0 on unimplemented commands so that games can move forward.
- SH-2: Build infrastructure needed to honor memory access cycles for improved performance and accuracy.
- SH-2: Slow down accesses to on-chip registers to 4 cycles.
- VDP: Rewrite VDP2 frame composition code to use SIMD on x86 and ARM for improved performance. (@Wunkolo)

### Fixes

- App: Customized profile paths are now created at the specified location instead of the default. (#119, #126; @lvsweat)
- CD Block: Clear partitions and filters on soft resets triggered by Initialize CD System command. Fixes some game boot issues.
- CD Block: Clear the "paused due to buffer exhausted" flag when SeekDisc command pauses playback. Fixes Sakura Taisen 2 read errors after FMVs.
- CD Block: Don't clear the file system when opening the tray.
- CD Block: Fix audio track sector sizes. Fixes some CD audio track playback glitches with certain images (particularly MDF/MDS).
- CD Block: Fix Delete Sector end position when sector count is FFFF. Fixes some game boot issues.
- CD Block: Fix directory indexing. Fixes one of Assault Suit Leynos 2 crashes on startup. (#127)
- CD Block: Free last buffer from partition when ending a Get Then Delete Sector transfer when the last sector isn't fully read. Fixes some game boot issues.
- IPL: Automatically load IPL ROM when switching disc images. (#128)
- M68K: Soft reset CPU when executing the `RESET` instruction. Fixes OutRun getting stuck on its own SEGA logo.
- Media: Fix crash when parsing CUE sheets with non-contiguous tracks.
- SCSP: Don't mirror sound RAM on 5A8'0000-5AF'FFFF. Fixes After Burner II audio and M68K crashes.
- SCU: Rework interrupt handling. Fixes Rayman inputs. (#59)
- SCU: Set ALU = AC before running DSP operations. Fixes Quake crash on boot. (#156)
- SCU: Timer enable flag applies to both timers. Fixes background priority issues in Need for Speed.
- SH-2: Fix PC offsets for exceptions, interrupts, TRAPA and RTE. Fixes some game boot issues.
- SH-2: Fix PC offsets for `mova`, `mov.w` and `mov.l` with `@(disp,PC)` operand (thanks to @celeriyacon).
- SH-2: Fixes and accuracy improvements to DIVU (thanks to @celeriyacon).
- SH-2: Fixes and accuracy improvements to FRT (thanks to @celeriyacon). Fixes freezes in Daytona USA. (#7)
- SH-2: Fixes and accuracy improvements to WDT (thanks to @celeriyacon).
- SH-2: Lazily update WDT and FRT timers. Provides a 5-10% performance boost *and* improves accuracy!
- SMPC: Various INTBACK handling adjustments. Partially fixes Assault Suit Leynos 2 no-boot issues.
- System: Fix cycle counting on the main loop not taking into account the number of cycles taken by the CPUs, resulting in undercounting timers.
- VDP1/2: Fix handling of 16-bit sprite data from VDP1 when VDP2 uses 8-bit sprite types. Fixes sprites in I Love Mickey Mouse/Donald Duck.
- VDP2: Allow 8-bit reads and writes to VDP2 registers.
- VDP2: Apply transparency to mixed-format sprite data when rendering the special value 0x8000. Fixes Assault Suit Leynos 2 black screen after loading.
- VDP2: Don't increment vertical mosaic counter if mosaic is disabled. Fixes text boxes and character portraits in Grandia. (#91)
- VDP2: Fix bitmap base address for RBGs. Fixes several graphics glitches on menus and in-game in Need for Speed.
- VDP2: Fix line screen scroll in double-density interlace mode. Fixes stretched videos in Grandia. (#91)
- VDP2: Fix special color calculation bits. Fixes Sonic R water effects. (#150)
- VDP2: Fix vertical cell scroll effect on games that set up access patterns that don't match the NBG parameters. Fixes Sakura Taisen 2 FMVs.
- VDP2: RBG0 was always being processed/rendered even when disabled.
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
