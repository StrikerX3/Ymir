# Contributing to Ymir

You can contribute to the project by:
- Reporting bugs
- Requesting features or improvements
- Proposing pull requests

The sections below explain how each of them work.

## Reporting bugs

Before reporting bugs, make sure you also test the [latest nightly build](https://github.com/StrikerX3/Ymir/releases/tag/latest-nightly) -- there's a chance the bug might've been already fixed.
Also refer to the [troubleshooting guide](TROUBLESHOOTING.md) for further instructions that might solve the issue.

Make sure to test the game with the recommended or accurate presets -- go to **Settings > Tweaks** and click the **Recommended** or **Best compatibility/accuracy** buttons in all sections. Specifically:
- SH-2 clock ratio **must be** at 100%. Users reporting issues where this option is not at the default setting will be asked to retry with this option at 100%.
- Graphics issues that only occur with the **Deinterlace video** or **Transparent meshes** must be reported as such.
- Games that are fixed by enabling **Emulate SH-2 cache**, tuning **Emulation step granularity** or enabling **Use low level CD Block emulation** should be reported in the [compatibility list](https://docs.google.com/spreadsheets/d/1SLZzL9LelSlpEmTKy8cjaQnE7mew2uW1rfCgcekO58Q/edit?usp=sharing) instead. They might be added to the internal database if this is the only way they will work.
- Any threading issues should be reported in the dedicated issues. Threaded VDP2 and deinterlacing never cause issues, but threaded VDP1 and SCSP might.

If the bug persists, check that it hasn't already been reported by searching through the [Issues](https://github.com/StrikerX3/Ymir/issues). If you can't find an existing report, open a new one and provide the following information:
- Ymir version(s) tested (go to **Help > About** and click **Copy version**)
- The full set of tweakable options (go to **Settings > Tweaks** and click **Copy to clipboard** under the blue box. You can paste this directly in GitHub issues to get a nicely-formatted list)
- Game name/title, if the bug is specific to a game
- Game disc image format (CUE + single BIN, CUE + multiple BINs, MAME CHD, etc.)
- Instructions for reproducing the bug
- Expected vs. actual behavior, with screenshots/videos if applicable
- If the error occurs at a late stage in the game, provide a save/backup file and instructions to reach the point
  - Save states can be provided for difficult to execute actions such as complex fighting game combos
    - If you provide a save state from a nightly build, you must include the emulator version used to generate it

Additional information you can provide that might help identify the issue faster:
- Emulation settings/tweaks (open **Settings > Tweaks** and click **Copy to clipboard**)
- Last known working version, if you tested multiple versions and found one that works
- Output from other emulators (preferably Mednafen), or ideally output from a real Sega Saturn console
- Operating system, CPU (SSE2/AVX2/ARM), GPU for problems that affect the application as a whole (e.g. Ymir itself freezes, crashes, fails to start, etc.)

The more details, the easier it is to find and fix a bug. Don't go overboard, though! Keep it simple and straight to the point.

## Requesting features or improvements

As with bugs, make sure the feature hasn't been already requested by searching existing [Issues](https://github.com/StrikerX3/Ymir/issues).

The issue should explain what you want to see added or improved in Ymir. Provide examples, screenshots or links to other emulators or applications for inspiration.

## Proposing pull requests

Pull requests must explain what they're proposing and the rationale behind changes. Provide links to existing bugs they fix or features they implement if applicable.

Code contributions must follow the code standards and formatting guidelines described below.

### AI usage guidelines

Ymir's code is (to the author's knowledge) entirely human-written; AI has only been used to acquire knowledge.

If you plan to use AI-enabled tools such as agent-enabled IDEs, CLIs, or integrated LLM-based code generation/assistance, please follow these guidelines:
- Clearly state that you've used such tools in the PR description, and in what way they have assisted you. Don't claim that the work is entirely your own if that is not the case.
- Prove that you understand what you're doing by taking the time to manually write PR and commit descriptions. LLM-generated descriptions are often too verbose, too broad, too generic, or lack contextual clues to explain the "why" no matter how detailed your prompt is. If you have the time to spend writing an elaborate prompt, you certainly have the time to write the PR description itself.
- If you're a first-time contributor, resist the urge to ask AI to do the work for you. You're sabotaging yourself by not getting hands-on experience with the project on your own.
- Keep the scope of the work constrained to something manageable by one person. If you can't explain what the PR does without the help of AI because it's too big or complex, chances are it's also too big and complex for maintainers to review and reason about.
- Conversely, if the work is small enough that you could do it by hand, *do it by hand*. It's the perfect learning opportunity and shows you are actively interested in contributing to the project.

Failing to follow these guidelines will likely get your PR closed.

I'd love to apply a zero-tolerance policy, but that is simply infeasible. LLMs are a reality and lots of people use them; this isn't going to change.
These usage guidelines instead promote self-improvement, prevent AIs from running the show, and should result in more productive discussions and contributions.
With that said, there should be no situation where AI assistance is *needed*.

### Coding guidelines

Avoid static initializers and global objects. These should only be used for process-wide features, usually dealing directly with operating system functionality such as controlling the mouse cursor or managing virtual memory.
Ymir puts everything into objects for a good reason - you can run multiple emulator cores in a single process for advanced features like parallel frame search or reuse components to create a VDP debugger with an independent VDP renderer, for example.

Do use classes and light OOP. Prefer composition over inheritance and avoid `virtual` functions if possible, especially in hot paths.
Avoid tightly coupling objects - use callbacks, interfaces or similar forms of indirection.

Keep emulation and frontend code separated. The core does not have to concern itself with frontend logic except for supporting code. This allows the core to be ported to as many systems as possible.
OS-specific features (such as graphics APIs, virtual memory management or synchronization primitives) may be used in the core library if they offer better performance or more features than the standard C++ library equivalents.

Put emulator types under the `ymir` namespace, preferably nested in its component namespace (e.g. `ymir::vdp` for all VDP types). Use further nesting to avoid name clashes or group related functionality if necessary.

Prefer the `class` keyword for objects with complex behavior and `struct` for POD types.

Prefer `enum class`/`enum struct` over plain `enum`s. If you need to define bitfields, use the [bitmask_enum.hpp](/libs/ymir-core/include/ymir/util/bitmask_enum.hpp) helper.
If you use plain `enum`s, prefix every entry with a common name or put them in a namespace or empty struct to avoid name clashes.

Do use and extend existing utility classes throughout the code base. Utility types in [ymir-core](/libs/ymir-core/include/ymir/util) are also available in library consumers such as the SDL3 frontend app.

Do use Ymir's [core types](/libs/ymir-core/include/ymir/core/types.hpp) (`uint8..64` and `sint8..64`). Prefer these sized ints if the values are known to have a specific bit width.

Do write comments explaining complex implementations (the "why") and to separate logical chunks of code for clarity. Line separators should extend to 80 columns.

Do write Doxygen documentation blocks using triple-slash comments (`///`) or double-asterisk multiline blocks (`/** ... */`) and at-directives (`@brief`, `@param[in,out]`, `@return`, etc.).

In hot code paths, performance trumps clean code. Use all tricks under your sleeve, but don't abuse undefined or compiler-specific behavior.
Intrinsics and OS-specific functions are OK, relying on member function pointer layouts of a particular compiler is not.

Keep in mind Ymir compiles with MSVC, GCC and Clang on Windows, Linux, macOS and FreeBSD.
If you rely on OS-specific behavior, you should implemented equivalent versions for all of these OSes and/or provide a generic fallback implementation relying on the standard C++ library.
See [util/event.cpp](/libs/ymir-core/src/ymir/util/event.cpp) for an example that covers all bases - Windows, Linux, macOS, FreeBSD and a generic implementation.

Accuracy trumps performance, unless it comes at a high cost for little benefit. If the accurate option is too expensive, provide runtime configuration and generate separate code paths for both options.
See [saturn.hpp](/libs/ymir-core/include/ymir/sys/saturn.hpp) (`m_runFrameFn` and other function pointers), [sh2.hpp](/libs/ymir-core/include/ymir/hw/sh2/sh2.hpp) (`template <bool debug>`) and [scsp.hpp](/libs/ymir-core/include/ymir/hw/scsp/scsp.hpp) (`OnSlotTickEvent`, `OnSampleTickEvent`, `OnTransitionalTickEvent`) for examples.

Any changes to the current hot code paths (SH2 interpreter, VDP2 software renderer, SCSP DSP) must be benchmarked to ensure no performance regressions.

Adhere to the code formatting rules. Use `clang-format` to format the code.

When adding new dependencies to `ymir-core`, **never** use vcpkg ports; always use Git submodules under [vendor/](/vendor). This allows the repository to be added as subproject in other CMake projects.
Other targets may use vcpkg ports, but Git submodules (or FetchContent) is preferred.
If cloning submodules, use HTTPS, not SSH, as some build pipelines won't be able to clone GitHub repos without an SSH key.
Make a custom CMakeLists.txt if the dependency's own file doesn't behave well as a dependency or if you only need a subset of functionality from the library.
See existing vendored dependencies for examples and the comments in [vendor/CMakeLists.txt](/vendor/CMakeLists.txt) for more details.

### Code formatting/style

The project includes a [`.clang-format`](/.clang-format) settings file which applies to all C/C++ code (except for vendored dependencies).
Make sure to run your IDE's automatic code formatting or run `clang-format` on all modified files.

### Naming conventions

- All source file names must use `lower_snake_case`. We use `.cpp` for C++ source files and `.hpp` for C++ header files. C source/header files must use the `.c`/`.h` extensions.
- All class, struct and enum names must use `PascalCase`.
  - Abstract classes that represent interfaces must be prefixed with a capital `I`. For example, `IBackupMemory`.
- All functions must use `PascalCase` (both free and member functions), except for frequently-used utility functions such as `bit::extract` which uses `lower_snake_case` like C++ library functions.
- Local variables must use `camelCase`.
- Template type parameters must be either single-letter capitals like `T` or `U` if they represent an unspecified/unconstrained generic type or use `PascalCase` prefixed with capital `T`, such as `TTemplateType`.
- Template non-type parameters must use `camelCase`.
- Concepts must prefer `lower_snake_case`, but may also use `PascalCase`, whichever feels more appropriate in context.
- Public class/struct fields in POD types must use `camelCase`.
- Public class/struct fields which change the object's behavior must use `PascalCase`, such as `Open` in GUI window types.
- Protected and private fields must use `camelCase` prefixed with `m_`, such as `m_someField`.
- Static fields must use `camelCase` prefixed with `s_`, such as `s_someStaticField`.
- Global objects (if used) must be prefixed with `g_` and  use `camelCase`, such as `g_someGlobal`.
- `constexpr` constants must be prefixed with `k` and use `PascalCase`, such as `kSomeConstant`.
- Capitalization of acronyms such as VDP2, SCU, SCSP:
  - Must be preserved in `PascalCase`, as in `IVDP2Renderer`
  - Must be switched to all-lowercase if starting a `camelCase` name, as in `vdp2Renderer`
    - May be preserved if the name has a prefix, such as `m_VDP2Renderer`
  - Must remain all-uppercase if in the middle or the end of a `camelCase` name, as in `softwareVDP2Renderer`
  - Must be switched to all-lowercase for filenames, as in `vdp2.cpp`
- Avoid excessive abbreviation. Only use well-known abbreviations such as `num` or `calc`.
  - Also avoid excessive verbosity. `numberOfElementsInList` is bad; `numListElems` is good; `listSize` is better.
- Follow original documentation naming conventions for Saturn components: VDP1, VDP2, SCSP, SCU, etc.
  - The same applies to external components such as the SH-2 and MC68EC000 CPUs. The two main CPUs are referred to as MSH2 and SSH2 in Ymir, abbreviating Hitachi/Renesas's naming conventions and following names seen in datasheets.
