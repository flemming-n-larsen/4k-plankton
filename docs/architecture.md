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

The project supports runtime-generated audio instead of embedding a WAV, MP3, or other complete sound file in the final executable. This keeps sound within the same procedural, size-constrained, explicitly-seeded model as the visuals.

### Ocean ambience

The first audio milestone is a synthesized ocean ambience, matching the underwater visuals rather than a musical score. It lives in its own translation unit, `src/audio.c` / `src/audio.h`, keeping the audio subsystem's boundary with platform and graphics code visible in the source layout rather than only in comments.

`AudioStart()` synthesizes a loopable buffer once at startup and plays it through `PlaySoundA` with `SND_LOOP`, and `AudioStop()` stops it. `mmsystem.h` is excluded by `WIN32_LEAN_AND_MEAN`, so `PlaySoundA` is declared by hand rather than pulling in that header, matching the rest of the codebase's style. This satisfies the starting/stopping and swappable-backend parts of the audio boundary below; a stable playback position for visual sync is not implemented, since nothing in the intro currently needs to react to the audio.

The synthesis itself is a small DSP loop, not a sample or a tracker song:

- A two-stage one-pole low-pass filter on white noise gives a "surf" body; a low-passed, smoothed noise difference gives "foam" on top. Both stages keep the ambience soft rather than bright/hissy.
- The loudness envelope eases between random waypoints roughly two seconds apart (smoothstep interpolation via the shared `Waypoint` helper), giving slow, full-range swells rather than a constant level.
- A second, slower waypoint (roughly six seconds apart) eases the amount of foam mixed in, so the ambience's brightness itself drifts between calmer and hissier over time.
- The sum is passed through a soft-knee saturation (`x / (1 + |x|)`), not a hard clamp, so loud swells round off instead of clipping.
- The loop's tail is crossfaded into its head before use, so playback has no click at the wrap.
- Generation is seeded by an explicit `AUDIO_SEED` constant, never the clock, so the ambience is identical on every run; change it to hear a different one.

None of this needs the CRT: the PRNG and filters are pure integer/float arithmetic, and every float-to-integer conversion casts through a signed `int` rather than `unsigned`, since a direct float-to-unsigned cast pulls in a CRT helper that `/NODEFAULTLIB` has no library for.

### Open audio questions

Music, if the intro grows one, is a separate decision from this ambience layer and remains open. Candidates:

