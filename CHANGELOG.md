# Ymir changelog

## Version 0.1.8

In development.

Introduced save state file version 9.

### New features and improvements

- App: Disable rounded window corners on Windows 11.
- App: Implement exception handler for macOS. (#460; @Wunkolo)
- App: Provide user feedback if any part of the app initialization fails.
- App: Show warning dialog if the user is missing the required ROM cartridge images for games that require them.
- Backup RAM: Per-game internal backup RAM file names changed from `bup-int-[<game code>] <title>.bin` to `bup-int-<title> [<game code>].bin` to allow sorting files alphabetically in file browsers. Existing files will be automatically renamed as they are loaded.
- Build: FreeBSD support for ARM64 systems. (#421; @bsdcode)
- Build: ARM64 Windows support. (#483; @tordona)
- Cart: Automatically insert Backup RAM cartridges for games that recommend their use, such as Dezaemon 2 and Sega Ages - Galaxy Force II. (#356)
- CD Block: Allow querying files at specific frame addresses and display file being read in System State window.
- Debug: Allow exporting debug output to a file.
- Debug: Move debug port writes to a callback and remove them from the SCU tracer. Eliminates the need for debug tracing to use Mednafen's debug output method.
- Debugger: Implemented SH-2 watchpoints. (#22)
- Input: Add support for loading an external game controller database and include a [community-sourced database](https://github.com/mdqinc/SDL_GameControllerDB) in builds.
- Input: Added hotkey for exiting the application, requiring a key combo to trigger: at least one key modifier (Ctrl, Alt, Shift, Option, etc.) and one other key (e.g. Ctrl+Shift+Q). (#160)
- Media: Cache CHD hunks for improved performance at the cost of extra RAM usage.
- Media: Provide basic error feedback when attempting to load bad, corrupt or truncated disc images.
- SCSP: Basic debugger view for all slot registers and some state.
- SCSP: Final output oscilloscope view.
- VDP1: Optimize line plotting by skipping lines that are entirely out of the system clipping area.
- VDP1: Optimize mesh polygons by limiting updates to system clip area.
- VDP1: Simplify mesh rendering code for slightly improved performance.
- VDP1: Various performance micro optimizations.
- VDP2: Basic debugger view for NBG0-3 and RBG0-1 parameters.

### Fixes

- App: Set en-US UTF-8 locale globally. Fixes CHD loader unable to load files with Unicode characters in their names.
- CD Block: Prevent a crash when attempting to set up subcode transfers without an active track.
- CD Block: Soft reset fixes. (thanks to @celeriyacon)
- CD Block: Use CD Block clock ratios instead of SCSP's for drive state update events.
- CD Block: Various state transition and playback nuances. (thanks to @celeriyacon)
- CD Block: Various Put/Get/Delete Sector Data nuances. (thanks to @celeriyacon)
- Input: Reset inputs when unbinding inputs or disconnecting gamepads.
- Media: Adjust sectors offsets when reading CHD images with multiple data tracks. Fixes some Last Bronx (USA) CHD images not booting. (#238)
- Media: Fix handling of Unicode characters when loading or saving the recent game disc list.
- Media: Ignore absolute paths when loading images from CUE sheets; load from the same directory as the CUE sheet instead.
- Media: Properly handle UTF-8-encoded CUE files referencing other files with Unicode characters.
- Media: Rewrite CUE parser to hopefully fix some audio skipping issues.
- SCSP: Allow M68K to fetch instructions from SCSP registers. Fixes CroNSF audio playback.
- SCSP: Don't use SBCTL on slots that are playing samples from Sound RAM when the EG reaches the silence threshold. Fixes busted audio in Guardian Heroes and Elevator Action^2. (#155)
- SCSP: Silence audio when MVOL=0. Fixes lingering sound/music when pausing in Sega Ages - Galaxy Force II. (#427)
- SCU: Timer 1 was never triggering when configured to trigger on Timer 0 match of 0x000.
- SH2: Fix byte order of direct cache data accesses. (thanks to @celeriyacon)
- SH2: Fix MOVA offset when in delay slot. (thanks to @celeriyacon)
- SH2: Swap memory read order for MAC.W and MAC.L operands. (thanks to @celeriyacon)
- SMPC: Clear SF (with a delay) when receiving an INTBACK break request. Fixes Phantasy Star Collection hanging on a black screen after SEGA licensing screen.
- SMPC: Fix register reads/writes. (thanks to @celeriyacon)
- SMPC: Optimized INTBACK flag is inverted. (thanks to @celeriyacon)
- SMPC: Time out pending INTBACK at VBlank IN if no Continue or Break requests are received until then. Fixes inputs in both Discworld versions. (#245; thanks to @celeriyacon)
- SMPC, VDP: Trigger optimized INTBACK more consistently closer to 1ms before VBlank IN depending on vertical resolution.
- System: Reset clock speed when soft resetting the system.
- VDP1: Clear transparent mesh layer to 0 instead of the erase write value when erasing framebuffer. Fixes Rayman's level loading screens rendering at half brightness when using the transparent meshes enhancement.
- VDP1: Cycle-count VBlank erase process. Fixes flashing subtitles in Panzer Dragoon FMVs.
- VDP1: Don't swap gouraud values when horizontal and/or vertical flip is enabled for an untextured polygon. Fixes bad shading in Croc - Legend of the Gobbos. (#543)
- VDP1: Improve performance by avoiding double-writing the same pixels in the erase process low resolution modes. Also fixes erase process cycle counting in high resolutions.
- VDP1: Pixel-perfect rendering. (thanks to Lordus)
- VDP1: Delay PTM=1 drawing start to dodge some timing issues with games that trigger drawing too early. Fixes flickering glass shard in Fighter's History Dynamite's intro sequence. (#503)
- VDP1: Primitive cycle counting to work around some games that horribly abuse the VDP1, such as Baroque, Dark Seed II, and Funky Fantasy. (#311, #316, #364)
- VDP1: Rework scaled sprite rendering to correctly handle undocumented zoom point settings.
- VDP2: Adjust line width for RBG line color insertion. Fixes half of the field not being colored in World League Soccer '98. (#517)
- VDP2: Always read line screen scroll data even for disabled NBGs. Fixes one-frame offset on system settings screen and Deep Fear's GUI elements.
- VDP2: Don't use supplementary data for characters in inaccessible VRAM banks. Fixes columns of "A"s in Darklight Conflict. (#545)
- VDP2: Double vertical window coordinates when the display is in single-density interlaced mode. Fixes the bottom half of the screen missing in Pro-Pinball: The Web. (#371)
- VDP2: Fix CRAM address bits shuffling. Fixes unexpected graphics showing up before the intro FMV in Saturn Bomberman. (#434)
- VDP2: Fix CRAM address calculation for RBG line colors. Fixes wrong colors for field shading in World League Soccer '98. (#517)
- VDP2: Fix exclusive monitor timings and resolution sizes, and out-of-bounds reads from lookup tables. (thanks to @celeriyacon)
- VDP2: Fix interlaced mode timings. (thanks to @celeriyacon)
- VDP2: Fix palette-based transparent meshes not blending with VDP2 layer (such as in Bulk Slash).
- VDP2: Fix window calculations involving illegal vertical coordinates. Fixes background glitch in Radiant Silvergun's Stage 2C.
- VDP2: Force-fetch first character of every scanline. Fixes some garbage tiles on the left edge of the screen in Athlete Kings' splash screen. (#515)
- VDP2: Handle games that only enable RBG1. Fixes missing background graphics in Houkago Ren'ai Club - Koi no Etude. (#551)
- VDP2: Handle sprite window on sprite layer manually. Fixes graphics effects when defeating the first boss in Metal Black. (#404)
- VDP2: Invert TVSTAT.ODD bit on single-density interlaced modes too. Fixes swapped interlaced fields in Pro-Pinball: The Web and Shienryuu. (#447)
- VDP2: Implement VCNT skip as a dedicated vertical phase. (thanks to @celeriyacon)
- VDP2: Latch TVMD.DISP at start of the frame. Fixes garbage graphics after loading screen in Samurai Spirits - Zankurou Musouken. (#472)
- VDP2: Move VCNT update to the left border horizontal phase where HBLANK switches to zero. (thanks to @celeriyacon)
- VDP2: Precompute per-dot rotation coefficient flag instead of deriving it twice per scanline.
- VDP2: Recalculate RBG0/1 page base addresses when changed mid-frame. Fixes broken ground on NiGHTS into Dreams's boss fights. (#423)
- VDP2: Reduce rotation parameter calculation precision to more closely match the real system.
- VDP2: Render transparent meshes onto a separate layer instead of immediately blending them onto the sprite layer. Fixes priority issues on Akumajou Dracula X when using the enhancement. (#484)
- VDP2: Rework bitmap delays due to bad VRAM access cycles configuration. Fixes shifted graphics in Baroque Report - CD Data File. (#524)
- VDP2: Store line color data for RBGs separately. Fixes sky box issues when jumping on the spring pad in Sonic Jam's Sonic World mode. (#83)
- VDP2: Use more accurate NTSC/PAL clock timings for video sync. Eliminates stutters with refresh rates that are very slightly lower than a perfect multiple of the NTSC/PAL frame rates.
- VDP2: Use the correct character fetcher for scroll RBGs. Fixes background glitches in Battle Monsters. (#340)


## Version 0.1.7

Released 2025-08-10.

Introduced save state file version 8.

### New features and improvements

- App: Added a button to copy the version string from the About window.
- App: Added hotkey to take screenshots (bound to F12 by default) with adjustable scaling from 1x to 4x. (#350)
- App: Added option to automatically load most recently loaded game disc image on startup.
- App: Auto-center About window whenever it is opened.
- App: Automatically detect profile path and allow using the user profile path from the OS's user home directory. (#411, #17; @bsdcode)
- App: Display error dialog on unhandled exceptions.
- App: Show actual emulation speed in title bar and frame rate OSD.
- App: Show actual VDP1 frame rate separated from VDP1 draw calls.
- Build: FreeBSD support for x86-64 systems. (#389; @bsdcode)
- Build: macOS builds are now universal -- one binary supports both Intel and Apple Silicon Macs. (#351; @Wunkolo)
- Build: Nightly builds are now available [here](https://github.com/StrikerX3/Ymir/releases/latest-nightly).
- Core: Improve manual reset event performance by using OS-specific implementations based on [cppcoro](https://github.com/lewissbaker/cppcoro).
- Debugger: Added CD Block filters view.
- Debugger: Added rudimentary SH-2 breakpoint management and per-game debugger state persistence. (#22)
- Debugger: Added SH-2 exception vector list view.
- Debugger: Allow suspending SH-2 CPUs in debug mode. (#22)
- Debugger: Implemented SH-2 breakpoints. (#22)
- Debugger: Introduced debug break signal that can be raised from just about anywhere. (#21)
- GameDB: Force SH-2 cache emulation for Astal, Dark Savior and Soviet Strike.
- GameDB: Implemented flag to force SH-2 cache emulation to specific games.
- Input: Categorized gamepad triggers and sticks as absolute axes. Absolute axes output fixed values at specific positions.
- Input: Categorized gamepad triggers as monopolar axes (having values ranging from 0.0 to 1.0) and gamepad sticks as bipolar axes (-1.0 to +1.0).
- Input: Implemented Arcade Racer peripheral. (#29)
- Input: Implemented Mission Stick peripheral with toggleable three-axis and six-axis modes. (#30)
- Video: Added hotkeys to rotate screen clockwise and counterclockwise. (#318)
- Video: Added option to reduce input lag by adjusting GUI frame rate to the largest multiple of the emulator's target frame rate that's not greater than the display's refresh rate.
- Video: Added option to reduce video latency by displaying the latest frame instead of the oldest when the emulator is running faster than the display's refresh rate.
- Video: Added option to synchronize video frames in windowed mode.
- Video: Avoid frame skipping on slow refresh rate monitors by disabling VSync if the target frame rate exceeds the display's refresh rate.
- Video: Simplify frame rate control in full screen mode.

### Fixes

- CD Block: Disconnect filter inputs for the fail target, not the filter itself. Fixes broken graphics in Ultraman Zukan's title screen. (#329)
- CD Block: Don't disconnect CD device from when setting the fail output of filter. Fixes Digital Dance Mix Vol. 1 - Namie Amuro playback.
- CD Block: Fix directory indexing for ReadDirectory and ChangeDirectory commands. Fixes Sega Rally Championship Plus (Japan) not booting.
- CD Block: Fix handling of "no change" playback end parameter. Fixes Astal taking a long time to load the first stage.
- CD Block: Properly read path table and directory records that cross the boundary between two CD sectors. Fixes Mizuki Shigeru no Youkai Zukan Soushuuhen booting back to BIOS. (#391)
- CD Block: Read subheader data from CD-ROM Mode 2 tracks only and fix their addressing. Fixes missing intro FMV in NiGHTS into Dreams... (#46)
- CD Block: Start new playbacks from starting FAD when paused. Fixes WipEout 2097 and XL boot issues. (#202)
- Media: Add support for CD-ROM Mode 2 tracks. Fixes Last Bronx not booting. (#238)
- Media: Compensate for INDEX 00 pregap in multi-indexed tracks in CUE sheets. Fixes partially skipped Minnesota Fats - Pool Legend voice lines. (#363)
- Media: Fix handling of pregap in data tracks in single BIN+CUE dumps. Fixes some Last Bronx dumps not booting.
- Media: Realign data offset to hunks between tracks in CHDs. Fixes some Last Bronx CHD dumps not booting.
- Save states: Added CD Block file system state to save state data.
- Scheduler: Ensure events are executed in chronological order.
- SCU: Fix A-Bus external interrupt handling.
- SCU: Fix DMA source address updates when source address increment is zero. Fixes background priority issue regression in Street Fighter - Real Battle on Film. (#168)
- SCU: Fix Timer 1 not triggering when the reload counter is larger than 0x1AA or 0x1C6 depending on horizontal resolution.
- SCU: Ignore/skip illegal DMA transfers in indirect transfer lists. Partially fixes corrupted or missing sprites in Fighting Vipers.
- SCU: Illegal DMA interrupts should not trigger VDP1 Sprite Draw End DMA transfers.
- SCU: Notify bus of DMA transfers.
- SCU: Prevent indirect DMA transfers from starting if the first entry is illegal. Stops Tennis Arena from destroying all memory.
- SCU: Use the source address increment for indirect DMA transfer data.
- Settings: Disable "Include VDP1 rendering in VDP2 renderer thread" by default and don't enable it in presets.
- Settings: Persist custom Screenshots profile path. (#398)
- Settings: Properly restore controller binds for controllers other than the Saturn Control Pad. (#397)
- SH2: Fix CPU getting stuck handling DMAC interrupts forever. Fixes Shellshock not booting. (#344)
- SH2: Handle sleep/standby mode and wake up on interrupts. Fixes Culdcept getting stuck on intro FMV and boosts overall performance on games that make use of the SLEEP instruction. (#346)
- SH2: Only clear zeroed out bits from write clear bitmask on FRT FTCSR writes. Fixes random lockups in Daytona USA. (#209) (thanks to @celeriyacon)
- SMPC: Fixed TH control mode reports on SH-2 direct mode. Fixes input response in World Heroes Perfect, Touge King the Spirits, Chaos Control Remix, and Father Christmas. (#297, #322, #348, #374)
- SMPC: Fixed TL reporting on SH-2 direct mode.
- VDP1: Disable "antialiasing" for lines and polylines.
- VDP1: Fix bad transparency caused by "illegal" RGB 5:5:5 color data (0x0001..0x7FFE). Fixes transparency in Sonic X-treme.
- VDP1: Process framebuffer swap slightly later in the VBlank OUT line. Fixes numerous issues:
	- Flickering graphics in DragonHeart - Fire & Steel, King of Fighters '96 / '97, Jantei Battle Cos-Player, PhantasM, Soviet Strike, Virtua Cop 2, and Yellow Brick Road (#272, #303, #334, #335, #336, #368)
	- Corrupted sprites in Center Ring Boxing, and Marvel Super Heroes (Shuma Gorath's Chaos Dimension move) (#72, #377)
	- Partially missing sprites in Fuusui Sensei - Feng-Shui Master (#405)
	- Freezes/crashes in Mahou no Janshi - Poe Poe Poemy, and Shockwave Assault (#378, #406)
- VDP1: Reorder LOPR, COPR, CEF and BEF updates. Fixes missing graphics in Virtual On - Cyber Troopers and Sega Touring CARS. (#112, #246)
- VDP1: Use SCU DMA bus notification to adjust VDP1 VRAM write timing penalty. Fixes hanging intro FMV in Sonic Jam without breaking Mega Man X3's sprites. (#83)
- VDP2: Always initialize and update background counters even for disabled layers. Fixes rolling screen in F-1 Challenge. (#300)
- VDP2: Block bitmap reads from VRAM banks without appropriate CP access. Fixes dirty graphics in NFL Quarterback Club 97's title screen. (#332)
- VDP2: Compute bitmap data access offsets when multiple chunks are read for an NBG. Fixes background offset in Doukyuusei - if while maintaining the slicing fix for Capcom Generation - Dai-5-shuu Kakutouka-tachi. (#384)
- VDP2: Don't apply sprite shadow if sprite priority is lower than the top layer. Fixes shadows drawing on top of objects in Blue Seed - Kushinada Hirokuden. (#349)
- VDP2: Don't blend line screen with layer 1 if line screen color calculations are disabled. Fixes fog in battle backgrounds in Zanma Chou Ougi - Valhollian. (#352)
- VDP2: Don't increment vertical scroll BG coordinate on complementary field lines when rendering deinterlaced RBG lines. Fixes jittery/interlaced Grandia FMVs when deinterlace is enabled.
- VDP2: Fix layer enable flags calculation when only RBG1 is enabled. Fixes missing background in MechWarrior 2's menus. (#413)
- VDP2: Fix line color insertion logic. Fixes erroneously blended ground in Athlete Kings. (#299)
- VDP2: Fix priority calculations for bitmap BGs. Fixes character sprites being drawn behind the background layer in Mr. Bones. (#247)
- VDP2: Fix race conditions with threaded deinterlacing causing some artifacts on RBGs in single-density interlaced mode.
- VDP2: Fix window indexing for RBGs in high resolution modes. Fixes stretched shadows in Last Bronx. (#395)
- VDP2: Handle RBG window pixels in high resolution modes. Fixes extra column of garbage in Athlete Kings. (#299)
- VDP2: Invert TVSTAT.ODD reads. Fixes garbled graphics on the top half of the screen in True Pinball. (#320)
- VDP2: RBG1 uses Rotation Parameter B, not A.
- VDP2: Reset NBG2/3 base vertical scroll counters when writing to SCYN2/3. Fixes garbled graphics in Marvel Super Heroes vs. Street Fighter during Shuma Gorath's Chaos Dimension move. (#72)
- VDP2: Rework rotation table calculations. Fixes warped ground on player two's screen in Sonic R multiplayer mode. (#401)
- VDP2: Skip calculation of VRAM PN/CP accesses for NBGs when RBG1 is enabled. Fixes missing car graphics in Gale Racer. (#359)
- VDP2: Swap even/odd field when entering VBlank.


## Version 0.1.6

Released 2025-07-20.

Introduced save state file version 7.

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

Released 2025-06-28.

Introduced save state file version 6.

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

Released 2025-06-02.

Uses save state file version 5.

### New features and improvements

- App: You can now drag and drop CCD, CHD, CUE, ISO or MDS files into the emulator window to load games.

### Fixes

- VDP2: Fix black screen on SSE2 builds.


## Version 0.1.4

Released 2025-06-02.

Introduced save state file version 5.

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

Released 2025-05-16.

Introduced save state file version 4.

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

Released 2025-05-04.

Introduced save state file version 3.

### New features and improvements

- Input: Improved support for gamepads. You can now configure triggers as buttons, map analog sticks to the D-Pad, adjust deadzones, and more. (#36, #37, #54)
- Input: Added one more slot for input binds

### Fixes

- Input: Keys no longer get stuck when focusing windows, menus, etc.
- Input: Keys bound to controller on port 2 (by default: arrow keys, numpad and navigation block) should no longer prevent keys bound to the controller on port 1 from working. (#50)
- Several stability improvements (@Wunkolo)


## Version 0.1.1

Released 2025-05-03.

Introduced save state file version 2.

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

Released 2025-05-01.

Initial release.

Introduced save state file version 1.
