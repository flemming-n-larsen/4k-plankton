# Plankton, a PC in4k

This project is about creating the setup required to make a PC in4k (4,096-byte intro) for competing in a 4k intro scene competition. The intro itself is titled **Plankton**.

The goal is to provide a practical starting point for developing, building, and packaging a very small Windows intro, where the final executable must fit within the competition's 4 KiB limit.

## Architecture

Plankton is a native Win32/x86 intro built through a size-optimized toolchain, with no external framework. It targets the TRSAC 4 KB compo; the party's rules and compo machine are authoritative and are checked before the release build.

The project uses a headless command-line toolchain. It does not require Visual Studio or Visual Studio Code. Compilation uses the standalone Microsoft C++ Build Tools and Windows SDK, driven directly by PowerShell scripts, with Crinkler performing the final linking and executable compression. CMake is not used.

Plankton is a CRT-free Win32/x86 executable running a 3D [Particle Life](https://particle-life.com/) simulation. 16,384 particles of 6 types are stepped by an OpenGL compute shader and drawn as additive point sprites, one plankton colour per type: teal, cyan, sea green, deep blue, red-tide red, and diatom gold. The attraction matrix and the starting positions are generated in the shader, so no data is embedded. The camera swims inside the swarm instead of watching it from outside, so creatures fill the frame to every corner with no visible edge to the cluster. Each species also flashes and goes dark in turn on a five-second cycle, so the light travels through the swarm as a wave.

A fullscreen background pass gives an underwater setting: a depth gradient, moving caustics, god rays, and rising bubbles, all procedural, no textures. Underneath it, a synthesized ocean ambience is generated at runtime from noise and filters instead of embedded as a sound file, with a tonal layer over the top. That layer is three sine voices moving through a slow chord progression in D minor, a sub under the reveal, and a single struck tone as the word forms.

The run is an ascent. It opens deep and near-black, with bioluminescent organisms glowing against dark water, and rises toward the surface over the next forty seconds: the light grows, the caustics and god rays strengthen, the plankton recover the colour that depth was absorbing, and bubbles stream past throughout.

At 30 seconds the camera pulls back out of the swarm and the cluster resolves into a full-frame word NUTS, drawn as lines and circular arcs, not a bitmap. Particles glow brighter the faster they move, so the formation arrives as a surge that decays as they settle, with the ambience surging under it. The word holds for eight seconds and is then let go: the swarm is handed back to the pairwise force, which tears the strokes apart and gathers them into cells, so the logo dissolves back into living creatures. The scene fades to black over the same stretch, and the Compo build exits on its own a few seconds into the black instead of sitting there until the operator intervenes. It targets the [TRSAC](https://trsac.dk/realtime-compos/#compo4) 4 KB compo. Its Compo build must be Crinkler-compressed to 4,096 bytes or less. Exact versions and integrity checks are defined in [tools.lock.json](tools.lock.json); see [docs/toolchain.md](docs/toolchain.md) for installation and version-update rules.

The audio is hand-written DSP throughout - noise, filters, and a handful of oscillators - with no synthesizer library, no sequencer, and no embedded sample. A full score remains a separate, still-open decision; candidates include [Sointu](https://github.com/vsariola/sointu), [4klang](https://github.com/gopher-atz/4klang), [Oidos](https://github.com/askeksa/Oidos), and shader-based audio. See [docs/architecture.md](docs/architecture.md) for the reasoning behind the synthesis.

The broader architecture, the build variants, and the decisions behind them are documented in [docs/architecture.md](docs/architecture.md). The concrete command-line stack and its responsibilities are documented in [docs/toolchain.md](docs/toolchain.md).

## Build and run

Open PowerShell 7 in the repository root. The build script initializes the MSVC environment itself, so a Visual Studio developer prompt is not required.

Verify the installed tool versions first:

```powershell
.\build.ps1 -VerifyTools
```

Build and run the windowed Debug version:

```powershell
.\build.ps1 -Configuration Debug
.\out\Debug\in4k-particles.exe
```

Or build and launch it directly:

```powershell
.\build.ps1 -Configuration Debug -Run
```

Build and run the Crinkler-compressed Compo version:

```powershell
.\build.ps1 -Configuration Compo
.\out\Compo\in4k-particles.exe
```

The Compo build opens a borderless primary-monitor window and fails if the executable exceeds 4,096 bytes. Use `-Run` to launch it immediately. Build outputs, including the executable, Crinkler report, size report, and toolchain report, are written under `out\` and ignored by Git.

Press Escape to exit either build. The intro uses the predefined `"static"` window class and runs no message pump in the Compo build, so the window's close button does nothing - Escape, or letting the run finish, is how it ends.

## Inspecting a Debug run

The Debug build writes shader and OpenGL diagnostics to `in4k.log` as well as to the debugger, and accepts options that capture a frame to `shot.bmp` directly from the back buffer:

```powershell
.\out\Debug\in4k-particles.exe -shot900                       # fixed 60 Hz step, capture at frame 900
.\out\Debug\in4k-particles.exe -live20                        # real timing, capture after 20 seconds
.\out\Debug\in4k-particles.exe -shot900 -width1280 -height720 # exact client size
.\out\Debug\in4k-particles.exe -shot900 -steps20              # 5 simulated minutes in ~15s
```

Add `-MinifyShaders` to the Debug build to compile the same minified shader text the Compo build uses, so a shader change can be compared frame-by-frame rather than only by eye on the Compo build:

```powershell
.\build.ps1 -Configuration Debug -MinifyShaders
```

`-shot` is reproducible, which makes it the right option for comparing a change; `-live` exercises the real wall-clock timing path that `-shot` bypasses. `-width`/`-height` check the two resolutions TRSAC requires without changing display mode. `-steps<k>` runs k simulation steps per rendered frame, which reaches a late state quickly when tuning and produces byte-identical output to running k times as many frames. These options are compiled out of the Compo build.

The Compo build runs borderless fullscreen at the current desktop resolution, and is resolution-independent, so one executable covers both resolutions TRSAC requires.

## Recording a video

`-record<fps>` captures the entire run frame-by-frame straight from the back buffer to `frames/frameNNNNN.bmp`, then exits once the run ends. This is more reliable than screen/window capture, which can record the wrong window against this exclusive fullscreen surface.

```powershell
.\out\Debug\in4k-particles.exe -record30 -width960 -height540
```

Requires [ffmpeg](https://ffmpeg.org/) (`scoop install ffmpeg`). Stitch the frames and the freshly generated `ocean.wav` (written alongside `frames/` by the same Debug run) into one MP4:

```powershell
ffmpeg -framerate 30 -i frames\frame%05d.bmp -i ocean.wav -shortest -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 160k in4k-intro.mp4
```

`ocean.wav` already covers the whole run and is deliberately longer than it, so `-shortest` trims its tail to the video's length. `frames\` and the output video are generated output, not source, and are not tracked in version control; delete `frames\` once the video is encoded.

To try a different attractor, change the seed constant in `hash()` in `src/shaders/particles.comp.glsl`. Nothing is seeded from the clock, so a given seed always produces the same run.

## Credits

Plankton is by NUTS. These are the tools and ideas it is built on.

[Crinkler](https://github.com/runestubbe/Crinkler) is what makes the 4,096-byte
limit reachable at all. It is a compressing linker by Rune L. H. Stubbe
(Mentor/TBC) and Aske Simon Christensen (Blueberry/Loonies), distributed under
the zlib license.

[Shader Minifier](https://github.com/laurentlb/shader-minifier) shrinks the GLSL
for the Compo build while the readable source stays in the repository. It is by
Laurent Le Brun (LLB / Ctrl-Alt-Test) and contributors, under Apache-2.0.

The [Particle Life](https://particle-life.com/) idea is
[Jeffrey Ventrella's](https://www.ventrella.com/Clusters/), who built it in 2011
as Clusters: particles of several types, each pair with its own signed
attraction, and cells emerging from nothing more than that. The name Particle
Life and the simulator this project links to are
[Tom Mohr's](https://github.com/tom-mohr/particle-life-app). The simulation here
is written from scratch in a compute shader against the published idea, with no
code taken from either.

The compiler, the Windows SDK and PowerShell are Microsoft's. Nothing else is
linked into the executable: no framework, no synthesizer, no library beyond the
Windows system DLLs and the OpenGL driver.

## Introduction to the in4k scene

The [in4k knowledge base](https://in4k.github.io/) is a good introduction to the domain. It covers the techniques and tools commonly used for 4k intros, including algorithms, graphics, sound, and executable packers.

## Relevant tools and references

- [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/): the standalone MSVC compiler toolchain; the Visual Studio IDE is not required.
- [MSVC command-line documentation](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line): compiler environment and direct command-line build guidance.
- [Crinkler](https://github.com/runestubbe/Crinkler): a compressing linker for small Windows demoscene executables. It is used to link the intro and compact the resulting executable.
- [Crinkler manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt): usage instructions, linker options, and compression details.
- [Shader Minifier](https://github.com/laurentlb/shader-minifier): minifies GLSL shader source for the Compo build while keeping readable shader source for development.
- [in4k Crinkler notes](https://in4k.github.io/wiki/crinkler): background on the compression and linking techniques used by Crinkler.
- [Sointu](https://github.com/vsariola/sointu): a modern, cross-platform 4klang-derived synthesizer candidate that can compile x86 music code to assembly.
- [Oidos](https://github.com/askeksa/Oidos): an additive synthesizer for very small executables, with a Renoise-based workflow.
- [Lev4k shader audio](https://github.com/TheNuSan/Lev4k): an example 4k framework supporting shader-based audio as well as 4klang and Oidos.
