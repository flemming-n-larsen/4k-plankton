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
| Resolution | Must support both 1920x1080 and 1280x720 |
| Runtime | 5 minutes maximum |
| Submission | Entry must include an information file |

Two rules constrain the code, not the packaging. The intro must render
correctly at both required resolutions, so every screen-space quantity is
derived from the viewport, never from pixel constants. And because both
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

`AudioStart()` synthesizes the whole run into one buffer at startup and plays it through `PlaySoundA` as a single shot, not a loop. There is no stop call: the intro ends by exiting the process, which stops playback anyway, so a teardown function would only cost bytes. `mmsystem.h` is excluded by `WIN32_LEAN_AND_MEAN`, so `PlaySoundA` is declared by hand rather than pulling in that header, matching the rest of the codebase's style. This satisfies the swappable-backend part of the audio boundary below; a stable playback position for visual sync is not implemented, since nothing in the intro currently needs to react to the audio.

The synthesis itself is a small DSP loop, not a sample or a tracker song:

- A two-stage one-pole low-pass filter on white noise gives a "surf" body. It is where most of the weight sits: 61% of the finished mix's energy is below 200 Hz, and a further 38% between 200 Hz and 1 kHz.
- Foam is differenced noise - a 6 dB/octave rise - taken back down by *two* one-pole stages, not one. One stage only cancels that same 6 dB/octave, so nothing above the corner ever rolls off and the layer reads as bright hiss sitting on top of the water. Two stages fall 12 dB/octave, which puts the foam in a band a few hundred Hz wide and leaves half a percent of the mix's energy above 1 kHz: a small share of the energy and a large share of the character. The stages cost most of the layer's level, so the mix amount is scaled back up to hold the foam-to-surf balance.
- The loudness envelope eases between random waypoints five seconds apart (smoothstep interpolation via the shared `Waypoint` helper), giving slow, full-range swells instead of a constant level. Two seconds is too fast: a new wave arrives before the ear has read the last one as a wave, and the result is churn.
- A second, slower waypoint nine seconds apart eases the amount of foam mixed in, so the ambience's brightness drifts between calmer and hissier over time. Nine is not a multiple of five, so the two envelopes drift against each other instead of lining up.
- The foam gate has a floor under it, `.2 + .8 * shaped * shaped`. The squared swell on its own puts the foam through a 94x range and silences it outright for two seconds at a time whenever the waypoint lands near zero, which is what makes the ambience sound washy: the layer is not varying, it is switching off. With the floor, the foam's quiet extreme is 26% of its median instead of 4%, and no half-second window in the run falls below a quarter of the median, where seven did. The mix constants are scaled to hold the same median level, so only the evenness changes.
- The sum is passed through a soft-knee saturation (`x / (1 + |x|)`), not a hard clamp, so loud swells round off instead of clipping.
- A scripted macro envelope over the top of those random swells follows the picture: quiet and close while the water is deep, opening out across the ascent, surging as the particles let go at thirty seconds, settling again over the held word, and fading with the image between forty-eight and fifty-six seconds. The surge opens the foam further than the level alone does, so the reveal arrives as a wash of white water rather than merely louder surf. Measured per-second RMS, as a fraction of the run's loudest second, runs about 0.04 through the deep opening, 0.24 by second twelve, 0.62 by second twenty-five, peaks at 1.00 during the migration and holds around 0.78 over the word before the fade takes it down. These windows are a fifth copy of the visual timeline and must be kept in step by hand.
- The buffer is one shot covering the whole run rather than a short loop, which allows an envelope written against the clock, and it needs no crossfade at all. It is generated longer than `END_SECONDS` because audio plays in real time while the visuals are driven by `sim_time`: a machine that cannot hold 60 Hz stretches the picture against the sound, and the margin keeps the sound from running out first. A looped buffer was immune to that and this one is not, which is the price of having anything happen in the audio at a specific moment.
- Generation writes 16-bit samples straight into the block handed to the player, with no float scratch buffer and no second allocation.
- The 44-byte RIFF header is a table of eleven literal little-endian words copied into place, not a packed struct filled in field by field.
- Generation is seeded by an explicit `AUDIO_SEED` constant, never the clock, so the ambience is identical on every run; change it to hear a different one.

