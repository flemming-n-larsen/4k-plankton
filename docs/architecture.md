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
- [Revision 2026 PC competition rules](https://2026.revision-party.net/competitions/pc/)

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

The first runnable milestone is intentionally small: a CRT-free Win32/x86 executable that creates an OpenGL 2.1 context and ray-marches a lit sphere through a fullscreen coverage triangle. GLSL source is kept readable under `src/shaders/` for Debug and minified into generated C strings for Compo. Debug builds report shader compile/link failures through the debugger, while the Compo path omits those diagnostics. It has no audio, external graphics framework, or runtime dependency beyond the Windows system libraries and OpenGL driver.

**Debug** runs the rectangle in a normal window. **Compo** runs it in a borderless primary-monitor window, links it with Crinkler, and fails the build if the final executable exceeds 4096 bytes. The planned **Safe** variant begins after this path is validated.

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
- Which graphics API and minimal framework to use.
- Whether audio playback should use DirectSound or another Windows API.
- Which Crinkler version and options should define the compo build.
- Which party's rules and compo machine should be the compatibility baseline.
- Which generated files belong in source control and which should be rebuilt.
