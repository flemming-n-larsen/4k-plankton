# Architecture

How Plankton, a PC 4k intro, is built, and why. Where an alternative does not work, the reason is kept next to the decision. Size and build lessons are in [learnings.md](learnings.md); tools and options are in [toolchain.md](toolchain.md).

## Target

A Win32/x86 executable of at most 4,096 bytes for the [TRSAC 4 KB compo](https://trsac.dk/realtime-compos/#compo4). Win32 and [Crinkler](https://github.com/runestubbe/Crinkler) are the usual 4k platform ([in4k wiki](https://in4k.github.io/wiki/win32)). The compo rules and machine are authoritative.

| | |
|---|---|
| Size limit | 4 KB, one `.exe` |
| Compo PC 1 (baseline) | Ryzen 9950X3D, RTX 5070 Ti, 32 GB, latest NVIDIA drivers |
| Compo PC 2 | i7-11700, RTX 3070, 32 GB, latest NVIDIA drivers |
| Resolution | at least a 1920x1080 and a 1280x720 `.exe` |
| Runtime | 5 minutes at most |
| Submission | includes an information file (`release/plankton.nfo`) |

Two rules shape the code:

- Every screen-space quantity derives from the viewport, never from pixel constants, so the intro renders correctly at both resolutions.
- Both machines must show the same thing, so the simulation advances by a fixed step driven by the sound's playback position, not by frame count or a wall clock.

The entry is three executables built by `build.ps1 -Release`:

- `plankton.exe` opens a borderless window at the current desktop resolution.
- `plankton_1920x1080.exe` and `plankton_1280x720.exe` fix the size at build time (`COMPO_WIDTH`, `COMPO_HEIGHT`). They open a borderless window of that size in the top-left corner, and are about 24 bytes smaller because the size is a constant.

None of them changes the display mode, which avoids a forced mode switch failing on the compo machine.

## Structure

```text
intro logic
    ├── platform layer    window, input, timing, process lifecycle   (src/intro.c)
    ├── graphics layer    OpenGL and shader execution                (src/intro.c, src/shaders/)
    ├── audio             synthesis, playback, clock                 (src/audio.c, src/audio.h)
    └── generated data    minified shaders

build and packaging       build.ps1: compile, minify, link, pack
```

The boundaries are for development. The Compo build may flatten them when that is smaller. No external framework is used: the platform layer is under a hundred lines, so a framework would add a dependency and a build step for a problem the project does not have. CMake and IDE projects are not used either (see [toolchain.md](toolchain.md)).

GLSL stays readable under `src/shaders/` for Debug and is minified into C strings for Compo. Runtime dependencies are the Windows system libraries, the OpenGL driver and `winmm.dll`.

## Build variants

- Debug: a normal window with shader and GL error reporting, frame capture, video recording and timing logs (see Debug tooling). `-MinifyShaders` compiles the same shader text as Compo.
- Compo: borderless, size-optimised, linked with Crinkler, and the build fails above 4,096 bytes.

Debug is what a change is measured against: it renders identical frames, writes the generated `ocean.wav`, and captures a reproducible frame at an exact simulation step.

## Graphics

OpenGL 4.3+ compute shaders with shader storage buffers, through a plain `wglCreateContext`. On Windows that call already returns the driver's maximum compatibility profile (4.6 on the compo machines), so `glDispatchCompute` and SSBOs resolve through `wglGetProcAddress` with no `wglCreateContextAttribsARB`. Both compo machines are recent NVIDIA cards, so ping-ponging state through float textures would only cost bytes and speed.

The compatibility profile also allows drawing `GL_POINTS` with no vertex array and no vertex buffer, reading positions from the SSBO by `gl_VertexID`. That is the largest saving in the renderer.

`gl_PointCoord` is undefined in a compatibility profile unless `GL_POINT_SPRITE` is enabled. Without it the sprite falloff test fails everywhere and the screen stays black with no GL error.

The browser port in [web/](../web/) uses WebGPU and quads instead of point sprites. It follows the same timeline and is close to, not identical to, the native output. It is not part of the executable.

## Platform layer

- The window uses the predefined `"static"` class, which already has a window procedure. No class registration, class name, instance handle or window procedure of our own.
- The intro never acts on a message, so Compo takes one per frame with a single `PeekMessageA` and dispatches nothing. That one message is required: a window that takes none for five seconds is marked hung, and Windows may cover it with a frozen "Not responding" copy. The close button still does nothing; Escape or the end of the run exits.
- The Compo window is a borderless `WS_POPUP` the size of the screen with `WS_EX_TOPMOST`, and `ShowCursor(0)` hides the cursor. A GDI screen grab of it returns black, so it cannot be used to check the picture. Debug stays an ordinary window; to see it as Compo does, pass `-width<w> -height<h>` at the desktop size.
- No teardown: `ExitProcess` releases the context, device context and sound.
- Compo checks no errors during setup. Without a window or a 4.3 context there is nothing to show. Debug keeps the checks and exit codes. In Compo the main function is the entry point itself and ends in `ExitProcess(0)`.
- The framebuffer size and resolution uniforms are resolved once before the loop. A popup's client area is the whole window, so the screen metrics are also the framebuffer size. There is no `glViewport` call.
- No uniform is looked up by name. Every shader fixes its locations with `layout(location = N)`: `t` is 0 in all three programs, `d` (compute) and `r` (vertex and background) are 1. Fixed locations need `#version 430` and an `out vec4 colour` in place of `gl_FragColor`.
- The compo build exits on its own: `END_SECONDS` in `src/intro.c` (65) ends the loop in the black after the fade. It is compiled out of Debug, whose diagnostics keep running past it.

## Simulation and rendering

16,384 particles of 6 types live in one SSBO as a flat `vec4` array: `v[n*2]` is position (`.w` is the type) and `v[n*2+1]` is velocity. A flat array rather than structs, because Shader Minifier runs with `--preserve-externals` and would keep a struct and its field names at every use site. Each frame a compute shader applies the standard Particle Life force over all pairs, and the points are drawn from the same buffer.

- No data is embedded. The attraction matrix and the initial positions come from a hash in the shader. The seed constant in `hash()` selects a different, reproducible attractor. Nothing is seeded from the clock.
- Shader constants are literals, not `#define`s. Shader Minifier does not substitute object-like macros, so a `#define` costs its own line plus its full name at every use. Names go in comments, which are stripped.
- Fixed timestep. `Run()` follows the sound's time but integrates in constant `SIM_STEP` (1/60 s) increments, and every uniform derives from `sim_time`. A real frame delta would make the chaotic simulation differ visibly between 60 and 144 Hz. The step count is an integer and `sim_time` is derived from it, because accumulating by addition drifts. `MAX_CATCHUP` bounds the catch-up loop, so a slow machine plays the same intro slowly instead of a different one. `-shot` (synthetic rate) and `-live` (wall clock) captures at the same time index are byte-identical.
- A type attracts itself. With a purely random matrix, 3D Particle Life settles into a uniform gas; a positive diagonal is what forms distinct cells.
- The world does not wrap. A soft central containment force, applied once per particle outside the pair loop, holds the swarm. A periodic box would put a minimum-image test in the O(n²) loop and pops at the faces. A box-shaped volume would show flat faces and straight edges sweeping across the frame, which reads as a tank.
- Tight core over a faint halo. The sprite falloff is an eighth-power core plus a squared halo. A squared falloff alone is at half peak 54% of the way out, so thousands of sprites read as fog; the eighth power reaches half at 29%. The weights keep the total light equal, so exposure is unchanged. Shrinking sprites instead would bring back the half-empty sea.
- Sprite size scales with viewport height, including its upper clamp, so 1080p, 720p and downscaled captures render the same image. An absolute pixel clamp capped every sprite at the compo resolutions, because the camera is inside the swarm and every sprite is near.
- The camera swims inside the cluster on a path of radius .5, inside the .9 containment shell. From outside, the cluster projects as a silhouette and the corners stay empty at any distance. From inside, every ray exits through the swarm. It also keeps the whole cluster within about 1.4, so depth absorption barely engages and colours stay saturated. A smaller radius leaves too little parallax. The same value is the reference depth of the per-particle distance fade.

## Timeline

The run is 65 seconds: an ascent, the word NUTS, then a rubber duck, each dissolving back into cells. The same windows appear in several files and must be kept in step by hand: `particles.comp.glsl`, `particles.vert.glsl`, `background.frag.glsl`, `audio.c` and `intro.c`.

| Seconds | Event |
|---|---|
| 0-5 | fade in from black |
| 0-10 | dive: the camera starts 1.8 further out and swims in |
| 4-40 | ascent: the water brightens and the plankton recover colour |
| 17-21 | wave of light; the light dips around 17.9 first |
| 21-30 | glow after the wave fades |
| 30-36 | camera pulls back from .5 to 1.42 |
| 33-37 | letters lock one at a time, N first and S last |
| 37 | ring of light crosses the held word |
| 44-51 | the word is let go |
| 45-48 | the duck forms |
| 53-57 | the duck is let go |
| 54-62 | fade to black |
| 65 | the Compo build exits |

### The ascent

The free swim is most of the run and does not develop on its own: a constant orbit reads as a holding pattern. The ascent is what develops. The camera does not move; moving the eye up would drag the bounded swarm off centre and there is nothing above it to rise past. Light and bubbles carry the motion, which is why the bubbles are not decoration.

In `background.frag.glsl` every lit term already scales with screen height, so the ascent folds into that one factor and drives the gradient, caustics and rays together. `particles.vert.glsl` has its own copy that eases off the distance absorption.

### The dive, the wave and the pulse

- Dive. The camera starts 1.8 units further out, so the run opens on the whole cloud hanging in the dark. At 1 unit further the swarm still fills the frame and the dive reads as colonies growing; from about 2.3 it sits inside the frame as a ball of light.
- Wave of light. From 17 s a shell leaves the camera and sweeps outward (`length(offset) - (t - 17.) * .5`, brightened by `1. + 6. * exp(-30. * shell * shell)`). It is centred on the camera, not the swarm: a shell around the swarm only lit the colonies on the camera's slice and read as scattered flashes. It needs no gate, since its radius is negative before 17 s and past the swarm after 21 s.
- Calm before, a mark after. The species flashes are held to a fifth of their strength until the wave (`awake`), the light dips by 60% around 17.9 s, a glow of +60% fades over the next nine seconds, and the wave lowers the depth absorption by .3 for the rest of the run so the colours stay stronger. The audio answers with a hush before the wave and a swell on it.
- Species flash. Each particle's colour is multiplied by a flash keyed to its species, `.5 + .5 * sin(t * 1.2566 + type * 1.0472)` cubed, so the six species light in turn over a five-second period. Keying to the species rather than the particle index is what makes it read as bioluminescence: the pairwise force sorts each species into a coherent cell, so whole regions light in sequence. The cube matters: a plain sine sits near its middle and the six phases cancel, so the eye sees a reshuffle instead of a pulse. The cubed shape's mean is 5/16, so `.5 + 1.6 * pulse` averages one and costs no exposure. The reveal damps it to a quarter with one multiply, since a strobing logo hurts legibility.

### The word NUTS

The word is a vector font of lines and arcs, not a bitmap: N is three segments, U two segments and a half circle, T two segments, S two 3/4 circles rotated 30° so the arcs do not meet tangent at one pinch point. Each particle hashes its own index to a point on a stroke, so it always seeks the same spot. The argument stays small enough that GPU `sin()` range reduction stays precise and distinct indices do not alias. A perpendicular jitter of ±.05 gives the strokes width so the counters of N, U and S close. Each stroke is a round tube, deepest along its middle, so the word has depth.

- The seek force replaces the pairwise force during the hold. Blending in a residual of the pairwise force amplifies into clumps on the densely packed strokes.
- The gather precedes the word. The seek term ramps in per particle, `smoothstep(33., 35., t - (target.x + 1.3) * .7)`. The ramp is delayed by where in the word the particle is going, so the letters lock one after another (about 35.3, 35.8, 36.2 and 36.9 s) and the creatures are drawn in from the left while the right is still swimming. A plain `smoothstep(30., 36., t)` would steer every particle toward its own target long before anything looked arrived.
- The spring is stiff. The seek force is `(target - position) * k` against velocity damping `exp(-17 * d)` per step. At `k = 9` the system is heavily overdamped, with a time constant of 1.8 s, so the letters would still be soft at 40 s. At `k = 60` the roots are -5 and -12 per second and the word is sharp about a second after the ease ends. The speed brightness coefficient is .5 so the arrival does not burn out.
- Camera. It pulls back from .5 to 1.42 over 30-36 s, so the word is legible and arrives as a retreat from the plankton. The distance is set by the word's widest point, the N's left vertical at -1.30: at 1.3 it projects to 96% of the half-width with no room to move, and at 1.42 it keeps about 60 px of margin on the left at 1280x720 in the worst frame of the hold. It is not recoupled to the narrower letter window, because compressing the retreat into the snap would stack a whip-zoom on it. It is not unwound for the release either: the free-swim azimuth is absolute (`t * .13`), so unwinding would swing the shot through six radians in seven seconds.
- The camera keeps moving during the hold. A small oscillation of about .045 rad fades in as the free-swim orbit fades out, so the word keeps turning through the hold and the release. It must swing around zero, not be a fraction of the free-swim term: 4% of the absolute azimuth would put the camera 10.7° off axis and skew the logo. At .045 the letters sit at depths 1.30 to 1.42, so the nearer N draws slightly larger and the word has some dimension.
- Speed accent. `particles.vert.glsl` scales each particle's brightness by its own speed, read from the velocity half of the buffer. During the migration the swarm surges and decays as it settles, with no scripted flash and no extra timeline constant.
- Ring. At 37 s, `length(offset) - 1.36 - (t - 37.) * .6` sweeps across the word from the middle out, a flash as the last letter lands. At amplitude 4 it burnt the letters to white; .7 reads as a flash without losing the letterforms.
- Release. The formation term is `smoothstep(30., 36., t) * (1. - smoothstep(44., 51., t))`. Handing a swarm packed onto thin strokes back to the pairwise force tears the strokes open, and the type attractions gather what is left into cells, so the logo dissolves into creatures. Scaling the letter targets outward instead does not work: the override stays in charge, so the word keeps its shape and grows past the frame, which reads as a push-in.

### The rubber duck

The TRSAC mascot forms as the word comes apart. `DuckTarget` in `particles.comp.glsl` gives each taking-part particle a spot on a duck seen from the side, facing left. The seek force draws it in over 45-48 s and lets it go over 53-57 s, so it dissolves into cells the way the word does.

It is a filled silhouette, not an outline: a few thousand points read as a duck when they cover it. The gold species (about 2,700 particles) fills the body, head and tail as ellipsoids, so the duck has volume in depth. A sixth of the red species forms the beak, a triangle. A sixteenth of the cyan species is a ring tight enough to burn out to white for the eye; the gold keeps clear of it, which leaves a dark pupil.

While the duck forms its gold glows brighter and every particle that is not part of it fades back, so passing cells do not cover it. The test for who takes part is written twice, in the compute and the vertex shader, and must match.

A third, finer bubble layer in the background shader shows only while the duck is held, strongest near the screen centre, so the duck swims in a cloud of small bubbles.

### Fades

- The scene fades in over 5 s and out over 54-62 s, long enough after the word and duck have come apart that the intro ends on the swarm. The same `min` of two `smoothstep`s appears in `particles.vert.glsl` and `background.frag.glsl`, folded multiplicatively into the colours instead of drawn as an overlay quad.
- The fade-in is short because the ascent opens on the darkest frame and the venue is a projector in a blacked-out hall. A projector's black floor and a gamma nearer 2.5 take roughly a fifth of the midtones; an 8 s fade left the frame at 2 s mostly below code value 16. The deep-water look survives, because bright points against black carry it. The floor of the ascent is not raised to compensate: that would spend the contrast the arc is built on.

## Background and palette

A third program draws the background before the points: a depth gradient, moving caustics and god rays converging on a sun above the top edge, built from a few `sin`/`atan` terms in normalised screen space with no textures or geometry.

- Blending is set once. The blend function is `GL_ONE, GL_SRC_ALPHA`: the background writes alpha 0 and replaces the destination, the points write alpha 1 and add. No `glClear`, and no blend switching per frame.
- No vertex stage. A fragment-only program runs the fixed-function vertex pipeline, which is what a pass-through would do. The geometry is one `glRecti(-1, -1, 1, 1)`.
- God rays use a single angular frequency whose amplitude is modulated by a much slower one. A single frequency gives nine identical shafts swaying like a cog. A second fan is worse, because where the two cancel they open dark wedges that look like a seam. Modulating the amplitude varies the shafts and drifts the variation along the fan without any shaft disappearing.
- Bubbles are two parallax layers of a hashed cell grid scrolling upward, on an aspect-corrected copy of the coordinate (otherwise every bubble is a 16:9 ellipse). Both layers scroll by the same rate, so the coarse layer drifts slower in screen units because its cells are larger. Screen space is valid because the camera never rolls. Each bubble is an annulus: a filled blob reads as dust, a bright rim around a dark middle reads as a bubble catching light. Only cells hashing above a threshold carry one, and a second hash decorrelates height from size. Density is the design problem: too many compete with the plankton.
- Tone constraints. Colonies must not burn out to white and lose detail, and the god rays and bubbles must not compete with them. The weights are tuned so about 2% of pixels reach a channel of 250 or more.
- No white-out for legibility. Particles keep their colours through the reveal. Mixing toward white reads as a white logo, not a logo made of creatures.
- Palette. A six-entry table `PLANKTON` in `particles.vert.glsl`, indexed by type: copepod teal, bioluminescent cyan, algal sea green, cyanobacterial deep blue, red-tide red and diatom gold. The count is coupled to the species count in `particles.comp.glsl`. A cosine gradient would be smaller but cannot reach both cool water tones and warm ones without sweeping through hues that do not belong underwater. Distant particles are pulled toward blue-black like real absorption, which keeps the warm entries from looking foreign at the back.
- Fades are folded into the colour in the vertex shader instead of a second varying: a point sprite has one vertex, so one interpolated colour is what colour and fade would have multiplied to. The fragment shader clamps the falloff at zero rather than `discard`ing, since a black fragment adds nothing.

## Audio

The audio is hand-written DSP in `src/audio.c`, behind `src/audio.h`: a synthesised ocean ambience with a tonal layer that gives the run a key but no beat. There is no synthesizer library, sequencer or embedded sample.

`AudioStart()` synthesises the whole run into one buffer of raw 16-bit samples and plays it through `waveOut` once, without looping. There is no stop call, because exiting the process stops playback. `mmsystem.h` is excluded by `WIN32_LEAN_AND_MEAN`, so the four `waveOut` functions and their structures are declared by hand. The buffer is longer than the run: audio plays in real time while the picture follows `sim_time`, and a machine that cannot hold 60 Hz stretches the picture against the sound, so the margin keeps the sound from ending first.

The sound is the clock. `AudioSamples()` returns the samples played so far (`waveOutGetPosition` in `TIME_SAMPLES`), and `Run()` takes its time as that count over the sample rate. The picture starts when the sound starts and stays locked to it. Two clocks started milliseconds apart can be pulled apart, and with a slow device the sound was once heard seconds behind the picture. The position is fine enough to drive the simulation: it changes about every 0.1 ms, the first value arrives about 18 ms after the write, and it advances 1.0000 s per wall-clock second. A machine that cannot compute 60 steps a second still falls behind the sound, because the simulation cannot skip steps and stay deterministic. Debug falls back to `GetTickCount` when there is no device; Compo has no fallback and no start-up error checks.

Generation is seeded by the constant `AUDIO_SEED`, never the clock, so the sound is identical on every run. It writes samples straight into the player's block with no scratch buffer. Only Debug adds a RIFF header, when it writes `ocean.wav`. Everything is pure integer and float arithmetic with no CRT. Float-to-integer conversions cast through a signed `int`, because a cast to `unsigned` pulls in a CRT helper that `/NODEFAULTLIB` cannot resolve.

### Ambience

- Surf: white noise through a two-stage one-pole low-pass. Most of the weight is here, with about 61% of the energy below 200 Hz and 38% between 200 Hz and 1 kHz.
- Foam: differenced noise (a 6 dB/octave rise) taken down by *three* one-pole stages. One stage only cancels the rise and leaves bright hiss on top of the water; three fall 18 dB/octave and give a darker underwater band.
- Swells: the loudness eases between random waypoints five seconds apart (the shared `Waypoint` helper, smoothstep interpolation). Two seconds is too fast: a wave arrives before the ear has read the last one. A second waypoint nine seconds apart eases the foam amount. Nine is not a multiple of five, so the two envelopes drift against each other.
- Foam gate floor: `.2 + .8 * shaped * shaped`. The squared swell alone spans a 94x range and switches the foam off for seconds at a time, which sounds washy. With the floor the quiet extreme is 26% of the median instead of 4%.
- Saturation: the ambience goes through a soft knee `x / (1 + |x|)`, not a hard clamp, *before* the tonal layer is added. Folding a sustained low pad into the same knee would let it dominate and duck the water on every swell.
- Macro envelope: it follows the picture: quiet and close in the deep, opening across the ascent, a swell under the wave of light, a surge as the word snaps together, settling over the hold, and fading with the image. The surge opens the foam further than the level alone, so the reveal is a wash of white water. Measured per-second RMS relative to the loudest second: about .04 in the deep opening, .24 by 12 s, .65 by 19 s, 1.0 at 35-36 s and above .9 through 40 s.
- Bubbles: sine sweeps from 80-140 Hz rising to four times that over 62-100 ms, with a rounded attack and a diminishing tail, in cascades with gaps of 1.5-2.5 ms. Open-water cascades come every 1.2-3.4 s and are more frequent around the release of the word and the duck (44-53 s).

### Tonal layer

- Oscillator: a magic-circle oscillator, `y += k * z; z -= k * y` with `k = 2π f / SAMPLE_RATE`. It is a symplectic rotation, so amplitude neither decays nor grows over the run. It needs no table and no `sinf` (which `/NODEFAULTLIB` lacks). Its output starts at zero, so a voice enters silently, and `k` can change at any sample without disturbing the phase, which makes chord changes click-free. The small-angle `k` puts the true frequency out by `k²/24`: 0.002% at the pad pitches, 0.12% at the accent.
- Pad: three sustained voices play Dm, Dm/F, B♭, F, Dm, changing at 6, 18, 24, 30 and 38 s, so the harmony brightens across the ascent and settles under the held word. The voices sit in D3-D4, above the surf body and below the foam band, so the chord occupies a gap in the ambience instead of fighting it. No voice moves more than a whole step at a change, so an instantaneous pitch step reads as a chord change. Six pitches serve fifteen chord slots, so chords are byte indices into a pitch table. The pad rides the macro envelope.
- Sub: a D2 enters between 32 and 35 s and leaves over 43-53 s, so the reveal has weight on a hall system.
- Motif: four struck tones, D5, F5, A5 and D6 (an arpeggio of the key), every .55 s. Each decays by a repeated multiply, an exponential with a 0.65 s time constant and no `expf`. It is heard four times:
  - as a question that stops on F5, softly at 12 s and louder at 18.6 s under the wave of light;
  - answered, resolving on D6, at 35.2 s as the four letters lock;
  - as a softer downward run, D6, A5, F5, D5, from 47 s as the duck forms, so it differs from the climbing answer and comes home on the low D. It plays over Dm, so all four tones belong to the chord.
- Limiter: the finished sum goes through a cubic soft clip, `x - x³/3`. It is nearly linear for small amplitudes, so the pad and sub (pure sines) get far less added distortion than from a second `x / (1 + |x|)`. Measured non-linear content over three stretches fell from 5.5%, 8.9% and 13.4% to 0.7%, 2.5% and 8.4%. The clip changes sign at 1.73, so the sum must stay below that. The sound is fixed by the seed, so this was checked once (the sum peaks at about 1.12) instead of guarded with a clamp.

### Why no synthesizer

The tonal layer was added to the hand-written synthesis instead of replacing it. [Sointu](https://github.com/vsariola/sointu), [4klang](https://github.com/gopher-atz/4klang), [Oidos](https://github.com/askeksa/Oidos) and shader-based audio ([Lev4k](https://github.com/TheNuSan/Lev4k)) are the options if the intro ever needs a sequenced, multi-instrument score, which the byte budget does not justify. The decision would rest on compressed size, sound, CPU/GPU cost, sync, workflow and reproducibility.

### Playback API

`waveOut` from `winmm.dll` (open, prepare, write, get position), one shot. `PlaySoundA` cannot report how far the sound has played, and DirectSound is unnecessary because `waveOutGetPosition` gives the sample position directly.

## Debug tooling

Compo compiles all of it out. Shader logs and the GL version go to the debugger and to `in4k.log`. Options (details in the [README](../README.md)):

- `-shot<frames>`: fixed 60 Hz step, capture at that frame, reproducible.
- `-live<seconds>`: real timing path, capture after that many seconds.
- `-width<w> -height<h>`: exact client size, so both required resolutions can be compared without changing display mode.
- `-steps<k>`: k simulation steps per rendered frame. Rendering never feeds back into the simulation, so the output is byte-identical and a late state is reached quickly.
- `-record<fps>`: capture every frame of the run.
- A timing log, printed to the starting console and `in4k.log`: when the samples were written, when the first frame was presented, when the sound device began playing, and every five seconds the wall clock, sound position and sim time. `sim_minus_audio` stays a little negative (one step plus draw time, 20-30 ms). Crinkler's unpack time is not included; it happens before `Run()`.

Capture reads the back buffer in-process. Capturing from the screen is unreliable: a fullscreen window that lost foreground returns a stale frame that looks like a frozen simulation.

## Generated files

None are tracked. `out/` holds executables, the Crinkler report and generated shader headers; `.tools/` holds local third-party tools; `frames/`, `ocean.wav` and `shot.bmp` are Debug output. All are rebuilt from source and ignored by Git.
