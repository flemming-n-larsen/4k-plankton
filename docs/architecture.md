# Architecture

This document describes the architecture of **Plankton**, a PC in4k intro, and records the constraints, boundaries, and decisions behind it. Where an alternative approach does not work, the reason is kept next to the decision so it does not get retried.

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
| Resolution | Must supply at least a 1920x1080 and a 1280x720 version of the `.exe` |
| Runtime | 5 minutes maximum |
| Submission | Entry must include an information file |

Two rules constrain the code, not the packaging. The intro must render
correctly at both required resolutions, so every screen-space quantity is
derived from the viewport, never from pixel constants. And because both
compo machines must show the same thing, the simulation is advanced by measured
time rather than per frame, so it evolves at the same rate regardless of frame
rate. That time is the sound's playback position, not a wall clock (see Audio
direction).

The entry ships as **three executables**, because the rules ask for a 1920x1080 and a 1280x720 version of the exe:

- `plankton.exe` runs borderless fullscreen at the **current desktop resolution**. It is resolution-independent, so the organizer sets the machine and the intro follows; 1920x1080 and 1280x720 have been confirmed to render identical framing and structure.
- `plankton_1920x1080.exe` and `plankton_1280x720.exe` are the same program with the window size fixed at build time (`COMPO_WIDTH` and `COMPO_HEIGHT`). They open a borderless window of exactly that size in the top-left corner, so they fill the screen when the desktop is set to that resolution. A fixed size is a constant instead of two `GetSystemMetrics` calls, so each is about 24 bytes smaller.

