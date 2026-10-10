# Command-line toolchain

A headless command-line build. Visual Studio and Visual Studio Code are not dependencies, and no build step may need an IDE or a preconfigured developer prompt.

## Locked versions

[../tools.lock.json](../tools.lock.json) is authoritative: the build refuses a different required version. Baseline of 24 August 2026:

| Component | Required version |
| --- | --- |
| Visual Studio Build Tools 2026 | 18.9.1 (build 12112.369) |
| MSVC x64-host/x86-target | toolset 14.51.36231; compiler 19.51.36256.0 |
| Windows 11 SDK | release 10.0.28000.2705; toolchain directory 10.0.28000.0 |
| PowerShell | 7.6.6 |
| Crinkler | 3.0b |
| Shader Minifier | 1.6.0 |

Install the Build Tools C++ workload with its x64/x86 compiler component and the listed Windows SDK. Extract the Crinkler 3.0b archive under `.tools/crinkler/3.0b/` and Shader Minifier under `.tools/shader-minifier/1.6.0/`. `.tools/` is ignored by Git. Run `./build.ps1 -VerifyTools` before building.

Crinkler 3.0b output needs SSE4.2 and Windows 10 or later, which is fine for the compo machines.

Upgrading a tool is an experiment, never automatic: build Debug and Compo first, compare size and hash with the new tool, then update `tools.lock.json` in its own commit.

## Tools

| Component | Responsibility | Required |
| --- | --- | --- |
| Microsoft C++ Build Tools (not the IDE) | `cl.exe` targeting Win32/x86; an x64-hosted compiler produces x86 objects | yes |
| Windows SDK | headers and import libraries | yes |
| PowerShell | build driver and tool discovery | yes |
| [Shader Minifier](https://github.com/laurentlb/shader-minifier) | minifies the Compo shaders | yes |
| [Crinkler](https://github.com/runestubbe/Crinkler) | links, compresses and reports the size | yes |
| ffmpeg | encodes captured frames into video | only for recording |
| NASM, Python, Sointu | not used: no hand-written assembly, no generators, audio is hand-written DSP | no |

References: [Build C/C++ on the command line](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line), [Crinkler manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt), [in4k Crinkler notes](https://in4k.github.io/wiki/crinkler).

## Build interface

```powershell
.\build.ps1 -VerifyTools
.\build.ps1 -Configuration Debug
.\build.ps1 -Configuration Compo
.\build.ps1 -Configuration Debug -MinifyShaders
.\build.ps1 -Configuration Debug -Optimize
.\build.ps1 -Release
```

- `-MinifyShaders` runs the Compo shader minification in a Debug build, so shader changes can be compared frame by frame with the capture options.
- `-Optimize` compiles Debug with the Compo flags instead of `/Od`, so `ocean.wav` is exactly what Compo plays. `cmp` it against one built from another commit to prove a size change did not touch the sound.
- `-Release` builds all three Compo executables, copies them to `release/`, and zips everything in `release/` (executables and `plankton.nfo`) into `release/plankton.zip` for upload.

The script locates and initialises the MSVC environment, runs the shader minifier, compiles with `cl.exe`, links with Crinkler, and writes the executable, Crinkler report, toolchain report and size report to ignored directories under `out/`. Compiler and linker commands stay visible in the output. Humans and coding agents use the same commands.

## Pinned size-critical options

| Stage | Options | Notes |
| --- | --- | --- |
| `cl.exe`, Compo | `/O1 /Os /Oy- /Ob0 /Zl /GS- /TC /DWIN32 /DCOMPO` | Size optimised, no CRT, no stack cookies. `/Oy-` and `/Ob0` save 75 bytes together. |
| Shader Minifier | `--format text --preserve-externals --field-names stpq` | Uniform and interface-block names are looked up by name from C and must survive. |
| Crinkler | `/COMPMODE:VERYSLOW /SATURATE /TRUNCATEFLOATS:16 /TINYIMPORT /ORDERTRIES:10000` | `/TINYIMPORT` loads DLLs and functions by hash. A Compo link takes about 30 seconds. |

The reasons, and the options tried and rejected, are in [learnings.md](learnings.md). `/TINYIMPORT` trades loader robustness for size, and the header decides how long the screen stays black, so validate any Crinkler change by running the Compo executable and timing how long the window takes to appear.

## Why not CMake?

The project targets one platform and has an unusual final link. A small PowerShell driver gives a reproducible build without generated IDE projects and keeps the exact Crinkler invocation visible. Reconsider it if more platforms or tools make dependencies hard to manage.

## Alternative compilers

`clang-cl` is not the baseline: MSVC already links the finished intro inside the limit, and switching would mean re-verifying every size-critical option for no known gain. MinGW/GCC risks avoidable ABI, runtime and import-library differences.

## Generated files

Nothing generated is tracked: `out/` (executables, reports, minified shader headers), `.tools/`, and Debug output (`ocean.wav`, `frames/`, `shot.bmp`) are rebuilt from source and ignored by Git.