- [Sointu](https://github.com/vsariola/sointu), a 4klang-derived synthesizer with x86 output and a song-to-assembly compiler;
- [4klang](https://github.com/gopher-atz/4klang), the established VSTi and assembly-based workflow;
- [Oidos](https://github.com/askeksa/Oidos), an additive synthesizer with a Renoise-based conversion workflow;
- shader-based audio, where music is generated procedurally alongside the visual shader;
- extending the current hand-written synthesis approach with a simple sequencer.

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
[Particle Life](https://particle-life.com/) simulation on the GPU, titled
**Plankton** after the drifting swarm it depicts, with a
synthesized ocean ambience (see Audio direction below). GLSL source is kept
readable under `src/shaders/` for Debug and minified into generated C strings
for Compo. It has no external graphics framework or runtime dependency beyond
the Windows system libraries, the OpenGL driver, and `winmm.dll` for audio
playback.

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
- **The simulation runs on a fixed timestep.** `Run()` accumulates real time but
  integrates in constant `SIM_STEP` (1/60 s) increments, and every shader
  uniform is driven by `sim_time` rather than the wall clock. Feeding the real
  frame delta to the compute shader instead makes the run frame-rate dependent,
  and because Particle Life is chaotic that is not a subtle difference: the same
  binary produces a visibly different cluster on a 60 Hz and a 144 Hz machine,
  which is unacceptable for a compo where the intro is seen once on unfamiliar
  hardware. The step count is held as an integer with `sim_time` derived from it,
  not accumulated by repeated addition, since the accumulated float drifts
  against a multiplied time and costs a step at the comparison boundary.
  `MAX_CATCHUP` bounds the catch-up loop, so a machine that cannot keep up plays
  the same intro slightly slowly rather than a different one. Verified by
  comparing captures from the synthetic fixed-rate `-shot` path against the
  wall-clock `-live` path at the same time index: byte-identical.
- **A type always attracts itself.** With a purely random matrix, 3D Particle
  Life settles into a uniform gas: there is too much room for cohesion to win.
  Biasing the diagonal positive is what makes distinct cells form.
- **The world does not wrap.** Particles are held by a soft central containment
  force applied once per particle, outside the pair loop. The classic periodic
  box would instead put a minimum-image test inside the O(n^2) loop, costing
  both bytes and time in the hottest code, and it pops at the box faces.

**Sprite size scales entirely with viewport height.** Both `gl_PointSize` and
its upper clamp in `particles.vert.glsl` are expressed as fractions of `r.y`, so
1080p, 720p and any downscaled capture render the same image - verified by
comparing mean luminance at a fixed time index across all three, which agree to
within 0.1%. The clamp exists to stop a particle drifting close to the camera
from filling the screen with a single sprite, but as an absolute pixel constant
it capped *every* sprite at the compo resolutions: the camera sits inside the
swarm, so the whole cluster is near and every sprite wants to be larger than the
cap. The effect was a sea that looked half empty on anything above 540p while
looking correct in downscaled test captures.

**The camera swims inside the cluster.** `ORBIT` in `particles.vert.glsl` is the
radius of the camera's free-swim path, and at .5 it sits well within the .9
containment shell. This is what fills the frame with creatures. From outside the
shell - at any distance, however close - the cluster still projects as a
silhouette, and a ray toward a screen corner only grazes that silhouette, so the
corners stay empty however far the camera dollies in; dollying just drags the
empty band outward with it. From inside, every ray exits through the swarm and
there is no edge anywhere on screen. It also brings the whole cluster within
about 1.4 of the camera, so the depth absorption barely engages and the creature
colours stay saturated. `ORBIT` is not pushed closer than .5 because it is also
the radius of the circle the camera travels, and a smaller one leaves too little
parallax for the drift to read. It doubles as the reference depth of the
per-particle distance fade, so overall brightness does not track camera distance.

A box-shaped containment volume would be the other way to reach the corners, and
it would light them more evenly, since a ray toward a corner passes through the
full depth of a box rather than grazing a sphere. It is rejected because a
world-axis-aligned box shows flat faces and straight silhouette edges that sweep
across frame as the camera orbits - it reads as a tank rather than open water -
and it would flatten cells against the faces and pile them in the corners.

### NUTS formation

The free-swimming cluster is the main effect. Once, at 30 seconds in (`REVEAL_START`),
easing over 6 seconds (`REVEAL_EASE`), the cluster dissolves into the word NUTS
and stays formed for the rest of the run - a finale rather than a recurring
event. The same `RevealBlend` timeline constants are duplicated in
`particles.comp.glsl` (which moves the particles) and `particles.vert.glsl`
(which eases the camera to a front-on view over the same seconds and pulls it
back out of the swarm, from `ORBIT` to 1.3), with a comment on each marking
them as needing to match. The camera has to leave the cluster for the word to be
legible at all, so the reveal is a retreat from the plankton rather than a push
in. The word is scaled and the pull-back distance tuned
together so the finished word fills about 85% of the frame's width with a
comfortable margin at both 1920x1080 and 1280x720; height is necessarily far
less full, since the word's 4-letter aspect ratio is much wider than the
16:9 frame.

The whole scene also fades: in from black over the first 8 seconds
(`FADE_IN_END`) - slow enough that the procedural underwater background
(depth gradient, caustics, god rays) reads clearly before the particles
brighten to full strength - and back to black starting 9 seconds after the
reveal ease ends (`FADE_OUT_START`, derived from `REVEAL_START + REVEAL_EASE +
9`), over 8 seconds (`FADE_OUT_DURATION`), ending the intro on the held word
rather than an abrupt cut. The 9 is measured, not guessed: the ease ending is
not the moment the logo stops moving, because the particles are still damping
into their targets for another ~3.7 seconds after it. Frame-to-frame motion
does not fall below noise until about 39.7 seconds, so the fade starts at 45 to
leave a full 5 seconds of a genuinely settled logo. Timing it from the end of
the ease instead makes the logo appear to fade the moment it arrives. `MasterFade` computes this and is duplicated a
third time in `background.frag.glsl`, folded multiplicatively into
`particles.vert.glsl`'s `tint` and into the background's final colour, rather
than added as a separate overlay quad.

The Compo build then exits on its own, rather than sitting on the black
screen indefinitely until the operator intervenes: `src/intro.c` holds a
fourth copy of this timeline, `END_SECONDS`, set to the moment the fade
reaches full black (`REVEAL_START + REVEAL_EASE + 9 + FADE_OUT_DURATION` = 53
seconds) plus a further 3-second hold, and breaks the main loop once `sim_time`
passes it. This check is compiled out of Debug (`#ifndef DEBUG`), since
Debug's `-live`/`-shot` diagnostics need to keep running past that point on
demand.

The word is a vector font of lines and circular arcs - N as three line
segments, U as two segments and a half-circle, T as two segments, S as two
3/4-circles, each rotated 30 degrees to keep the arcs from meeting tangent at
a single pinch point on the centerline - not a bitmap or embedded glyph
image, matching the "no data embedded" principle used for the attraction
matrix and initial positions. Each particle is assigned a point on one stroke
by hashing its own index (kept well under the argument magnitude where GPU
`sin()` range reduction stays precise, to avoid aliasing distinct indices
onto the same output), so the same particle always seeks the same spot;
a perpendicular jitter of +/-.05 in font units gives each stroke real width,
enough that the strokes read as solid bodies rather than wires and the
counters of N, U and S close up;
during the hold, that force fully replaces the Particle Life pairwise force
rather than blending in a residual of it, since the extra density of points
compressed onto a thin curve would otherwise amplify any residual into
clumps.

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

The particles keep their plankton colours through the reveal - nothing brightens
or desaturates when the word forms. Raising luminance for legibility was tried
and dropped: mixing toward white reads as a white logo rather than a logo made
of creatures, which is the point of the shot.

The particle palette is a literal six-entry table in `particles.vert.glsl`,
`PLANKTON`, indexed directly by the particle's type - one colour per type, so
the cells read as distinct organisms rather than shades of one hue. The six are
drawn from real ocean life: copepod teal, bioluminescent cyan, algal sea green,
cyanobacterial deep blue, red-tide dinoflagellate red, and diatom gold. The
entry count is coupled to `TYPES` in `particles.comp.glsl`. A cosine gradient
would be fewer bytes, but it cannot reach both the cool water tones and the warm
ones without also sweeping through hues that do not belong underwater. Particles
further from the camera are pulled toward a dark blue-black, mimicking how real
water absorbs colour with depth; this is what keeps the two warm entries from
reading as foreign at the back of the cluster.

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
  wall-clock time while producing byte-identical output, since the simulation
  is fully deterministic.

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