None of them switches the display mode, which avoids the risk of a forced mode switch failing on the compo machine. `build.ps1 -Release` builds all three and copies them to `release\`.

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

For the native Win32 executable, browser APIs do not apply. The separate
[browser showcase](../web/) uses WebGPU for its compute pass and WGSL shaders;
it is a presentation port and is not included in the competition executable.
WebGPU does not expose variable-size OpenGL point sprites, so its renderer
expands each particle into a small quad. The browser version keeps the same
simulation timeline and procedural scene, with output that is close rather
than pixel-identical to the native build.

## Toolchain decision

The baseline is a fully headless Windows command-line toolchain. Neither Visual Studio nor Visual Studio Code is required.

The initial stack is:

- **Standalone Microsoft C++ Build Tools and Windows SDK** for `cl.exe`, Windows headers, and import libraries, targeting Win32/x86;
- **PowerShell build scripts** as the build driver and the documented interface used by developers and coding agents;
- **Shader Minifier** for release shader processing;
- **Crinkler** as the final linker and executable compressor.

CMake is not part of the initial baseline. The project is small, Windows-specific, and has a specialized generation and linking pipeline, so direct scripts keep every command and size-related option visible. CMake may be reconsidered if the project grows enough to benefit from it.

NASM, Python, Sointu, and other generators are optional, not baseline dependencies. They should be added only when a selected implementation requires them. In particular, choosing an audio backend may introduce its own editor, compiler, assembler, or code-generation step.

The Microsoft package retains the *Visual Studio Build Tools* product name, but it installs the compiler and SDK without requiring the Visual Studio IDE. Build scripts should initialize its command-line environment and invoke the tools directly. See [toolchain.md](toolchain.md) for the stack responsibilities and intended command interface.

## Architectural boundaries

The intro should be organized around replaceable subsystems, not one framework-specific implementation:

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

The audio is a synthesized ocean ambience, matching the underwater visuals, with a tonal layer over it that gives the run a key without giving it a beat. It lives in its own translation unit, `src/audio.c` / `src/audio.h`, keeping the audio subsystem's boundary with platform and graphics code visible in the source layout rather than only in comments.

`AudioStart()` synthesizes the whole run into one buffer of raw 16-bit samples at startup and plays it through `waveOut` as a single shot, not a loop. There is no stop call: the intro ends by exiting the process, which stops playback anyway, so a teardown function would only cost bytes. `mmsystem.h` is excluded by `WIN32_LEAN_AND_MEAN`, so the four `waveOut` functions and their structures are declared by hand rather than pulling in that header, matching the rest of the codebase's style.

**The sound is the clock.** `AudioSamples()` returns how many samples the device has played, from `waveOutGetPosition` in `TIME_SAMPLES`, and `Run()` takes its time as that count divided by the sample rate. The picture therefore starts when the sound really starts and stays locked to it, instead of two clocks, a tick count for the picture and the sound card's own for the sound, that were started a few milliseconds apart and could be pulled apart. That matters when the device is slow to start: on one run the sound was heard seconds behind the picture, and an 8000 ms delay of the picture lined them up. With the sound as the clock the picture waits for it. The position is fine enough to drive the simulation: sampled in a tight loop it changes about every 0.1 ms, the first value arrives about 18 ms after the write, and it advances 1.0000 seconds per wall-clock second. If there is no buffer or no device, `AudioSamples()` counts from the call to `AudioStart` using `GetTickCount` instead, so the intro still plays, silently, rather than standing on a black frame; that fallback costs 22 bytes, and the whole change 44 (3985 to 4029). It does not help a machine that cannot compute 60 steps a second: the simulation cannot skip steps, because it must stay deterministic, so there the picture still falls behind the sound. The raw samples need no RIFF header, which the Debug build adds only when it writes `ocean.wav`.

The synthesis itself is a small DSP loop, not a sample or a tracker song:

- A two-stage one-pole low-pass filter on white noise gives a "surf" body. It is where most of the weight sits: 61% of the finished mix's energy is below 200 Hz, and a further 38% between 200 Hz and 1 kHz.
- Foam is differenced noise - a 6 dB/octave rise - taken back down by *three* one-pole stages. One stage only cancels that same 6 dB/octave, so nothing above the corner rolls off and the layer reads as bright hiss sitting on top of the water. Three stages fall 18 dB/octave, giving the foam a darker, more underwater band while leaving the surf body clear.
- The loudness envelope eases between random waypoints five seconds apart (smoothstep interpolation via the shared `Waypoint` helper), giving slow, full-range swells instead of a constant level. Two seconds is too fast: a new wave arrives before the ear has read the last one as a wave, and the result is churn.
- A second, slower waypoint nine seconds apart eases the amount of foam mixed in, so the ambience's brightness drifts between calmer and hissier over time. Nine is not a multiple of five, so the two envelopes drift against each other instead of lining up.
- The foam gate has a floor under it, `.2 + .8 * shaped * shaped`. The squared swell on its own puts the foam through a 94x range and silences it outright for two seconds at a time whenever the waypoint lands near zero, which is what makes the ambience sound washy: the layer is not varying, it is switching off. With the floor, the foam's quiet extreme is 26% of its median instead of 4%, and no half-second window in the run falls below a quarter of the median. The foam mix and reveal boost are deliberately lower than the surf body, so its movement stays atmospheric rather than reading as constant noise.
- The ambience alone is passed through a soft-knee saturation (`x / (1 + |x|)`), not a hard clamp, so loud swells round off instead of clipping. The finished sum is limited by a cubic soft clip, `x - x^3/3`, which is described below.
- A scripted macro envelope over the top of those random swells follows the picture: quiet and close while the water is deep, opening out across the ascent, swelling briefly under the wave of light at seventeen to twenty-one seconds, surging as the word snaps together at thirty-five, settling again over the held word, and fading with the image between forty-eight and fifty-six seconds. The surge opens the foam further than the level alone does, so the reveal arrives as a wash of white water rather than merely louder surf. Measured per-second RMS, as a fraction of the run's loudest second, runs about 0.04 through the deep opening, 0.24 by second twelve, 0.65 by second nineteen, peaks at 1.00 as the word snaps together at seconds 35 and 36, and stays above 0.9 through second forty before the fade takes it down. These windows are a fifth copy of the visual timeline and must be kept in step by hand.
- The buffer is one shot covering the whole run rather than a short loop, which allows an envelope written against the clock, and it needs no crossfade at all. It is generated longer than `END_SECONDS` because audio plays in real time while the visuals are driven by `sim_time`: a machine that cannot hold 60 Hz stretches the picture against the sound, and the margin keeps the sound from running out first. A looped buffer was immune to that and this one is not, which is the price of having anything happen in the audio at a specific moment.
- Generation writes 16-bit samples straight into the block handed to the player, with no float scratch buffer and no second allocation.
- The 44-byte RIFF header exists only in the Debug build, as a table of eleven literal little-endian words written in front of the samples in `ocean.wav`. The Compo build hands raw samples to `waveOut` and carries no header.
- Generation is seeded by an explicit `AUDIO_SEED` constant, never the clock, so the ambience is identical on every run; change it to hear a different one.

The tonal layer sits on top of that bed:

- Sine voices come from a magic-circle oscillator, `y += k * z; z -= k * y` with `k = 2*PI*f / SAMPLE_RATE`. It is a symplectic rotation, so its amplitude neither decays nor grows across the whole run - there is no lookup table to store, nothing to renormalise, and no `sinf`, which `/NODEFAULTLIB` has no library for. Its output starts at zero, so a voice entering mid-run enters silently, and `k` can be changed at any sample without disturbing the phase, which is what makes the chord changes click-free. The small-angle `k` puts the true frequency out by `k*k/24`, which is 0.002% at the pad pitches and 0.12% at the accent.
- Three sustained voices play a five-chord progression in D minor - Dm, Dm/F, B-flat, F, Dm - changing at seconds 6, 18, 24, 30 and 38, so the harmony resolves brighter across the ascent and settles home under the held word. The voices sit in D3 to D4, above the surf body and below the foam band, so the chord occupies a gap in the ambience's spectrum rather than fighting it. No voice moves by more than a whole step at any change, which keeps an instantaneous pitch step reading as a chord change rather than a jump. Six distinct pitches serve fifteen chord slots, so the chords are stored as byte indices into a pitch table rather than as a flat grid of floats.
- The pad is multiplied by the same macro envelope as the water, so it swells with the ascent and lifts at the reveal instead of sitting at one level and flattening the run's dynamics.
- A sub at D2 enters between seconds 32 and 35 and leaves over seconds 43 to 53, so the reveal arrives with weight on a hall system rather than as nothing but more foam. A four-note motif - D5, F5, A5 and then D6, an arpeggio of the key, a tone every .55 seconds - is heard three times, and each tone is decayed by a repeated multiply, an exponential with a 0.65 second time constant and no `expf`. It is first a question that stops on F5: softly at 12 seconds, in the quiet stretch, and louder at 18.6 seconds under the wave of light. At 35.2 seconds it is answered, resolving on the high D6, and that statement is the one that follows the four letters locking. A review found the sound pleasant but short of a motif to remember, and asked for a short, recognizable figure that returns at the reveal; this reuses the oscillator and the trigger that were already there for the letter tones, and costs 69 bytes (3878 to 3947). Measured in the generated sound: the intended pitch is the strongest of the four in every one of the twelve notes, and the peak is 15868 of 32767, so nothing clips.
- Bubble cascades use deeper sine sweeps starting at 80..140 Hz and rising to four times that frequency over 62..100 ms, with a rounded attack and diminishing tail. This follows frequency analysis of the user's `sine-bubbles-underwater-fx.wav` reference, adapted to the requested slower, deeper sound; the reference is not embedded. Gaps inside a cascade are about 1.5..2.5 ms, giving the stream a little more separation, and each cascade contains six times the previous number of bubbles. The gap before the next separate cascade varies randomly over roughly 1.2..3.4 seconds in open water, and a chain is guaranteed during the 30..36 second NUTS assembly. Cascades also become more frequent around the 48..56 second fade-out. These cues use the audio clock and can drift from the visuals on machines that cannot maintain the simulation rate.
- The ambience passes through its own soft-knee saturation *before* the tonal layer is added, and the sum through the cubic clip. Folding a sustained low pad into a single knee would let it dominate the knee and duck the water on every swell.

Two details keep the synthesis small. `Smoothstep` takes its edges as ints in half seconds, because a float constant is a five-byte `push` at every call and an int under 128 is two; doubling is exact in floating point, so the result is bit for bit the same as with seconds (4029 to 4003 bytes). And the helpers that return a float, `Smoothstep`, `Oscillator`, `NextNoise` and `Waypoint`, are `__vectorcall`: the default x86 convention returns a float in the x87 register, so after every call the compiler had to store it to memory and reload it into an SSE register, about ten bytes each, and it mixed x87 and SSE arithmetic in the same expressions. With `__vectorcall` the value stays in `xmm0` (4003 to 3953 bytes). An optimized build of the old code also differed from the unoptimized Debug build in 3 of 1.65 million samples, by one least significant bit, because of that mixing; after the change the optimized and unoptimized builds are bit for bit identical, which is what makes the Debug `ocean.wav` a valid reference for what Compo plays. Compiling everything for x87 instead (`/arch:IA32`, with the missing `_ftol2_sse` supplied by a `fisttp` stub) was tried and made the file 50 bytes larger.

**The finished sum is limited by a cubic, not a knee.** The sum used to go through a second `x / (1 + |x|)`. That curve has no linear part, so it works on everything: measured in the generated sound, the signal in front of it has a median of .14 and 95% of it is under .51, and a pure sine through it carries 1.6% harmonic distortion at an amplitude of .1, 4.4% at .3 and 6.7% at .5. The pad and the sub are pure sines, which is where distortion is easiest to hear, and the waves are noise that the knee also intermodulates with them. Taking the part of the output that is not a linear copy of the input as the measure, over the three stretches 10 to 16, 26 to 32 and 36 to 44 seconds the old curve gives 5.5%, 8.9% and 13.4%; the cubic gives 0.7%, 2.5% and 8.4%. The cubic is nearly linear for small amplitudes and only bends over near 1, and `(1 - x*x/3)` costs less than the old division and the absolute value. It changes sign at 1.73, so it needs the sum to stay below that. Since the sound is generated from a fixed seed and is the same on every run, that was checked once rather than guarded: the sum reaches about 1.12 at its largest, and the clamp that would have enforced it cost 18 bytes (4020 without it, 4038 with). The output scale went from 30000 to 25000 to keep the loudness: RMS per stretch is between 0.88 and 1.11 of what it was and the correlation with the old sound is .995, so it is the same sound with less added to it. Compo 4007 to 4020 bytes. This is a change to what is heard, not only to a number, and should be judged by ear; it is its own commit so it can be reverted.

None of this needs the CRT: the PRNG and filters are pure integer/float arithmetic, and every float-to-integer conversion casts through a signed `int` rather than `unsigned`, since a direct float-to-unsigned cast pulls in a CRT helper that `/NODEFAULTLIB` has no library for.

### Why no synthesizer

The hand-written synthesis was extended with the tonal layer above rather than replaced. The following were considered and are not needed at this scope; they remain the options if the intro ever wants a sequenced, multi-instrument score, which the byte budget does not currently justify:

- [Sointu](https://github.com/vsariola/sointu), a 4klang-derived synthesizer with x86 output and a song-to-assembly compiler;
- [4klang](https://github.com/gopher-atz/4klang), the established VSTi and assembly-based workflow;
- [Oidos](https://github.com/askeksa/Oidos), an additive synthesizer with a Renoise-based conversion workflow;
- shader-based audio, where music is generated procedurally alongside the visual shader;
- extending the current hand-written synthesis further, with a simple sequencer.

The eventual decision should be based on the compressed size of the complete intro, sound quality, CPU/GPU cost, synchronization quality, development workflow, and toolchain reproducibility.

## Framework

No external framework is used. The intro is a minimal native Win32/OpenGL
executable with hand-written platform, graphics and audio code, which was chosen
against these criteria:

- final Crinkler-compressed size, including platform setup, graphics, audio, and data;
- compatibility with the intended Windows systems and competition machine;
- fast development and debugging workflow;
- clear integration points for generated shader and music data;
- reproducible builds with pinned tool versions;
- no runtime dependencies that the competition machine will not provide.

An existing 4k framework and a shader-audio framework were both considered. At
the size the intro actually reached, neither pays for itself: the platform layer
is under a hundred lines and the whole executable is well inside the limit, so a
framework would add a dependency and a build step to solve a problem the project
does not have.

## Build variants

There are two:

`Debug` runs in a window, reports shader compile and link failures, captures
frames, and records the run. Pass `-MinifyShaders` and it compiles the same
shader text the Compo build uses. `Compo` is the size-optimized executable: it
links through Crinkler and fails the build above 4096 bytes.

The Compo build is not the only artifact. Debug is what a change is measured
against: it renders identical frames, writes the generated `ocean.wav`, and can
capture a reproducible frame at an exact simulation step, none of which the
Compo build can do.

## The intro

Plankton is a CRT-free Win32/x86 executable running a 3D
[Particle Life](https://particle-life.com/) simulation on the GPU, named after
the drifting swarm it depicts, with synthesized audio (see Audio direction
below). The run is 59 seconds long. GLSL source is kept
readable under `src/shaders/` for Debug and minified into generated C strings
for Compo. It has no external graphics framework or runtime dependency beyond
the Windows system libraries, the OpenGL driver, and `winmm.dll` for audio
playback.

**Debug** runs in a normal window and reports shader compile and link failures.
**Compo** runs in a borderless primary-monitor window, links with Crinkler, and
fails the build if the executable exceeds 4096 bytes.

### Platform layer

The window uses the predefined `"static"` class instead of one registered by
the intro. That class already supplies a window procedure, so there is no
`RegisterClassA` call, no class name string, no instance handle, and no window
procedure of our own. The intro never acts on a message - it ends on Escape or
on its own clock - so the Compo build only takes one message a frame with a
single `PeekMessageA` and dispatches nothing; Debug runs a full pump so the
harness window stays interactive. The one message a frame is not optional: a
window whose thread takes no messages for five seconds is marked as hung, and
Windows may then put a frozen "Not responding" copy over the picture. Without it
`IsHungAppWindow` reported the Compo window hung within seconds; with it the
window responds for the whole run, for 13 bytes (3881 to 3894). The close button
still does nothing.

The Compo window is a borderless `WS_POPUP` the size of the screen, created with
`WS_EX_TOPMOST` so the taskbar cannot sit over it, and the cursor is hidden with one
`ShowCursor(0)` call, since the arrow would otherwise stay on top of the picture for the
whole run. Together they cost 9 bytes (3989 to 3998). This is borderless fullscreen, not
exclusive fullscreen: switching the display mode with `ChangeDisplaySettings` was
rejected earlier for the risk of a forced mode change failing or flickering on the
compo machine, and it would cost more bytes. Checked on the 2560x1440 desktop: cursor
hidden, window topmost and foreground, rectangle 0,0 2560x1440. A screen grab through GDI
returns black for this window, so it cannot be used to judge the result. The Debug build
stays an ordinary window with a title bar and a visible cursor; to see it the way Compo
does, start it with `-width<w> -height<h>` set to the desktop size, which gives a
borderless popup.

There is likewise no teardown. `ExitProcess` releases the render context, the
device context and the sound, so calling `wglMakeCurrent`, `wglDeleteContext`
and `ReleaseDC` by hand would only add code and import entries.

The Compo build checks no errors while it sets up: without a window or an
OpenGL 4.3 context there is nothing to show, so the checks would only cost
bytes. Debug keeps them, with the exit codes. In Compo the main function is also
the entry point itself and ends in `ExitProcess(0)`, rather than a
`WinMainCRTStartup` that calls it; the two together saved 33 bytes (3914 to
3881). The `PIXELFORMATDESCRIPTOR` is an initialised `static` rather than a
local filled in field by field: mostly-zero data costs almost nothing, the
stores cost 45 bytes (3959 to 3914). Folding `LoadOpenGl`, which is called
once, into the main function was tried and was 3 bytes larger.

The framebuffer size and the resolution uniforms are resolved once before the
main loop instead of every frame: a `WS_POPUP` window's client area is the whole
window, so the screen metrics passed to `CreateWindowExA` are also the
framebuffer size, and no `GetClientRect` is needed in the Compo build. There is
no `glViewport` call at all - the viewport already covers the whole window when
the context is first made current, and neither the window nor the resolution
ever changes.

No uniform is looked up by name. Every shader fixes its locations with
`layout(location = N)`: `t` is 0 in all three programs, `d` (compute) and `r`
(vertex and background) are 1, so `glGetUniformLocation`, its name in the
function table and the uniform-name strings are gone (4000 to 3959 bytes). The
background shader needed `#version 430` for this, and with it an
`out vec4 colour` in place of `gl_FragColor`.