The tonal layer sits on top of that bed:

- Sine voices come from a magic-circle oscillator, `y += k * z; z -= k * y` with `k = 2*PI*f / SAMPLE_RATE`. It is a symplectic rotation, so its amplitude neither decays nor grows across the whole run - there is no lookup table to store, nothing to renormalise, and no `sinf`, which `/NODEFAULTLIB` has no library for. Its output starts at zero, so a voice entering mid-run enters silently, and `k` can be changed at any sample without disturbing the phase, which is what makes the chord changes click-free. The small-angle `k` puts the true frequency out by `k*k/24`, which is 0.002% at the pad pitches and 0.12% at the accent.
- Three sustained voices play a five-chord progression in D minor - Dm, Dm/F, B-flat, F, Dm - changing at seconds 6, 16, 24, 30 and 38, so the harmony resolves brighter across the ascent and settles home under the held word. The voices sit in D3 to D4, above the surf body and below the foam band, so the chord occupies a gap in the ambience's spectrum rather than fighting it. No voice moves by more than a whole step at any change, which keeps an instantaneous pitch step reading as a chord change rather than a jump. Six distinct pitches serve fifteen chord slots, so the chords are stored as byte indices into a pitch table rather than as a flat grid of floats.
- The pad is multiplied by the same macro envelope as the water, so it swells with the ascent and lifts at the reveal instead of sitting at one level and flattening the run's dynamics.
- A sub at D2 enters between seconds 29 and 32 and leaves over seconds 43 to 53, so the reveal arrives with weight on a hall system rather than as nothing but more foam. A single tone at D5 is struck on the sample the word forms and decayed by a repeated multiply - an exponential with a 0.65 second time constant and no `expf`.
- Bubble cascades use deeper sine sweeps starting at 80..140 Hz and rising to four times that frequency over 62..100 ms, with a rounded attack and diminishing tail. This follows frequency analysis of the user's `sine-bubbles-underwater-fx.wav` reference, adapted to the requested slower, deeper sound; the reference is not embedded. Gaps inside a cascade are about 1.5..2.5 ms, giving the stream a little more separation, and each cascade contains six times the previous number of bubbles. The gap before the next separate cascade varies randomly over roughly 1.2..3.4 seconds in open water, and a chain is guaranteed during the 30..36 second NUTS assembly. Cascades also become more frequent around the 48..56 second fade-out. These cues use the audio clock and can drift from the visuals on machines that cannot maintain the simulation rate.
- The ambience passes through its own soft-knee saturation *before* the tonal layer is added, and the sum through a second one. Folding a sustained low pad into a single knee would let it dominate the knee and duck the water on every swell.

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
on its own clock - so the Compo build runs no message pump at all; Debug keeps
one so the harness window stays interactive. The visible cost is that the close
button does nothing and Windows may mark the window unresponsive, which is
standard for a compo binary.

There is likewise no teardown. `ExitProcess` releases the render context, the
device context and the sound, so calling `wglMakeCurrent`, `wglDeleteContext`
and `ReleaseDC` by hand would only add code and import entries.

The framebuffer size and the resolution uniforms are resolved once before the
main loop instead of every frame: a `WS_POPUP` window's client area is the whole
window, so the screen metrics passed to `CreateWindowExA` are also the
framebuffer size, and no `GetClientRect` is needed in the Compo build. There is
no `glViewport` call at all - the viewport already covers the whole window when
the context is first made current, and neither the window nor the resolution
ever changes.

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
runs 25.7 at five seconds, 44.7 at twenty and 76.6 at forty.

### NUTS formation

