# PC in4k setup

This project is about creating the setup required to make a PC in4k (4,096-byte intro) for competing in a 4k intro scene competition.

The goal is to provide a practical starting point for developing, building, and packaging a very small Windows intro, where the final executable must fit within the competition's 4 KiB limit.

## Architecture direction

The current implementation direction is a native Win32/x86 intro built through a size-optimized toolchain. The exact framework and competition target will be validated against the rules of the selected party.

The project uses a headless command-line toolchain. It does not require Visual Studio or Visual Studio Code. Compilation uses the standalone Microsoft C++ Build Tools and Windows SDK, driven directly by PowerShell scripts, with Crinkler performing the final linking and executable compression. CMake is not part of the initial baseline.

Audio will be generated at runtime rather than embedded as a music file. The intro will use an audio abstraction so the synthesis and playback implementation can be selected later without coupling the rest of the intro to one audio solution. Candidate approaches include [Sointu](https://github.com/vsariola/sointu), [4klang](https://github.com/gopher-atz/4klang), [Oidos](https://github.com/askeksa/Oidos), and shader-based audio.

The broader architecture, framework-selection criteria, build variants, and open decisions are documented in [docs/architecture.md](docs/architecture.md). The concrete command-line stack and its responsibilities are documented in [docs/toolchain.md](docs/toolchain.md).

## Introduction to the in4k scene

The [in4k knowledge base](https://in4k.github.io/) is a good introduction to the domain. It covers the techniques and tools commonly used for 4k intros, including algorithms, graphics, sound, and executable packers.

## Relevant tools and references

- [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) — the standalone MSVC compiler toolchain; the Visual Studio IDE is not required.
- [MSVC command-line documentation](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line) — compiler environment and direct command-line build guidance.
- [Crinkler](https://github.com/runestubbe/Crinkler) — a compressing linker for small Windows demoscene executables. It is used to link the intro and compact the resulting executable.
- [Crinkler manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt) — usage instructions, linker options, and compression details.
- [in4k Crinkler notes](https://in4k.github.io/wiki/crinkler) — background on the compression and linking techniques used by Crinkler.
- [Sointu](https://github.com/vsariola/sointu) — a modern, cross-platform 4klang-derived synthesizer candidate that can compile x86 music code to assembly.
- [Oidos](https://github.com/askeksa/Oidos) — an additive synthesizer for very small executables, with a Renoise-based workflow.
- [Lev4k shader audio](https://github.com/TheNuSan/Lev4k) — an example 4k framework supporting shader-based audio as well as 4klang and Oidos.