### Simulation and rendering

16384 particles of 6 types are held in a single shader storage buffer as a flat
`vec4` array: particle *n* occupies `v[n * 2]` (position, with `.w` carrying the
type index) and `v[n * 2 + 1]` (velocity). A flat array instead of an array of
two-`vec4` structs because Shader Minifier is run with `--preserve-externals`,
which protects the block and its field names - the struct and the long field
names would survive minification at every use site.
Every frame a compute shader applies the standard Particle Life force curve over
all pairs, and points are drawn straight from that buffer.

Three decisions are worth recording:

- **No data is embedded.** The attraction matrix and the initial positions are
  generated from a hash inside the shader, costing zero bytes. The seed constant
  in that shader's `hash()` selects a different but fully reproducible attractor;
  nothing in the intro is seeded from the clock, so every run is identical.
- **Shader constants are literals, not `#define`s.** Shader Minifier does not
  substitute object-like macros, so a `#define` costs both its own line in the
  minified output and its full-length name at every use site. The values are
  named in comments instead, which are stripped and therefore free. The same
  applies to the timeline constants duplicated across the shaders: the seconds
  themselves appear in the code and the comment says what they mean.
- **The simulation runs on a fixed timestep.** `Run()` follows the sound's time but
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
  the same intro slightly slowly, not a different one. Verified by
  comparing captures from the synthetic fixed-rate `-shot` path against the
  wall-clock `-live` path at the same time index: byte-identical.