The free-swimming cluster is the main effect. Once, over the 30-to-36-second window,
the cluster dissolves into the word NUTS
and stays formed for the rest of the run: a finale, not a recurring
event. The same `smoothstep(30., 36., t)` is duplicated in
`particles.comp.glsl` (which moves the particles) and `particles.vert.glsl`
(which eases the camera to a front-on view over the same seconds and pulls it
back out of the swarm, from .5 to 1.42), with a comment on each marking
them as needing to match. The camera has to leave the cluster for the word to be
legible at all, so the reveal is a retreat from the plankton rather than a push
in. Height is necessarily far less full than width, since the word's 4-letter
aspect ratio is much wider than the 16:9 frame.

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
spend the contrast the arc is built on. The 9 is measured, not guessed: the ease ending is
not the moment the logo stops moving, because the particles are still damping
into their targets for another ~3.7 seconds after it. Frame-to-frame motion
does not fall below noise until about 39.7 seconds, so the fade starts at 45 to
leave a full 5 seconds of a genuinely settled logo. Timing it from the end of
the ease instead makes the logo appear to fade the moment it arrives. The same `min` of two `smoothstep`s appears a
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

**The word is let go again.** The formation term is
`smoothstep(30., 36., t) * (1. - smoothstep(44., 51., t))`, so after eight seconds
of hold the seek force unwinds and the word comes apart before the fade takes
it. Handing a swarm packed onto thin strokes back to the pairwise force is
precisely the failure the full override above exists to avoid, run deliberately
and in reverse: at that density the short-range repulsion tears the strokes
open, and the type attractions gather what is left into cells, so the logo
dissolves into living creatures instead of drifting apart as a cloud. The
velocity-driven brightness below flares the swarm while it breaks up, so the
release answers the surge at thirty seconds without a scripted flash. It costs
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
flash by 4.5x. The cubed shape's mean is exactly 5/16, so `.35 + 2.08 * pulse`
averages to one and the flash costs no exposure; measured mean frame luminance
falls 2.5 to 3.5%, the bright peaks clipping in the dense cell cores. Writing it
as a deviation from one lets the reveal damp it to a quarter with a single
multiply, since a strobing logo hurts legibility and the speed accent below
already owns that moment.

**The reveal accents itself.** `particles.vert.glsl` scales each particle's
brightness by its own speed, read from the velocity half of the buffer that the
vertex shader otherwise never touches. For the six seconds of the migration every
particle is moving hard, so the swarm surges and then decays as it settles into
the letters. This is deliberately not a scripted flash: it costs one buffer read,
it lands exactly on the timeline without another constant to keep in step, and it
resolves on its own as the particles stop. Mean frame luminance runs 55 just
before the reveal, peaks at 85 as the word lands, and settles back to 78. While
the cluster is merely drifting it reads as a faint twinkle on the fastest
cells.

### Background and palette

A third program draws the background before the points, giving an underwater
setting:
a vertical gradient from deep navy to lit teal, a moving caustic ridge pattern,
and god rays converging on a sun point above the top edge, all built from a
handful of `sin`/`atan` terms in normalized `[0,1]` screen space instead of
any texture or 3D geometry - this costs about 280 bytes for the whole effect.
Blending is disabled while it draws, since it covers every pixel of the
viewport and would otherwise accumulate additively over whatever was left in
the back buffer from a previous frame; blending is re-enabled for the points
drawn on top. This also removes the need for `glClear` - the background
already overwrites every pixel.

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
`discard`ing the corner fragments: blending is `GL_ONE, GL_ONE`, so a black
fragment adds nothing and the image is the same without the branch.

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

PowerShell exposes stable Debug and Compo commands. Coding agents and human developers should use those same commands; builds must not depend on IDE projects, IDE state, or manually configured editor tasks.

The concrete installation and invocation details will be added to [toolchain.md](toolchain.md) as the build is implemented. Tool versions, discovery rules, expected paths, compiler flags, Crinkler options, and generated-file policy must be pinned there once validated.

## Decisions taken

- **Audio backend**: hand-written DSP in `src/audio.c`, no synthesizer library and no embedded sample. See Audio direction above.
- **Playback API**: `PlaySoundA` from `winmm.dll`, one shot, no loop and no teardown. DirectSound was not needed: nothing in the intro reads a playback position, and `PlaySoundA` is a single import against a buffer the intro already owns.
- **Generated files**: none are tracked. `out/` holds the executables, the Crinkler report and the generated shader headers; `.tools/` holds local third-party tools; `frames/` and `ocean.wav` are Debug output. All are rebuilt from source and ignored by Git.
