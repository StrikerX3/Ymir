/**
@file
@brief Main page documentation.
*/

/**
@mainpage Ymir

Ymir is a Sega Saturn emulator written in C++20.



@section usage Usage

`ymir::Saturn` emulates a Sega Saturn system. You can make as many instances of it as you want; they're all completely
independent and free of global state.

Use the methods and members on that instance to control the emulator. The Saturn's components can be accessed directly
through the instance as well.

The constructor automatically hard resets the emulator with @link ymir::Saturn::Reset(bool) `ymir::Saturn::Reset(true)`
@endlink. This is cheaper than constructing the object from scratch. You can also soft reset with @link
ymir::Saturn::Reset(bool) `ymir::Saturn::Reset(false)` @endlink or by changing the Reset button state through the SMPC,
which raises the NMI signal and causes the guest software to enter the reset vector, just like pushing the Reset button
on the real Saturn.

In order to run the emulator, set up a loop that processes application events and invokes `ymir::Saturn::RunFrame()` to
run the emulator for a single frame.

The emulator core makes no attempt to pace execution to realtime speed - it's up to the frontend to implement some rate
control method. If no such method is used, it will run as fast as your CPU allows. For example, you can use a blocking
ring buffer with the audio callback function to implement a simple audio sync mechanism that will pace the frames based
on how fast the audio buffer is drained by the sound driver.

You can configure several parameters of the emulator core through `ymir::Saturn::configuration`.



@subsection loading_contents Loading IPL and CD Block ROMs, discs, backup memory and cartridges

Use `ymir::Saturn::LoadIPL` to copy an IPL ROM image into the emulator. By default, the emulator will use a simple
do-nothing image that puts the master SH-2 into an infinite loop and immediately returns from all exceptions. The IPL
ROM is accessible through the `ymir::Saturn::mem` member, in `ymir::sys::SystemMemory::IPL`.

CD Block ROMs (required for low level emulation) can be loaded directly into the SH-1's internal ROM area with
`ymir::sh1::SH1::LoadROM`. By default it also uses the same do-nothing image used with the SH-2s.

To load discs, you will need to use the media loader library included with the emulator core in media/loader/loader.hpp.
The header is automatically included with ymir.hpp. Use `ymir::media::LoadDisc` to load a disc into an
`ymir::media::Disc` instance, then move it into the `ymir::Saturn` instance with `ymir::Saturn::LoadDisc`. This will
trigger the appropriate interrupts in the CD block, causing the system to switch back to the spaceship menu if a game
was in progress and didn't hijack the CD block interrupt handler.

To load an internal backup memory image, invoke `ymir::Saturn::LoadInternalBackupMemoryImage` method with the path to
the image and an `std::error_core` instance to output errors into. The internal backup memory can be accessed through
`ymir::Saturn::mem` with the `ymir::sys::SystemMemory::GetInternalBackupRAM` method. The internal backup memory is 32
KiB in size and will be automatically created if it doesn't exist. If a file with a different size is provided, it will
be truncated to 32 KiB and formatted without prior warning.

Alternatively, you can manually construct an `ymir::bup::BackupMemory` object and load a backup memory image into it,
then move it into the system memory object with `ymir::sys::SystemMemory::SetInternalBackupRAM`. This gives more
flexibility in how the image is loaded. Note that the internal backup memory image must be 32 KiB in size, otherwise it
will not be loaded.

To load an external backup memory, you must build an `ymir::bup::BackupMemory` object beforehand, then pass it to the
`ymir::cart::BackupMemoryCartridge` constructor by invoking `ymir::Saturn::InsertCartridge` with that cartridge type.
Other cartridges can be loaded in the same manner, by specifying their type in the `InsertCartridge` method's template
parameter `T` and passing the appropriate constructor arguments to the method. The cartridge can be removed with
`ymir::Saturn::RemoveCartridge`.

Cartridges are accessed through the `ymir::scu::SCU` instance present in `ymir::Saturn::SCU`. The same insert/remove
operations are available in this class and work in the same manner.



@subsection input Sending input

To process inputs, you'll need to attach a controller to one or both ports and configure callbacks. You'll find the
ports in the @link ymir::Saturn::SMPC `SMPC` @endlink member of `ymir::Saturn`.

Ports are instances of `ymir::peripheral::PeripheralPort` which provides methods for inserting, removing and retrieving
connected peripherals.

Use one of the `Connect` methods to attempt to attach a controller of a specific type to the port. If successful, the
method will return a valid pointer to the specialized controller instance which you can use to further configure the
peripheral. `nullptr` indicates failure to instantiate the object or to attach the peripheral due to incompatibility
with other connected peripherals (e.g. you cannot use the Virtua Gun with a multi-tap adapter).

@note The emulator currently only supports attaching a single peripheral to each port. Multi-tap and other types of
peripherals are planned.

Use `ymir::peripheral::PeripheralPort::DisconnectPeripherals()` to disconnect all peripherals connected to the port. Be
careful: any existing pointers to previously connected peripherals will become invalid. The same applies when replacing
a peripheral.

Whenever input is queried, either through INTBACK or by direct access to PDR/DDR registers, the peripheral will invoke a
callback function with the following signature:

```cpp
void PeripheralReportCallback(ymir::peripheral::PeripheralReport &report, void *userContext)
```

The type of the peripheral is specified in `ymir::peripheral::PeripheralReport::type` which is an enum of the type
`ymir::peripheral::PeripheralType`. The callback function must fill in the appropriate report depending on the type. The
report is preinitialized with the default values for the controller: all buttons released, all axes at zero, etc. This
callback is invoked from the emulator thread.

Use `ymir::peripheral::PeripheralPort::SetPeripheralReportCallback` to bind the callback.



@subsection video_renderer Configuring video renderers

The `ymir::vdp::VDP` class uses a renderer (a concrete subtype of `ymir::vdp::IVDPRenderer`) to draw frames. Ymir
currently supports these rendering backends:
- **Null** (`ymir::vdp::NullVDPRenderer`): renders nothing, but invokes all standard renderer callbacks.
- **Software** (`ymir::vdp::SoftwareVDPRenderer`): CPU-only renderer. Can use dedicated threads or run directly in the
emulator thread. Requires additional callback configuration to receive the framebuffer data.
- **Direct3D 11** (`ymir::vdp::Direct3D11VDPRenderer`): Direct3D 11 hardware-accelerated renderer. Requires a pointer to
a `ID3D11Device` instance with support for deferred contexts and Shader Model 5.0 pixel and compute shaders.

Renderers can be selected through one of the `Use*Renderer()` methods in `ymir::vdp::VDP`. The methods return a pointer
to the concrete renderer instance, allowing you to perform additional configuration if necessary.

You can get the current renderer instance with `ymir::vdp::VDP::GetRenderer()`, which always returns a valid reference
to a `ymir::vdp::IVDPRenderer`. Use `ymir::vdp::IVDPRenderer::GetType()` to determine its type, or cast it to a concrete
type with `ymir::vdp::IVDPRenderer::As<ymir::vdp::VDPRendererType>()` which returns a valid pointer if the renderer
matches the given type or `nullptr` otherwise.

All renderer callbacks (including renderer-specific callbacks) are preserved when switching renderers.

All renderers (including the null renderer) invoke the `ymir::vdp::config::RendererCallbacks::VDP2ResolutionChanged`
callback whenever the VDP2 resolution is changed. It has the following signature:

```cpp
void VDP2ResolutionChanged(uint32 width, uint32 height, void *userContext)
```

where:
- `width` and `height` specify the dimensions of the framebuffer
- `userContext` is a user-provided context pointer

This callback must be registered by the frontend in order to adjust the screen size.



@subsection sw_renderer Software renderer

- Implementation class: `ymir::vdp::SoftwareVDPRenderer`
- Enum value: `ymir::vdp::VDPRendererType::Software`
- Instantiation: `ymir::vdp::VDP::UseSoftwareRenderer()`

The software renderer is the reference implementation for the Saturn's VDP1 and VDP2 graphics chips. It aims to be
pixel-perfect, easy to use and flexible enough to support basic graphics enhancements.

The software VDP renderer invokes the frame completed callback once a frame finishes rendering immediately after the
VDP2 frame finished callback. The callback signature is:

```cpp
void SoftwareFrame(uint32 *fb, uint32 width, uint32 height, void *userContext)
```

where:
- `fb` is a pointer to the rendered framebuffer in little-endian XBGR8888 format (`..BBGGRR`)
- `width` and `height` specify the dimensions of the framebuffer
- `userContext` is a user-provided context pointer

The dimensions are passed in for convenience. They will always match the latest resolution received by the VDP2
resolution changed callback.

Use `ymir::vdp::VDP::SetSoftwareRenderCallback` to bind this callback, or set it directly in the software renderer
instance.

@note The most significant byte of the framebuffer data is set to 0xFF for convenience, so that it is fully opaque in
case your framebuffer texture has an alpha channel (ABGR8888 format).



@subsection hw_renderer Hardware renderers

All hardware-accelerated renderers are frontend-agnostic and make use of raw graphics APIs available on each platform.
In order to instantiate them, you'll generally need to provide a high-level object representing a graphics device or
context and some additional API-specific configuration. Renderers may require certain features to be available, such as
multithreading or minimum shader model version.

Hardware renderers build command lists that must be executed on the thread where graphics is managed (typically the main
or GUI thread) by invoking `ymir::vdp::HardwareVDPRendererBase::ExecutePendingCommandList()` at an appropriate time in
the thread, which will execute the latest pending command list if one is available. The application may have to flush
graphics state prior to executing the command lists. This can be easily achieved with the pre-execution callback that is
invoked immediately before a command list is executed, if one is available:

```cpp
void PreExecuteCommandList(void *userContext)
```

This callback is invoked in the same thread that invokes `ExecutePendingCommandList()` and can be configured through the
VDP with `ymir::vdp::VDP::SetHardwarePreExecuteCommandListCallback(ymir::vdp::CBHardwarePreExecuteCommandList callback)`
or directly in the hardware renderer instance in the `ymir::vdp::HardwareVDPRendererBase::HwCallbacks` field.

The `ExecutePendingCommandList()` function returns `true` if a command list was processed. With that, you can use the
following idiom if you need to perform additional logic after processing a commmand list:

```cpp
if (vdpRenderer->ExecutePendingCommandList()) {
    // Perform additional logic
}
```

where
- `userContext` is a user-provided context pointer

Whenever a command list is prepared, the hardware renderer invokes another callback function to notify the frontend. The
callback signature is:

```cpp
void CommandListReady(void *userContext)
```

where
- `userContext` is a user-provided context pointer

This callback is invoked by the renderer thread (which may be the emulator thread) and can be configured through the VDP
with `ymir::vdp::VDP::SetHardwareCommandListReadyCallback(ymir::vdp::CBHardwareCommandListReady callback)` or directly
in the hardware renderer instance in the `ymir::vdp::HardwareVDPRendererBase::HwCallbacks` field.

Each renderer exposes a handle or pointer to a texture containing the VDP2 framebuffer output, ready to be rendered to
the screen however the application sees fit.



@subsection d3d11_renderer Direct3D 11 renderer

- Implementation class: `ymir::vdp::Direct3D11VDPRenderer`
- Enum value: `ymir::vdp::VDPRendererType::Direct3D11`
- Instantiation: `ymir::vdp::VDP::UseDirect3D11Renderer(ID3D11Device *device, bool restoreState)`

The Direct3D 11 VDP renderer requires a pointer to an `ID3D11Device` instantiated with support for deferred contexts and
Shader Model 5.0 pixel and compute shaders. The `restoreState` parameter indicates if the deferred context state should
be restored after executing each command list. Set this to `false` if your frontend application manages the state.

`ymir::vdp::Direct3D11VDPRenderer::GetVDP2OutputTexture()` returns an `ID3D11Texture2D *` with the VDP2 framebuffer
output texture.



@subsection recv_video_audio Receiving video frames and audio samples

In order to receive video and audio, you must configure callbacks in `ymir::vdp::VDP` and `ymir::scsp::SCSP`, accessible
through `ymir::Saturn::VDP` and `ymir::Saturn::SCSP`.

Video callbacks are defined in `ymir::vdp::config::RendererCallbacks` which can be accessed directly from the renderer
instance obtained with `ymir::vdp::VDP::GetRenderer()` via the field `ymir::vdp::IVDPRenderer::Callbacks`.

The software renderer defines additional callbacks in `ymir::vdp::SoftwareRendererCallbacks`. These can be configured in
`ymir::vdp::SoftwareVDPRenderer::SwCallbacks` but can also be configured directly in the `ymir::vdp::VDP` instance for
convenience, which avoids the need to type-checking and casting.

All hardware renderers use callbacks from the `ymir::vdp::HardwareRendererCallbacks`. Similar to software renderer
callbacks, these are managed and automatically configured by the VDP when hardware renderers are instantiated.

All renderers invoke the VDP1 and VDP2 frame complete callbacks when their respective frames are finished. The VDP1
frame complete callback is invoked whenever a framebuffer swap occurs, while the VDP2 frame complete callback is invoked
when entering the VBlank phase. Both callbacks have the following signature:

```cpp
void FrameComplete(void *userContext)
```

where
- `userContext` is a user-provided context pointer

Both callbacks must be set directly in the renderer instance. They only need to be set once; they are transferred over
to new renderer instances when switching renderers.

Software and hardware renderers output frames in their own ways. See the corresponding sections above for details on
their framebuffer management mechanisms.

The SCSP invokes the sample callback on every sample (signed 16-bit PCM, stereo, 44100 Hz).
The callback signature is:

```cpp
void SCSPSampleCallback(sint16 left, sint16 right, void *userContext)
```

where:
- `left` and `right` are the samples for the respective channels
- `userContext` is a user-provided context pointer

You probably want to accumulate those samples into a ring buffer before sending them to the audio system.

Use `ymir::scsp::SCSP::SetSampleCallback` to bind this callback.

You can run the emulator core without providing video and audio callbacks (headless mode). It will work fine, but you
won't receive video frames or audio samples. For optimal performance in this scenario, use the null VDP renderer.

All callbacks are invoked from inside the emulator core deep within the `RunFrame()` call stack, so if you're running it
on a dedicated thread you need to make sure to sync/mutex updates coming from the callbacks into the GUI/main thread.
The VDP renderers may invoke their callbacks from different threads which are synchronized with the emulator thread.



@subsection persistent_state Persistent state

The internal backup memory, external backup RAM cartridge and SMPC persist data to disk.

Use `ymir::Saturn::LoadInternalBackupMemoryImage` to configure the path to the internal backup memory image. Make sure
to check the error code in `error` to ensure the image was properly loaded. If the file is 32 KiB in size and contains a
proper internal backup memory image, it is loaded as is, otherwise the file is resized or truncated to 32 KiB and
formatted to a blank backup memory.

By default, the emulator core will *not* load an internal backup memory image, so it will not work out of the box. You
must load or create an image in order to use internal backup memory saves.

For external backup RAM cartridges, you will need to use `ymir::bup::BackupMemory` to try to load the image:

```cpp
std::error_code error{};
ymir::bup::BackupMemory bupMem{};
switch (bupMem.LoadFrom(path, error)) {
case ymir::bup::BackupMemoryImageLoadResult::Success:
    // The image is valid
    break;
case ymir::bup::BackupMemoryImageLoadResult::FilesystemError:
    // A file system error occurred; check `error` for details
    break;
case ymir::bup::BackupMemoryImageLoadResult::InvalidSize:
    // The file does not have a valid backup memory image size
    break;
}
```

If the backup image loaded successfully, you can insert the cartridge:

```cpp
ymir::Saturn &saturn = ...;
saturn.InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
```

The SMPC is initialized with factory defaults. Upon startup, the emulated Saturn will require the user to set up the
language and system clock just like a real Saturn when the system configuration is reset or lost due to a dead battery.
You can also force a factory reset with `ymir::Saturn::FactoryReset`.

As with the internal backup memory, the emulator core will not automatically persist any settings upon exit unless you
bind it to a file with either `ymir::smpc::SMPC::LoadPersistentDataFrom` or `ymir::smpc::SMPC::SavePersistentDataTo`.
As their names imply, `LoadPersistentDataFrom` will attempt to read persistent data from the given path and
`SavePersistentDataTo` will attempt to write the current settings to the file. Both functions will additionally bind the
persistent data path so that any further changes are automatically saved to that file. It is sufficient to call one of
these functions only once to configure the persistent path for SMPC settings for the lifetime of the `ymir::Saturn`
instance.



@subsection debugging Debugging

The debugger framework provides two major components: the *probes* and the *tracers*. You can also use `ymir::sys::Bus`
objects to directly read or write memory.

`ymir::sys::Bus` instances provide `Peek`/`Poke` variants of `Read`/`Write` methods that circumvent memory access
limitations, allowing debuggers to read from write-only registers or do 8-bit reads and writes to VDP registers which
normally disallow accesses of that size. `Peek` and `Poke` also avoid side-effects when accessing certain registers such
as the CD Block's data transfer register which would cause the transfer pointer to advance and break emulated software.

@a Probes are provided by components through `GetProbe()` methods to inspect or modify their internal state. They are
always available and have virtually no performance cost on the emulator thread. Probes can perform operations that
cannot normally be done through simple memory reads and writes such as directly reading from or writing to SH2 cache
arrays or CD Block buffers. Not even `Peek`/`Poke` on `ymir::sys::Bus` can reach that far.

@a Tracers are integrated into the components themselves in order to capture events as the emulator executes. The
application must implement the provided interfaces in @link libs/ymir-core/include/ymir/debug ymir/debug @endlink then
attach tracer instances to the components with their `UseTracer()` methods to receive events as they occur while the
emulator is running.

Some tracers require you to run the emulator in *debug tracing mode*. Call `ymir::Saturn::EnableDebugTracing` on the
`Saturn` instance with `true` then attach the tracers. There's no need to reset or reinitialize the emulator core to
switch modes -- you can run the emulator normally for a while, then switch to debug mode at any point to enable tracing,
and switch back and forth as often as you want. Tracers that need debug tracing mode to work are documented as such in
their header files.

Running in debug tracing mode has a noticeable performance penalty as the alternative code path enables calls to the
tracers in hot paths. This is left as an option in order to maximize performance for the primary use case of playing
games without using any debugging features. For this reason, `EnableDebugTracing(false)` will also detach all tracers.

Some components always have tracing enabled if provided a valid instance, so in order avoid the performance penalty,
make sure to also detach tracers when you don't need them by calling `ymir::Saturn::DetachAllTracers`. Currently, only
the SH2 and SCU DSP tracers honor the debug tracing mode flag.

Debug tracing mode is not necessary to use probes as these have no performace impact on the emulator.

Tracers are invoked from the emulator thread. You will need to manage thread safety if trace data is to be consumed by
another thread. It's also important to minimize performance impact, especially on hot tracers (memory accesses and CPU
instructions primarily). A good approach to optimize time spent handling the event is to copy the trace data into a
lock-free ring buffer to be processed further by another thread.

@warning Since the emulator is not thread-safe, care must be taken when using buses, probes and tracers while the
emulator is running in a multithreaded context:
- Reads will retrieve dirty data but are otherwise safe.
- Certain writes (especially to nontrivial registers or internal state) will cause race conditions and potentially
  crash the emulator if not properly synchronized.

The debugger also provides a debug break signal that can be raised from just about anywhere in the core, and a callback
that can be used by frontends to respond to those signals. Use `ymir::Saturn::SetDebugBreakRaisedCallback` to register
the callback. The callback function is called from the emulator thread.



@subsection thread_safety Thread safety

The emulator core is *not* thread-safe and *will never be*. Make sure to provide your own synchronization mechanisms if
you plan to run it in a dedicated thread.

As noted above, the input, video and audio callbacks as well as debug tracers are invoked from the emulator thread.
Provide proper synchronization between the emulator thread and the main/GUI thread when handling these events.

The software VDP1 and VDP2 renderers may optionally run in their own threads. They are thread-safe within the core.
Hardware renderers may run in the emulator thread or a dedicated thread, depending on the implementation.
*/