- **A type always attracts itself.** With a purely random matrix, 3D Particle
  Life settles into a uniform gas: there is too much room for cohesion to win.
  Biasing the diagonal positive is what makes distinct cells form.
- **The world does not wrap.** Particles are held by a soft central containment
  force applied once per particle, outside the pair loop. The classic periodic
  box would instead put a minimum-image test inside the O(n^2) loop, costing
  both bytes and time in the hottest code, and it pops at the box faces.

**A tight core over a faint halo, not one soft blob.** The sprite falloff is an
eighth-power term carrying most of the light with a squared term as a wide halo,
rather than the squared term alone. A squared falloff is still at half its peak
54% of the way out, so sixteen thousand of them read as fog and the cell
structure the simulation produces is invisible; the eighth power reaches half
peak at 29%. The two weights are chosen to hold the total emitted light where the
single term had it - radially the squared term integrates to 1/6 and the eighth
to 1/18 - so the change sharpens the image without altering its exposure, which
measures as a mean frame luminance within 0.2% of the single-term version.

Sharpening the profile is what makes this affordable at all. The obvious fix,
shrinking the sprites, would undo the viewport-relative sizing below and bring
back the half-empty sea; concentrating the same light into a core costs nothing
in coverage.

**Sprite size scales entirely with viewport height.** Both `gl_PointSize` and
its upper clamp in `particles.vert.glsl` are expressed as fractions of the
viewport height uniform, so
1080p, 720p and any downscaled capture render the same image - verified by
comparing mean luminance at a fixed time index across all three, which agree to
within 0.1%. The clamp exists to stop a particle drifting close to the camera
from filling the screen with a single sprite, but as an absolute pixel constant
it capped *every* sprite at the compo resolutions: the camera sits inside the
swarm, so the whole cluster is near and every sprite wants to be larger than the
cap. The effect was a sea that looked half empty on anything above 540p while
looking correct in downscaled test captures.

