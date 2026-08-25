# Architecture

This document describes the current direction for the PC in4k setup. It records constraints, boundaries, and decisions that should remain useful while the concrete framework and toolchain are still being evaluated.

## Goal

Create a native PC 4 KiB intro that can be developed, tested, built, and packaged for a 4k intro scene competition.

The final compo executable must fit within the competition's 4096-byte limit. The rules of the selected party and the actual compo machine are authoritative and must be checked before finalizing the release configuration.

## Current target assumption

The default implementation target is a **Win32/x86 executable**.

This is a practical starting point because Win32 is a common platform for 4 KiB intros and Crinkler is specifically designed for small 32-bit Windows demoscene executables. It is an implementation assumption, not a claim that every competition requires 32-bit Windows.

References:

- [in4k: Win32 as a 4 KiB platform](https://in4k.github.io/wiki/win32)
- [Crinkler](https://github.com/runestubbe/Crinkler)
- [TRSAC realtime compo rules](https://trsac.dk/realtime-compos/#compo4)

## Compatibility baseline: TRSAC, Compo PC 1

The target competition is the TRSAC 4 KB intro compo, and Compo PC 1 is the
compatibility baseline:

| | |
|---|---|
| Size limit | 4 KB, one `.exe` file |
| Compo PC 1 | Ryzen 9950X3D, GeForce RTX 5070 Ti, 32 GB, Windows with latest NVIDIA drivers |
| Compo PC 2 | i7-11700, GeForce RTX 3070, 32 GB, Windows with latest NVIDIA drivers |
| Resolution | Must support both 1920x1080 and 1280x720 |
| Runtime | 5 minutes maximum |
| Submission | Entry must include an information file |

Two rules constrain the code rather than the packaging. The intro must render
correctly at both required resolutions, so every screen-space quantity is
derived from the viewport rather than from pixel constants. And because both
compo machines must show the same thing, the simulation is advanced by measured
wall-clock time rather than per frame, so it evolves at the same rate
regardless of frame rate.

The intro ships as **one executable that runs borderless fullscreen at the
current desktop resolution**, rather than as separate per-resolution builds. It
is resolution-independent, so the organizer sets the machine and the intro
follows; 1920x1080 and 1280x720 have been confirmed to render identical
framing and structure. This keeps the entry to the single exe file the rules
describe, costs no bytes, and avoids the risk of a forced mode switch failing
on the compo machine.

## Graphics API decision

The intro uses **OpenGL 4.3+ compute shaders with shader storage buffers**,
reached through a plain `wglCreateContext`.

On Windows that call already returns the driver's maximum compatibility
profile - 4.6 on the compo machines - so `glDispatchCompute`, SSBOs, and the
rest resolve through `wglGetProcAddress` with no `wglCreateContextAttribsARB`
path and no extra bytes. Both compo machines are recent NVIDIA cards under
current drivers, so there is no compatibility argument for the older
alternative of ping-ponging state through float textures, which would cost FBO
setup, format juggling, and an awkward data layout while running slower.

The compatibility profile also allows drawing `GL_POINTS` with no vertex array
object and no vertex buffer, reading positions straight from the SSBO by
`gl_VertexID`. That is the single largest size saving in the renderer.

One consequence is easy to miss: in a compatibility profile `gl_PointCoord` is
undefined unless `GL_POINT_SPRITE` is enabled. Without it the value reads as
zero, every fragment fails the sprite falloff test, and the screen stays black
with no GL error reported.

WebGPU and WebGL were considered and do not apply - both are browser APIs, and
this is a native Win32 executable. For a browser entry WebGPU would be the
right choice for the same reason compute shaders are here.

## Toolchain decision

The baseline is a fully headless Windows command-line toolchain. Neither Visual Studio nor Visual Studio Code is required.

The initial stack is:

- **Standalone Microsoft C++ Build Tools and Windows SDK** for `cl.exe`, Windows headers, and import libraries, targeting Win32/x86;
- **PowerShell build scripts** as the build driver and the documented interface used by developers and coding agents;
- **Shader Minifier** for release shader processing;
- **Crinkler** as the final linker and executable compressor.

CMake is not part of the initial baseline. The project is small, Windows-specific, and has a specialized generation and linking pipeline, so direct scripts keep every command and size-related option visible. CMake may be reconsidered if the project grows enough to benefit from it.

NASM, Python, Sointu, and other generators are optional rather than baseline dependencies. They should be added only when a selected implementation requires them. In particular, choosing an audio backend may introduce its own editor, compiler, assembler, or code-generation step.

The Microsoft package retains the *Visual Studio Build Tools* product name, but it installs the compiler and SDK without requiring the Visual Studio IDE. Build scripts should initialize its command-line environment and invoke the tools directly. See [toolchain.md](toolchain.md) for the stack responsibilities and intended command interface.

## Architectural boundaries

The intro should be organized around replaceable subsystems rather than a single framework-specific implementation:

```text
intro logic
    ├── platform layer       window, input, timing, process lifecycle
    ├── graphics layer       rendering API and shader execution
    ├── audio abstraction    music generation, playback, and sync timing
    └── generated data       shaders, music data, tables, and other assets

build and packaging         compilation, assembly, linking, minification, packing
```

The abstractions are primarily development boundaries. The final compo build may flatten or specialize them when that produces a smaller executable.

## Audio direction

The project should support runtime-generated audio instead of embedding a WAV, MP3, or other complete music file in the final executable. This keeps the music within the same procedural, size-constrained model as the visuals.

The audio boundary should allow the rest of the intro to remain independent of the eventual synthesis and playback implementation. It should provide room for:

- starting and stopping the music;
- obtaining a stable music position for synchronizing visuals;
- development playback and inspection;
- replacing the final backend without redesigning the intro loop.

No audio solution is selected yet. Current candidates are:

- [Sointu](https://github.com/vsariola/sointu), a 4klang-derived synthesizer with x86 output and a song-to-assembly compiler;
- [4klang](https://github.com/gopher-atz/4klang), the established VSTi and assembly-based workflow;
- [Oidos](https://github.com/askeksa/Oidos), an additive synthesizer with a Renoise-based conversion workflow;
- shader-based audio, where music is generated procedurally alongside the visual shader.

The eventual decision should be based on the compressed size of the complete intro, sound quality, CPU/GPU cost, synchronization quality, development workflow, and toolchain reproducibility.

## Framework-selection criteria

The intro/framework should be evaluated against these criteria:

- final Crinkler-compressed size, including platform setup, graphics, audio, and data;
- compatibility with the intended Windows systems and competition machine;
- fast development and debugging workflow;
- ability to produce a larger safe build and a minimal compo build;
- clear integration points for generated shader and music data;
- reproducible builds with pinned tool versions;
- no runtime dependencies that the competition machine will not provide.

The framework is not selected yet. The project should preserve the ability to compare a minimal native framework, an existing 4k framework, and a more specialized audio or shader approach before committing.

## Build variants

The project should plan for separate build variants from the beginning:

- **Debug** — development diagnostics and convenient iteration.
- **Safe** — compatibility-oriented executable for testing and distribution outside the compo environment.
- **Compo** — aggressively size-optimized executable intended to meet the 4096-byte limit.

The compo build must not be treated as the only artifact. A safe build is useful for diagnosing compatibility problems introduced by aggressive size optimizations or executable packing.

## Bootstrap milestone

The bootstrap milestone is a CRT-free Win32/x86 executable running a 3D
[Particle Life](https://particle-life.com/) simulation on the GPU. GLSL source
is kept readable under `src/shaders/` for Debug and minified into generated C
strings for Compo. It has no audio, external graphics framework, or runtime
dependency beyond the Windows system libraries and OpenGL driver.

**Debug** runs in a normal window and reports shader compile and link failures.
**Compo** runs in a borderless primary-monitor window, links with Crinkler, and
fails the build if the executable exceeds 4096 bytes. The planned **Safe**
variant begins after this path is validated.

### Simulation and rendering

16384 particles of 6 types are held in a single shader storage buffer of
`{ vec4 position, vec4 velocity }`, where `position.w` carries the type index.
Every frame a compute shader applies the standard Particle Life force curve over
all pairs, and points are drawn straight from that buffer.

Three decisions are worth recording:

- **No data is embedded.** The attraction matrix and the initial positions are
  generated from a hash inside the shader, costing zero bytes. `SEED` in
  `particles.comp.glsl` selects a different but fully reproducible attractor;
  nothing in the intro is seeded from the clock, so every run is identical.
- **A type always attracts itself.** With a purely random matrix, 3D Particle
  Life settles into a uniform gas: there is too much room for cohesion to win.
  Biasing the diagonal positive is what makes distinct cells form.
- **The world does not wrap.** Particles are held by a soft central containment
  force applied once per particle, outside the pair loop. The classic periodic
  box would instead put a minimum-image test inside the O(n^2) loop, costing
  both bytes and time in the hottest code, and it pops at the box faces.

### Background and palette

A third program (`background.vert.glsl` / `background.frag.glsl`) draws an
oversized fullscreen triangle before the points, giving an underwater setting:
a vertical gradient from deep navy to lit teal, a moving caustic ridge pattern,
and god rays converging on a sun point above the top edge, all built from a
handful of `sin`/`atan` terms in normalized `[0,1]` screen space rather than
any texture or 3D geometry - this costs about 280 bytes for the whole effect.
Blending is disabled while it draws, since it covers every pixel of the
viewport and would otherwise accumulate additively over whatever was left in
the back buffer from a previous frame; blending is re-enabled for the points
drawn on top. This also removes the need for `glClear` - the background
already overwrites every pixel.

The particle palette is a cosine gradient (`a + b*cos(2*pi*(c*x+d))`) chosen to
stay in the teal/cyan/blue/violet range, with the red channel's amplitude kept
small so no phase of the 6 types swings into a warm hue - a first attempt with
a larger red amplitude produced one type that read as brown, found by
computing all 6 discrete outputs by hand rather than only eyeballing renders.
Particles further from the camera are also pulled toward a dark blue-black,
mimicking how real water absorbs colour with depth.

### Debug-only tooling

The Debug build carries diagnostics that the Compo path compiles out entirely.
Shader logs and the GL version go to both the debugger and `in4k.log`. Three
command-line options make the output verifiable without a debugger or a screen
grab, by reading the back buffer in-process and writing `shot.bmp`:

- `-shot<frames>` runs a fixed 60 Hz timestep and captures at that exact frame,
  which makes a frame reproducible;
- `-live<seconds>` uses the real wall-clock timing path and captures after that
  many seconds;
- `-width<w> -height<h>` sizes the client area exactly, so the two required
  compo resolutions can be compared without changing display mode;
- `-steps<k>` runs k simulation steps per rendered frame. Rendering never feeds
  back into the simulation, so this reaches a late state in a fraction of the
  wall-clock time while producing byte-identical output - verified by comparing
  `-shot1800 -steps1` against `-shot180 -steps10` and `-shot90 -steps20`. That
  equivalence also demonstrates the simulation is deterministic, confirming the
  split-pass integration is free of read/write races.

Capturing from the screen instead is unreliable: a fullscreen window that has
lost foreground returns a stale frame, which is easily mistaken for a frozen
simulation.

## Planned build pipeline

The intended pipeline is:

```text
source code and generated assets
        ↓
optional shader/audio/data generation
        ↓
MSVC compilation to x86 object files
        ↓
Crinkler compression and linking
        ↓
final executable and size report
```

PowerShell exposes stable Debug and Compo commands for the bootstrap milestone; Safe is added when compatibility behavior exists to validate. Coding agents and human developers should use those same commands; builds must not depend on IDE projects, IDE state, or manually configured editor tasks.

The concrete installation and invocation details will be added to [toolchain.md](toolchain.md) as the build is implemented. Tool versions, discovery rules, expected paths, compiler flags, Crinkler options, and generated-file policy must be pinned there once validated.

## Open decisions

- Which audio backend and composition workflow to use.
- Whether audio playback should use DirectSound or another Windows API.
- Which Crinkler version and options should define the compo build.
- Which generated files belong in source control and which should be rebuilt.
