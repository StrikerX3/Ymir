# Compiling Ymir

Ymir requires [CMake 3.28+](https://cmake.org/) and a C++20 compiler.

The repository includes several vendored dependencies as Git submodules. When cloning, make sure to include the `--recurse-submodules` parameter.
You can also initialize submodules with `git submodule update --init --recursive` after a `git clone` or when pulling changes.

Ymir has been successfully compiled with the following toolchains:
- Visual Studio 2022's Clang 19.1.5
- Visual Studio 2022's MSVC 19.44.35213.0
- Clang 15.0.7 on WSL Ubuntu 24.04.5 LTS (`clang-15` / `clang++-15`)
- Clang 18.1.3 on WSL Ubuntu 24.04.5 LTS (`clang` / `clang++`)
- Clang 19.1.1 on Ubuntu 24.04.2 LTS (`clang-19` / `clang++-19`)
- GCC 14.2.0 on Ubuntu 24.04.2 LTS (`gcc-14` / `g++-14`)
- Clang 19.1.7 on FreeBSD 14.3-RELEASE (`clang19` / `clang++19`)
- Clang 21.1.0 on FreeBSD 14.3-RELEASE (`clang21` / `clang++21`)
- Apple Clang 17 on macOS 15 Sequoia

The project has been compiled for x86_64 and ARM64 Windows, Linux, FreeBSD and macOS platforms.

Clang is the preferred compiler for it's multiplatform support and excellent code generation. Ymir requires Clang 15 or later.


## Build configuration

You can tune the build with following CMake options:

- `Ymir_AVX2` (`BOOL`): Set to `ON` to use AVX2 extensions (on x86_64 platforms only). `OFF` uses the platform's default instruction set, typically SSE2. ARM64 platforms will always use NEON. Disabled by default.
- `Ymir_ENABLE_TESTS` (`BOOL`): Includes the unit test project in the build. Enabled by default if this is the top level CMake project.
- `Ymir_ENABLE_SANDBOX` (`BOOL`): Includes the sandbox project in the build. Enabled by default if this is the top level CMake project.
- `Ymir_ENABLE_YMDASM` (`BOOL`): Includes the disassembly tool project in the build. Enabled by default if this is the top level CMake project.
- `Ymir_ENABLE_IPO` (`BOOL`): Enables interprocedural optimizations (also called link-time optimizations) on all projects. Enabled by default.
- `Ymir_ENABLE_DEVLOG` (`BOOL`): Enables logs meant to aid development. Enabled by default.
- `Ymir_ENABLE_IMGUI_DEMO` (`BOOL`): Enables the ImGui demo window, useful as a reference when developing new UI elements. Enabled by default.
- `Ymir_EXTRA_INLINING` (`BOOL`): Enables more aggressive inlining, which slows down the build in exchange for better runtime performance. Disabled by default.

For a Release build, you might want to disable the devlog and ImGui demo window and enable extra inlining to maximize performance and reduce the binary size.

It is highly recommended to use [Ninja](https://ninja-build.org/) as it greatly accelerates the build process, especially on machines with high CPU core counts.


## Building on Windows

To build Ymir on Windows, you will need [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/community/) and [CMake 3.28+](https://cmake.org/).
Clang is highly recommended over MSVC as it produces much higher quality code, outperforming MSVC by 50-80%. However, MSVC tends to provide a better debugging experience.

All dependencies are included through `vcpkg` and in the `vendor` directory, and are built together with the emulator. No external dependencies are needed.

You can choose to generate a .sln file with CMake or open the directory directly with Visual Studio.
Both methods work, but opening the directory allows Visual Studio to use Ninja for significantly faster build times.
If you choose to generate the .sln file, you will need to specify the vcpkg toolchain:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Ymir uses custom Windows triplets to ensure all libraries are statically linked when possible. You can find the overridden triplets in the `vcpkg-triplets` folder.


## Building on Linux

To build Ymir on Linux, first you will need to install SDL3's required dependencies. Follow the instructions on [this page](https://wiki.libsdl.org/SDL3/README-linux) to install them.

The compiler of choice for this platform is Clang. GCC is also supported, but produces slightly slower code.

Use CMake to generate a Makefile or (preferably) a Ninja build script:

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Pass additional `-D<option>=<value>` parameters to tune the build. See the [Build configuration](#build-configuration) section above for details.

You can use CMake to build the project, regardless of generator:

```sh
cmake --build build --parallel
```


## Building on FreeBSD

To build Ymir on FreeBSD, first you will need to install SDL3's required dependencies:

```sh
pkg install evdev-proto libX11 libXcursor libXext libXfixes libXi libXrandr libXrender \
    libXScrnSaver libglvnd libinotify pkgconf vulkan-loader
```

The compiler of choice for this platform is Clang. Although a Clang compiler toolchain
is provided with a base install of FreeBSD, it lacks the required `clang-scan-deps`
binary. It is required to install a complete LLVM toolchain (arm64 builds require
at least version 21 due to a compiler bug in lower versions):

```sh
pkg install llvm19
```

Finally, install CMake. Compiling with Ninja is generally recommended:

```sh
pkg install cmake ninja
```

Use CMake to generate a Makefile or (preferably) a Ninja build script. It is necessary
to tell CMake to use the correct compiler from the previous LLVM installation. It
is also recommended to add the paths `/usr/local/include` and `/usr/local/lib` to
the build environment:

```sh
cmake -S . -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++19 \
    -DCMAKE_CXX_FLAGS=-I/usr/local/include \
    -DCMAKE_C_COMPILER=clang19 \
    -DCMAKE_C_FLAGS=-I/usr/local/include \
    -DCMAKE_EXE_LINKER_FLAGS=-L/usr/local/lib \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

Pass additional `-D<option>=<value>` parameters to tune the build. See the [Build configuration](#build-configuration) section above for details.

You can use CMake to build the project, regardless of generator:

```sh
cmake --build build --parallel
```

If you have ALSA libraries installed during compilation (`alsa-libs` package is
installed), the vendored dependency RtMidi will be build with ALSA support. Ymir
will fail to start because RtMidi tries to access `/dev/snd/seq`, which isn't available
on FreeBSD out-of-the box. There are three ways to circumvent this problem:

- deinstall `alsa-libs` prior to compiling
- include the `-DRTMIDI_API_ALSA=off` parameter during CMake generation
- install `alsa-seq-server` on the system and start its service

Ymir uses SDL3's Dialog API. This requires an installed dialog driver in order for
the file dialogs to work in Ymir. Install Zenity:

```sh
pkg install zenity
```


## Building on macOS

Use CMake to generate a Makefile or (preferably) a Ninja build script:

```sh
cmake -S . -B build -G Ninja
```

Pass additional `-D<option>=<value>` parameters to tune the build. See the [Build configuration](#build-configuration) section above for details.

You can use CMake to build the project, regardless of generator:

```sh
cmake --build build --parallel -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```


After building, you will find the .app bundle at:
```sh
build/apps/ymir-sdl3/ymir-sdl3.app
```