**The camera swims inside the cluster.** The camera's free-swim path in
`particles.vert.glsl` has radius .5, well within the .9 containment shell. This is what fills the frame with creatures. From outside the
shell - at any distance, however close - the cluster still projects as a
silhouette, and a ray toward a screen corner only grazes that silhouette, so the
corners stay empty however far the camera dollies in; dollying just drags the
empty band outward with it. From inside, every ray exits through the swarm and
there is no edge anywhere on screen. It also brings the whole cluster within
about 1.4 of the camera, so the depth absorption barely engages and the creature
colours stay saturated. It is not pushed closer than .5 because that is also
the radius of the circle the camera travels, and a smaller one leaves too little
parallax for the drift to read. It doubles as the reference depth of the
per-particle distance fade, so overall brightness does not track camera distance.

A box-shaped containment volume would be the other way to reach the corners, and
it would light them more evenly, since a ray toward a corner passes through the
full depth of a box instead of grazing a sphere. It is rejected because a
world-axis-aligned box shows flat faces and straight silhouette edges that sweep
across frame as the camera orbits - it reads as a tank rather than open water -
and it would flatten cells against the faces and pile them in the corners.

### The ascent

The free-swimming stretch is the majority of the run, and on its own it does not
develop: the orbit rate is constant, the elevation wobble is constant, and
without something changing, thirty seconds of a drifting cluster reads as a
holding pattern rather than a build. The ascent is what develops. Over seconds 4
to 40 the scene rises from deep water toward the surface, so the light grows, the
caustics and god rays strengthen, and the plankton recover the colour that depth
was absorbing. The word then forms in bright, near-surface water.

The camera does not actually travel. Moving the eye upward would drag the bounded
swarm off centre, and there is nothing above the cluster to rise past. The ascent
is carried entirely by light and by the bubbles below - which is also why the
bubbles are not decoration: without something streaming past, brighter water
alone does not read as motion.

In `background.frag.glsl` every lit term already scaled with screen height, so
the ascent folds into that single factor and drives the depth gradient, the
caustics and the rays at once. `particles.vert.glsl` carries its own copy, easing
off the distance absorption over the same seconds. Measured mean frame luminance
ran 25.7 at five seconds, 44.7 at twenty and 76.6 at forty when first measured; after the
background and sprite weights were pulled back (see Background and palette) it runs about
35 at eight seconds, 43 at twenty and 63 at forty.

### NUTS formation

The free-swimming cluster is the main effect. Once, over the 30-to-36-second window,
the cluster dissolves into the word NUTS
and stays formed for the rest of the run: a finale, not a recurring
event. The outer bound, `smoothstep(30., 36., t)`, is duplicated in
`particles.comp.glsl` (which moves the particles) and `particles.vert.glsl`
(which eases the camera to a front-on view over the same seconds and pulls it
back out of the swarm, from .5 to 1.42), with a comment on each marking
them as needing to match - though as of the entry curve described below, the
two copies share only that outer bound, not the same inner shape. The camera
has to leave the cluster for the word to be legible at all, so the reveal is a
retreat from the plankton rather than a push in. Height is necessarily far
less full than width, since the word's 4-letter aspect ratio is much wider
than the 16:9 frame.

The pull-back distance is set by how close the word comes to the frame edge. Its
widest point is the N's left vertical, at -1.30 in world units once the stroke
jitter is counted. At a distance of 1.3 that projects to -.956 of the frame
half-width with the camera dead ahead - a 4% margin, and nothing left for the
camera to move. At 1.42 the same edge sits near -.91 even at the extreme of the
drift below, which measures as 60 pixels of clear margin on the left and 126 on
the right at 1280x720 in the worst frame of the hold. It costs about a tenth of
the word's size and is the difference between a logo with air around it and a
logo with a letter cut off the side of a projector screen.

The whole scene also fades: in from black over the first 5 seconds, and back to
black over seconds 48 to 56, which is
12 seconds after the reveal ease ends, ending the intro on the dissolving word
rather than an abrupt cut.

The fade-in is short because the ascent means the intro opens on its darkest
frame, and the target venue is a projector in a blacked-out hall. Modelling one
- an elevated black floor from a finite contrast ratio, and a cinema gamma
nearer 2.5 than the 2.2 the frame is authored for - costs roughly a fifth of the
midtones: a frame whose median code value is 50 on a monitor lands near 40 on
the projector. The deep-water look survives that comfortably, because what
carries it is bright points against black, not shadow detail. What did
not survive was the opening: over an 8-second fade the frame at 2 seconds was
90% below code value 16, which is an empty screen for the first seconds of a
compo entry. At 5 seconds the same frame reads clearly as a swarm in deep water.
The floor of the ascent was deliberately not raised to compensate - that would
spend the contrast the arc is built on. The ease ending is not the moment the
logo stops moving, because the particles still have to damp into their targets.
With the original spring (see the next paragraphs) that took about 3.7 more
seconds and the letters were still soft at 40 seconds; with the stiffer spring
they are sharp about a second after the ease ends. Timing the fade from the end
of the ease instead makes the logo appear to fade the moment it arrives. The same `min` of two `smoothstep`s appears a
third time in `background.frag.glsl`, folded multiplicatively into the particle
colour and into the background's final colour, rather
than added as a separate overlay quad.

The Compo build then exits on its own, rather than sitting on the black
screen indefinitely until the operator intervenes: `src/intro.c` holds a
fourth copy of this timeline, `END_SECONDS`, set to the moment the fade
reaches full black (56 seconds) plus a further 3-second hold, and breaks the main loop once `sim_time`
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
enough that the strokes read as solid bodies instead of wires and the
counters of N, U and S close up;
during the hold, that force fully replaces the Particle Life pairwise force
rather than blending in a residual of it, since the extra density of points
compressed onto a thin curve would otherwise amplify any residual into
clumps.

**The gather precedes the word.** The plain `smoothstep(30., 36., t)` used to read as
individual particles converging on a shape rather than the free-swim cells gathering
and then resolving into it: `mix(force, seek, reveal)` blends the seek term in from
the first instant `reveal` is nonzero, so even a small fraction of it sustained across
most of a six-second window is enough to steer every particle toward its own fixed
target well before anything looks arrived. The fix moves only the seek force's own
lower bound from 30 to 34, a same-length literal that costs nothing: for `t` under 34,
`reveal` is exactly 0, bit-identical to ordinary free swim, and the same hermite ease
used everywhere else in the file then carries it to full override over the last two
seconds, landing at 36 exactly as before. `pow(smoothstep(30., 36., t), k)` was tried
first and rejected - it costs a call plus an exponent constant, and even at a high `k`
only approaches zero asymptotically rather than reaching it, so it re-admits a fainter
version of the same early-steering bias across the whole window instead of removing
it. This is the release's logic run in the opposite pacing, not its curve mirrored in
time: the release eases across its entire window because the word must visibly
dissolve throughout it, while the entry needs nothing to resolve until near the end,
so only the last two seconds carry the ease.

