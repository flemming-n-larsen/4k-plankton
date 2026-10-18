# Plankton, a PC in4k

Plankton is a 4,096-byte Windows intro for the [TRSAC](https://trsac.dk/realtime-compos/#compo4) 4 KB compo. A 3D [Particle Life](https://particle-life.com/) simulation of 16,384 particles in 6 colours runs in an OpenGL compute shader and is drawn as additive point sprites over a procedural underwater background, with sound synthesised at runtime. Nothing is embedded: no textures, samples or data tables.

The 65-second run is an ascent from dark water toward the light, a wave of light through the swarm, the word NUTS assembled letter by letter, and a rubber duck, the TRSAC mascot. The word and the duck each dissolve back into living cells.

It placed 4th in the Combined Intro competition at TRSAC 2026. Watch it on [YouTube](https://www.youtube.com/watch?v=RcUpAMERlE8); production details and comments are on [Demozoo](https://demozoo.org/productions/399218/).

It is a CRT-free Win32/x86 executable built with a headless toolchain (MSVC, PowerShell, Shader Minifier, Crinkler). It needs no Visual Studio, Visual Studio Code or CMake.

- Design and decisions: [docs/architecture.md](docs/architecture.md)
- Tools, versions and options: [docs/toolchain.md](docs/toolchain.md), locked in [tools.lock.json](tools.lock.json)
- Size and build lessons: [docs/learnings.md](docs/learnings.md)
- Credits: [CREDITS.md](CREDITS.md)
- A WebGPU and Web Audio port, not part of the executable: [web/](web/)

## Build and run

Open PowerShell 7 in the repository root. The script sets up the MSVC environment itself.

```powershell
.\build.ps1 -VerifyTools                  # check tool versions first
.\build.ps1 -Configuration Debug -Run     # windowed build with diagnostics
.\build.ps1 -Configuration Compo -Run     # Crinkler-compressed, borderless; fails above 4,096 bytes
.\build.ps1 -Release                      # three Compo builds copied to release\
```

Outputs, including the executable and the Crinkler, size and toolchain reports, go under `out\` and are not tracked. `-Release` copies `plankton.exe` (current desktop resolution) and the fixed-size `plankton_1920x1080.exe` and `plankton_1280x720.exe` to `release\`, which is tracked, and zips everything in `release\` into `release\plankton.zip` for upload. One fixed size can be built with `.\build.ps1 -Resolution 1280x720`, which goes to `out\Compo-1280x720\`. `release\plankton.nfo` is the compo's information file and is maintained by hand.

Escape exits both builds. The Compo build also exits by itself at the end of the run.

## Inspecting a Debug run

Debug writes shader and OpenGL diagnostics to `in4k.log` and accepts options that capture the back buffer to `shot.bmp`:

```powershell
.\out\Debug\plankton.exe -shot900                       # fixed 60 Hz step, capture at frame 900
.\out\Debug\plankton.exe -live20                        # real timing, capture after 20 seconds
.\out\Debug\plankton.exe -shot900 -width1280 -height720 # exact client size
.\out\Debug\plankton.exe -shot900 -steps20              # 20 simulation steps per frame
```

- `-shot` is reproducible, so use it to compare a change. `-live` runs the real timing path, with the sound's position as the clock.
- `-width` and `-height` check the two TRSAC resolutions without changing display mode.
- `-steps<k>` reaches a late state quickly and gives byte-identical output.
- `-MinifyShaders` on the build compiles the same shader text as Compo, for frame-by-frame comparison.
- `-Optimize` on the build uses the Compo flags, so `ocean.wav` is exactly what Compo plays; `cmp` it against an earlier build to prove a size change left the sound alone.

These options are compiled out of the Compo build.

## Recording a video

`-record<fps>` writes every frame of the run from the back buffer to `frames/frameNNNNN.bmp` and exits at the end. It is more reliable than screen capture, which can record the wrong window.

```powershell
.\out\Debug\plankton.exe -record30 -width960 -height540
ffmpeg -framerate 30 -i frames\frame%05d.bmp -i ocean.wav -shortest -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 160k in4k-intro.mp4
```

Needs [ffmpeg](https://ffmpeg.org/) (`scoop install ffmpeg`). `ocean.wav` is longer than the run, so `-shortest` trims it. Delete `frames\` after encoding; it is not tracked.

To try a different attractor, change the seed in `hash()` in `src/shaders/particles.comp.glsl`. Nothing is seeded from the clock.

## References

- [in4k knowledge base](https://in4k.github.io/): techniques and tools for 4k intros.
- [Crinkler](https://github.com/runestubbe/Crinkler) and its [manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt).
- [Shader Minifier](https://github.com/laurentlb/shader-minifier).
- [MSVC command-line documentation](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line).
