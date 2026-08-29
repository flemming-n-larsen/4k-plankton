# Command-line toolchain

This project uses a headless command-line build. Visual Studio and Visual Studio Code are not dependencies, and no build step should require an IDE.

## Locked August 2026 baseline

The project starts from the newest stable versions available on 24 August 2026. Exact machine-readable requirements are in [../tools.lock.json](../tools.lock.json); the build refuses to use a different required version.

| Component | Required version |
| --- | --- |
| Visual Studio Build Tools 2026 | 18.9.1 (build 12112.369) |
| MSVC x64-host/x86-target | toolset 14.51.36231; compiler 19.51.36256.0 |
| Windows 11 SDK | release 10.0.28000.2705; toolchain directory 10.0.28000.0 |
| PowerShell | 7.6.5 |
| Crinkler | 3.0b |
| Shader Minifier | 1.6.0 |

Install the Build Tools C++ workload and its x64/x86 compiler component, plus the listed Windows SDK. Extract the Crinkler 3.0b release archive under `.tools/crinkler/3.0b/`; `.tools` is deliberately ignored by Git. Run `./build.ps1 -VerifyTools` before building.

Crinkler 3.0b produces executables that require SSE4.2 and runs on Windows 10 or later. This is acceptable for the project's modern Windows competition target.

When upgrading a tool, build and run the current Debug and Compo artifacts first, compare their byte size and hash, then update `tools.lock.json` in a dedicated commit. Tool upgrades are experiments, never automatic changes.

## Baseline stack

| Component | Responsibility | By | Required |
| --- | --- | --- | --- |
| Standalone Microsoft C++ Build Tools | Provide the MSVC compiler (`cl.exe`) targeting Win32/x86 | Microsoft | Yes |
| Windows SDK | Provide Windows headers and import libraries | Microsoft | Yes |
| PowerShell | Drive reproducible build variants and tool discovery | Microsoft | Yes |
| Shader Minifier | Minify release shaders | Laurent Le Brun (LLB / Ctrl-Alt-Test) and contributors, Apache-2.0 | Yes |
| Crinkler | Link object files, compress the executable, and report final size | Rune L. H. Stubbe (Mentor/TBC) and Aske Simon Christensen (Blueberry/Loonies), zlib | Yes |
| ffmpeg | Encode captured frames into video | the FFmpeg project | Only for recording, not for the intro |
| NASM | Assemble generated or hand-written assembly | | No, nothing here is hand-written assembly |
| Audio tools such as Sointu | Compose or generate procedural music code and data | | No, the audio is hand-written DSP |
| Python | Run a generator that specifically requires Python | | No |

Despite its name, the standalone *Visual Studio Build Tools* package is not the Visual Studio IDE. Only its C++ build tools workload and a compatible Windows SDK are needed. MSVC supports direct command-line compilation, including using an x64-hosted compiler to produce x86 object files.

References:

- [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
- [Microsoft: Build C/C++ code on the command line](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line)
- [Microsoft: Configure MSVC command-line tools for x86 and x64](https://learn.microsoft.com/en-us/cpp/build/how-to-enable-a-64-bit-visual-cpp-toolset-on-the-command-line)
- [Crinkler manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt)

## Build interface

The intended public interface is a repository-owned PowerShell script:

```powershell
.\build.ps1 -VerifyTools
.\build.ps1 -Configuration Debug
.\build.ps1 -Configuration Compo
.\build.ps1 -Configuration Debug -MinifyShaders
```

`-MinifyShaders` runs the Compo shader-minification pass for a Debug build, so a
change to the minifier or to the shaders can be compared frame-by-frame with the
capture options in `src/intro.c` rather than only by eye on the Compo build.

The script is responsible for:

1. locating and initializing the standalone MSVC command-line environment;
2. running selected shader, audio, or data generators;
3. compiling C or C++ sources directly with `cl.exe` for Win32/x86;
4. invoking Crinkler directly with the object files and required Windows libraries;
5. writing the executable, Crinkler report, toolchain report, and Compo size report to predictable ignored output directories.

Compiler and linker commands should remain visible in build output so size regressions and configuration differences are easy to diagnose. The build must not rely on a developer command prompt already being configured.

## Pinned size-critical options

| Stage | Options | Why |
| --- | --- | --- |
| `cl.exe`, Compo | `/O1 /Os /Zl /GS- /TC /DWIN32 /DCOMPO` | Optimize for size, no CRT, no stack cookies. `/arch:IA32` is *not* usable: x87 float-to-int conversion emits a call to a CRT helper that `/NODEFAULTLIB` cannot resolve. `/Gr` and `/Gv` build but come out slightly larger. |
| Shader Minifier | `--format text --preserve-externals` | Uniform and interface-block names are looked up by name from C, so they must survive. `--preprocess` is a no-op here - it does not substitute object-like macros - and `--aggressive-inlining` makes no difference to the final size. |
| Crinkler | `/COMPMODE:VERYSLOW /TINYHEADER /TINYIMPORT /ORDERTRIES:10000` | `/TINYHEADER` and `/TINYIMPORT` together are worth about 100 bytes. `/UNSAFEIMPORT` adds nothing on top of `/TINYIMPORT`. `/ORDERTRIES` plateaus at 10000 - higher values cost build time without shrinking further - and `/HASHTRIES` does not help. |

`/TINYHEADER` and `/TINYIMPORT` trade loader robustness for size, so any change
to them must be validated by running the Compo executable, rather than by building
it. `/ORDERTRIES:10000` makes a Compo link take roughly 20 seconds.

## Why not CMake?

CMake is useful for portable projects and larger dependency graphs, but it is not necessary to invoke a compiler. This project currently targets one platform and has an unusual final link step. A small PowerShell driver gives the project a reproducible build without generating Visual Studio projects or hiding the exact Crinkler invocation.

This decision can be revisited if additional platforms, tools, or source targets make dependency management cumbersome.

## Alternative compiler

LLVM's `clang-cl` may be evaluated later because it can emit Windows COFF object files and offers an MSVC-compatible command-line interface. It is not the baseline: MSVC has compiled and linked the finished intro with Crinkler inside the size limit, so switching would mean re-verifying every size-critical option below for no known gain. MinGW/GCC is likewise not used, because it may introduce avoidable ABI, runtime, and import-library differences.

## Generated files

Nothing generated is tracked. The minified shader headers, the executables, the Crinkler report and the size and toolchain reports are written under `out/`; `.tools/` holds local third-party tools; the Debug build writes `ocean.wav` and, when recording, `frames/`. All are rebuilt from source and ignored by Git.

No audio-specific tools are needed: the audio is hand-written DSP compiled from `src/audio.c`, with no synthesizer, tracker, or conversion step in the build.