The camera is deliberately not recoupled to this narrower window. `particles.vert.glsl`
keeps dollying back over the full 30-to-36 seconds, so it is already framed correctly
by the moment the letters snap together rather than compressing the .5-to-1.42 retreat
into the same two seconds as the snap, which would stack a whip-zoom on top of it. The
cells are still worth watching while the camera recedes through seconds 30 to 34, so
the wider camera window reads as continuity with the preceding free swim rather than
an empty establishing shot.

The velocity-driven brightness accent below is also left as it was, and will now show
a sharper, more concentrated peak in the last two seconds instead of a gentler one
spread over six - the same total convergence delivered in a third of the time. That
reads as a flare-in answering the release's own flare-out rather than a flaw; if a
measured frame ever shows it clipping, the fix is narrowing the entry window further
(34 to 33 or lower), not touching the shared brightness formula, which has its own,
separately tuned history.

**The spring is stiff enough to settle.** The seek force is
`(target - position) * k` against a velocity damping of `exp(-17 * d)` per step.
With the original `k = 9` that is a heavily overdamped system, `x'' + 17x' + 9x = 0`,
whose slow root is -0.55 per second: a time constant of 1.8 seconds. A review
render showed the letters still blurred at 40 seconds and only crisp near 44,
when the release begins, so the logo had almost no time to be looked at. At
`k = 60` the roots are -5 and -12 per second, a time constant near 0.2 seconds,
and the word is sharp from about 37 seconds with about seven seconds of hold.
The change is one literal. The speed-driven brightness accent below scales
with the stiffer spring, so its coefficient dropped from 1.5 to .5 to keep the
arrival from burning out into white blobs. The audio sub and surge moved to the snap.

**The word assembles letter by letter.** The seek ramp is per particle:
`smoothstep(33., 35., t - (target.x + 1.3) * .7)`, so a particle's ramp is delayed by
where in the word it is going. N locks first, at about 35.3 seconds, then U, T and S at
about 35.8, 36.2 and 36.9, which turns one snap into four beats and lets the audio hit
each letter (the answer statement of the motif above, at 35.2, 35.75, 36.3 and 36.85 seconds). The release
stays global. The camera's own 30..36 second window is unchanged. While the left of the
word is forming, the cells on the right are still free-swimming, which reads as the
creatures being drawn into the word rather than as a shape fading in. `LetterTarget` is now evaluated once per particle outside the branch.
The dive, the letter-by-letter assembly, the ring and the four tones together cost 75
bytes (3964 to 4039); merging the two near-identical bubble-start branches in `audio.c`,
with the random draws in their old order so the sound is bit-identical, gave 50 back (3989).

**A ring crosses the held word.** The wave's shell expression appears a second time at
37 seconds, `length(offset) - 1.36 - (t - 37.) * .6`. The word is 1.42 from the camera at
its middle and up to 1.9 at its edges, so a camera-centred shell widening from 1.36 sweeps
across it from the middle outward, a flash at the moment the last letter is down. At an
amplitude of 4 it burnt the letters out to white (10.5% of pixels at 250 or more, against
about 4.4% in the hold); at .7 it is 6.6% and 3.2% white, which reads as a flash rather
than a loss of the letterforms.

**The word is let go again.** The formation term is
`smoothstep(30., 36., t) * (1. - smoothstep(44., 51., t))`, so after eight seconds
of hold the seek force unwinds and the word comes apart before the fade takes
it. Handing a swarm packed onto thin strokes back to the pairwise force is
precisely the failure the full override above exists to avoid, run deliberately
and in reverse: at that density the short-range repulsion tears the strokes
open, and the type attractions gather what is left into cells, so the logo
dissolves into living creatures instead of drifting apart as a cloud. The
velocity-driven brightness below flares the swarm while it breaks up, so the
release answers the surge at the snap without a scripted flash. It costs
six bytes.

Moving the letter targets outward instead - scaling `LetterTarget` up over the
same window - does not work: the override stays in charge, so the word
keeps its shape and merely grows past the edges of the frame, which reads as a
push-in rather than a release.

The release is deliberately not copied into the camera. The camera's free-swim
azimuth is absolute, `t * .13`, so unwinding its reveal would swing the shot
through six radians in seven seconds while the word was coming apart. The camera
holds its pulled-back front-on view and lets the subject dissolve in front of
it.

**The hold is not a still frame.** Once the reveal completes, both camera angles
would multiply out to exactly zero and the distance is constant, so with the
particles settled onto their targets nothing but the background would move for
the last stretch of the run before the fade. A much smaller oscillation therefore
fades in as the free-swim orbit fades out, and the word keeps turning gently
through the hold and on through the release.

That residual has to swing around zero, not be a fraction of the
free-swim term. Azimuth there is absolute, `t * .13`, so leaving even 4% of it
would put the camera 10.7 degrees off axis by the time the word lands and still
climbing - against the word's half-width that spreads the letters across depths
1.07 to 1.53, a 43% sprite-size difference end to end and a visibly skewed logo.
At the .045 radians actually used, the spread is about 1.30 to 1.42, which reads
as the word having some dimension: the N sits nearer than the S and is drawn
slightly larger. Measured over three seconds of the hold, the word's projected
width changes by 14 pixels and its centroid by about 4, against 3 and under 1
for a static camera.

**The swarm pulses.** `particles.vert.glsl` multiplies each particle's colour
by a flash keyed to its species: `.5 + .5 * sin(t * 1.2566 + type * 1.0472)`,
cubed, so the six species light and go dark in turn a sixth of a cycle apart
over a five-second period. It fills the run's emptiest stretch, the twenty-four
seconds of free swim in which the camera orbits and the light slowly grows and
nothing else happens.

