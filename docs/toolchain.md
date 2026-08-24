# Command-line toolchain

This project uses a headless command-line build. Visual Studio and Visual Studio Code are not dependencies, and no build step should require an IDE.

## Baseline stack

| Component | Responsibility | Required initially |
| --- | --- | --- |
| Standalone Microsoft C++ Build Tools | Provide the MSVC compiler (`cl.exe`) targeting Win32/x86 | Yes |
| Windows SDK | Provide Windows headers and import libraries | Yes |
| PowerShell | Drive reproducible build variants and tool discovery | Yes |
| Shader Minifier | Minify release shaders | Yes once shaders are present |
| Crinkler | Link object files, compress the executable, and report final size | Yes |
| NASM | Assemble generated or hand-written assembly | Only if the selected implementation needs it |
| Audio tools such as Sointu | Compose or generate procedural music code and data | Only after an audio backend is selected |
| Python | Run a generator that specifically requires Python | Only if needed |

Despite its name, the standalone *Visual Studio Build Tools* package is not the Visual Studio IDE. Only its C++ build tools workload and a compatible Windows SDK are needed. MSVC supports direct command-line compilation, including using an x64-hosted compiler to produce x86 object files.

References:

- [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
- [Microsoft: Build C/C++ code on the command line](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line)
- [Microsoft: Configure MSVC command-line tools for x86 and x64](https://learn.microsoft.com/en-us/cpp/build/how-to-enable-a-64-bit-visual-cpp-toolset-on-the-command-line)
- [Crinkler manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt)

## Build interface

The intended public interface is a repository-owned PowerShell script:

```powershell
.\build.ps1 -Configuration Debug
.\build.ps1 -Configuration Safe
.\build.ps1 -Configuration Compo
```

The script will be responsible for:

1. locating and initializing the standalone MSVC command-line environment;
2. running selected shader, audio, or data generators;
3. compiling C or C++ sources directly with `cl.exe` for Win32/x86;
4. invoking Crinkler directly with the object files and required Windows libraries;
5. writing the executable and a size report to a predictable output directory.

Compiler and linker commands should remain visible in build output so size regressions and configuration differences are easy to diagnose. The build must not rely on a developer command prompt already being configured.

## Why not CMake initially?

CMake is useful for portable projects and larger dependency graphs, but it is not necessary to invoke a compiler. This project currently targets one platform and has an unusual final link step. A small PowerShell driver gives the project a reproducible build without generating Visual Studio projects or hiding the exact Crinkler invocation.

This decision can be revisited if additional platforms, tools, or source targets make dependency management cumbersome.

## Alternative compiler

LLVM's `clang-cl` may be evaluated later because it can emit Windows COFF object files and offers an MSVC-compatible command-line interface. It is not the baseline until a complete intro has been compiled and linked successfully with Crinkler and tested on the competition target. MinGW/GCC is likewise outside the initial baseline because it may introduce avoidable ABI, runtime, and import-library differences.

## Pending implementation details

- pin the MSVC Build Tools, Windows SDK, Shader Minifier, and Crinkler versions;
- define automatic tool discovery and useful error messages;
- establish compiler flags for Debug, Safe, and Compo;
- establish Crinkler options and compression profiles;
- decide whether generated source and binary data belong in source control;
- add any audio-specific tools only after choosing an audio backend.