Keying the flash to the species instead of to the particle index is what makes
it read as bioluminescence. The pairwise force sorts each species into its own
spatially coherent cell, so whole regions light in sequence and the light
travels through the cluster as a wave. Keyed to the index it is a uniform
per-particle flicker.

The cubing matters as much as the phase. A plain sine is invisible: it sits near
its middle most of the time, and with six phases spread evenly the frame's total
light never changes, so the eye reads a slow reshuffle of which species is
brightest instead of a pulse. Against the scene's own drift between seconds 15
and 18, the sine moves the red-tide species' emitted light by 1.7x and the cubed
flash by 4.5x. The cubed shape's mean is exactly 5/16, so `.5 + 1.6 * pulse`
(originally `.35 + 2.08 * pulse`, before the peak was lowered to stop dense cores
burning out) averages to one and the flash costs no exposure; measured mean frame luminance
falls 2.5 to 3.5%, the bright peaks clipping in the dense cell cores. Writing it
as a deviation from one lets the reveal damp it to a quarter with a single
multiply, since a strobing logo hurts legibility and the speed accent below
already owns that moment.

**The opening is a dive.** The camera starts 1.8 units further out and swims in to the
swarm over the first ten seconds (`1.8 * (1. - smoothstep(0., 10., t))` added to its
distance), so the run opens on the whole creature cloud hanging in the dark and ends
the dive among the colonies. At the original 1 unit the swarm still fills the frame,
since its projected radius at 1.5 is 1.27 of the half-height, and the dive only read as
the colonies growing. From about 2.3 it sits inside the frame as a ball of light, which
is what makes the dive read.

**A wave of light crosses the free swim.** Eight, sixteen and twenty-four seconds
looked alike in review, so between 17 and about 21 seconds a shell of light
leaves the camera and sweeps outward through the swarm
(`length(offset) - (t - 17.) * .5` in `particles.vert.glsl`, brightened by
`1. + 6. * exp(-30. * shell * shell)`). It is centred on the camera, not the
swarm: a shell around the swarm centre was tried first and only lit the few
colonies that happen to lie on the camera's slice of it, which read as scattered
flashes, while a shell around the camera crosses every direction at once and
reads as a front rolling over the whole frame. No gate is needed, since the
shell has negative radius before 17 seconds and is past the swarm after 21.
Mean frame luminance rises about 35% at the peak. The audio gets a small swell
under it and the chord change moves from 16 to 18 seconds to land on the front.
It cost about 60 bytes with the audio.

**Calm before, a mark after.** A second review found that the wave helped but that the picture
before it, during it and after it had much the same density, shapes and composition, and
asked for calm before the wave, a marked change, and a visible aftermath. Measured as mean
luminance in 1280x720 captures the old wave was a flash of about a second and a half, 40 to
54 and back to 46, with nothing before it and nothing after. Mean luminance is a poor gauge
for the species flashes, whose mean is held at one by design, so the frames were looked at as
well. Four changes, all in `particles.vert.glsl` and costing 34 bytes:

- the species flashes are held to a fifth of their strength until the wave (`awake`, a
  `smoothstep(17., 21., t)`), which wakes them, so the swim before it is calm and the swim
  after it is lively;
- the light dips by 60% for a moment centred on 17.9 seconds, as if the water drew breath,
  `1 - .6 * exp(-3 * (t - 17.9)^2)`: mean luminance 39.5 down to 35.2 at 18 s;
- once the wave has passed the swarm keeps a glow of +60% that fades over the next nine
  seconds (`smoothstep(18., 20., t) * (1. - smoothstep(21., 30., t))`), and the wave itself
  peaks at 59 instead of 54;
- the wave washes the water clear: the depth absorption of the plankton colours is lowered
  by .3 once it has passed, which is permanent, so the colonies are more saturated for the
  rest of the run (the held word moves by about 1% in mean luminance).

The ambience follows with a hush: `macro` is multiplied by `1 - .5 * hush`, where `hush` is a
triangle of two smoothsteps over 16.5 to 19.5 seconds, so the sound falls to 63% at 17.5 to 18
seconds and swells back as the wave and the second statement of the motif arrive at 18.6
seconds (RMS per half second, old and new: 4420 and 2777 at 18.0 s, 5028 and 4860 at 19.0 s;
nothing changes before 16.5 s or after 19.5 s). That cost 26 bytes.

**The reveal accents itself.** `particles.vert.glsl` scales each particle's
brightness by its own speed, read from the velocity half of the buffer that the
vertex shader otherwise never touches. For the six seconds of the migration every
particle is moving hard, so the swarm surges and then decays as it settles into
the letters. This is deliberately not a scripted flash: it costs one buffer read,
it lands exactly on the timeline without another constant to keep in step, and it
resolves on its own as the particles stop. Mean frame luminance runs 55 just
before the reveal, peaks at 85 as the word lands, and settles back to 78. While
the cluster is merely drifting it reads as a faint twinkle on the fastest
cells. With the stiffer spring the coefficient is .5 instead of 1.5.

### Background and palette

A third program draws the background before the points, giving an underwater
setting:
a vertical gradient from deep navy to lit teal, a moving caustic ridge pattern,
and god rays converging on a sun point above the top edge, all built from a
handful of `sin`/`atan` terms in normalized `[0,1]` screen space instead of
any texture or 3D geometry - this costs about 280 bytes for the whole effect.
It must replace what the previous frame left in the back buffer, while the
points drawn on top must add. Rather than switching blending off and on around
it every frame, the blend function is `GL_ONE, GL_SRC_ALPHA`, set once: the
destination is scaled by the source's alpha, the background writes alpha 0 and
so replaces, the points write alpha 1 and add (3893 to 3886 bytes). This also
removes the need for `glClear` - the background already overwrites every pixel.

The program is `background.frag.glsl` alone, with **no vertex stage**: in a
compatibility profile a program with only a fragment shader runs the
fixed-function vertex pipeline, which is exactly what a
`gl_Position = gl_Vertex` pass-through would have done. The geometry is a single
`glRecti(-1, -1, 1, 1)` covering clip space, rather than `glBegin` / three
`glVertex2f` / `glEnd` - one import and one call instead of three and five.

The shaft pattern is a single angular frequency whose amplitude is modulated by
a second, much lower one. On its own the single frequency gives nine identical,
evenly spaced shafts swaying as a rigid fan, which reads as a cog. Adding a
second fan on top is worse rather than better: where the two cancel they open
broad dark wedges that look like a seam in the water. Modulating the amplitude
instead varies the brightness from shaft to shaft and drifts that variation
along the fan over time, without any shaft ever disappearing.

Rising bubbles are drawn in the same pass, as two parallax layers of a hashed
cell grid scrolling upward. Both layers scroll by the same rate, so the coarser
one drifts more slowly in screen units purely because its cells are larger - the
parallax comes free from the scale rather than from a per-layer rate. Screen
space is sound here specifically because the camera never rolls: `up` is derived
from world up, so screen-vertical is world-vertical. The grid is built on an
aspect-corrected copy of the screen coordinate, without which the cells would be
16:9 wide and every bubble a visible ellipse.

Each bubble is an annulus, not a filled disc. A filled blob reads as
out-of-focus dust; a bright rim around a dark middle reads as a bubble catching
the light from above. Only cells hashing above a threshold carry one, so the
water is not a uniform lattice, and a second hash decorrelates vertical placement
from size so the layers do not show their rows. Density is the whole design
problem here: at the obvious settings the frame fills with bubbles that compete
with the plankton for attention, and the layer count and threshold exist to keep
them a background presence.

A review of the intro found the broad god rays and the many distinct bubbles still
competing with the colonies, and some colonies burning out to near white and losing
their detail. The god ray weight went from .9 to .55, the bubble brightness from .26
to .15 and the threshold from .78 to .84 (with the gain raised to 6 so the surviving
bubbles keep their strength), the sprite core weight from .5 to .44 and the species
flash peak from 2.08 to 1.6. Pixels with a channel at 250 or more fell from about
4.4% to 2.2% at eight seconds, at the price of about 13% of the mean luminance in
the free-swim stretch. This is constants only and costs no bytes.

The particles keep their plankton colours through the reveal - nothing brightens
or desaturates when the word forms. Raising luminance for legibility was tried
and dropped: mixing toward white reads as a white logo rather than a logo made
of creatures, which is the point of the shot.

The particle palette is a literal six-entry table in `particles.vert.glsl`,
`PLANKTON`, indexed directly by the particle's type - one colour per type, so
the cells read as distinct organisms instead of shades of one hue. The six are
drawn from real ocean life: copepod teal, bioluminescent cyan, algal sea green,
cyanobacterial deep blue, red-tide dinoflagellate red, and diatom gold. The
entry count is coupled to the species count in `particles.comp.glsl`. A cosine gradient
would be fewer bytes, but it cannot reach both the cool water tones and the warm
ones without also sweeping through hues that do not belong underwater. Particles
further from the camera are pulled toward a dark blue-black, mimicking how real
water absorbs colour with depth; this is what keeps the two warm entries from
reading as foreign at the back of the cluster.

The per-particle distance fade and the master fade are both folded into that
colour in the vertex shader instead of passed to the fragment shader as a
second varying. A point sprite has a single vertex, so one interpolated colour
is exactly what a separate colour and fade would have multiplied out to. The
fragment shader likewise clamps its sprite falloff at zero instead of
`discard`ing the corner fragments: a point sprite's colour is added (its alpha
of 1 keeps the destination), so a black fragment adds nothing and the image is the same without the branch.

### Debug-only tooling

The Debug build carries diagnostics that the Compo path compiles out entirely.
Shader logs and the GL version go to both the debugger and `in4k.log`. Three
command-line options make the output verifiable without a debugger or a screen
grab, by reading the back buffer in-process and writing `shot.bmp`:

- `-shot<frames>` runs a fixed 60 Hz timestep and captures at that exact frame,
  which makes a frame reproducible;
- `-live<seconds>` uses the real timing path, with the sound's position as the clock,
  and captures after that many seconds;
- `-width<w> -height<h>` sizes the client area exactly, so the two required
  compo resolutions can be compared without changing display mode;
- Timing log. Every Debug run attaches to the console it was started from and writes the same
  lines to `in4k.log`: when the samples were written to `waveOut`, when the first frame was
  presented, when the sound device actually began playing, and every five seconds the wall
  clock since the write, the sound's position and the picture's sim time, with how far the
  picture lags (`sim_minus_audio`, always a little negative: one simulation step plus the time
  to draw it, about 20 to 30 ms). The sound's position is the picture's clock, so there is no
  drift to see. Crinkler's unpack time is not part of this: it happens before `Run()` starts.
  With the normal Crinkler header the window appears after about 0.3 seconds on the
  development machine. With `/TINYHEADER` it took about 11.5 seconds, and that header was
  also 22 bytes larger (see `docs/toolchain.md`).
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

PowerShell exposes stable Debug and Compo commands. Coding agents and human developers should use those same commands; builds must not depend on IDE projects, IDE state, or manually configured editor tasks.

The concrete installation and invocation details will be added to [toolchain.md](toolchain.md) as the build is implemented. Tool versions, discovery rules, expected paths, compiler flags, Crinkler options, and generated-file policy must be pinned there once validated.

## Decisions taken

- **Audio backend**: hand-written DSP in `src/audio.c`, no synthesizer library and no embedded sample. See Audio direction above.
- **Playback API**: `waveOut` from `winmm.dll` (open, prepare, write and get the position), one shot, no loop and no teardown. `PlaySoundA` was used first and cannot report how far the sound has played, so the picture ran on a separate tick-count clock. DirectSound was not needed: `waveOutGetPosition` gives the sample position directly. The sound's position is the intro's only clock.
- **Generated files**: none are tracked. `out/` holds the executables, the Crinkler report and the generated shader headers; `.tools/` holds local third-party tools; `frames/` and `ocean.wav` are Debug output. All are rebuilt from source and ignored by Git.
