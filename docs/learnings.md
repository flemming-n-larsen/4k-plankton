# Learnings

What Plankton has learned about 4k intros on Windows with C, OpenGL, Shader Minifier and
Crinkler 3.0b, measured on this intro. The numbers are bytes of the Compo executable. Most
points in the first round came from the sister intro NUTS (`4k-nuts`, its
`docs/learnings.md`) and were measured again here before they were kept; NUTS and
Plankton share the toolchain, so a learning in one is worth trying in the other.

## Round one: from NUTS (October 2026)

From 4020 to 3886 bytes, 134 bytes in all, and the window now opens after 0.3 s instead
of 11.5 s. Every Debug frame compared (5, 25, 35 and 50 s, with and without minified
shaders) was bit for bit the same as before.

| Change | Bytes | Kept |
| --- | --- | --- |
| No `/TINYHEADER` (normal Crinkler header, `/COMPMODE:VERYSLOW`) | 4020 to 4000 | yes |
| `/SATURATE`, which `/TINYHEADER` had ignored | 4000 to 3998 | yes |
| Fixed uniform locations, `layout(location = N)`, no `glGetUniformLocation` | 4000 to 3959 | yes |
| `PIXELFORMATDESCRIPTOR` as an initialised `static` | 3959 to 3914 | yes |
| No error checks in Compo; the main function is the entry point and ends in `ExitProcess(0)` | 3914 to 3881 | yes |
| `LoadOpenGl` (called once) folded into the main function | 3881 to 3884 | no |
| One `PeekMessageA` a frame in Compo | 3881 to 3894 | yes, it is a fix |
| Blending never switched off: `glBlendFunc(GL_ONE, GL_SRC_ALPHA)`, the background writes alpha 0 | 3893 to 3886 | yes |

(3894 to 3893 was renaming a shader local; see below.)

## Round two: the audio code (October 2026)

From 3886 to 3774 bytes, 112 in all. The `ocean.wav` of a Debug `-Optimize` build has the
same hash before and after every step except the ones marked below, so a size change is
proved to have left the sound alone.

| Change | Bytes | Kept |
| --- | --- | --- |
| No error checks and no tick-count fallback clock in the audio start-up in Compo; the wave format is an initialised `static` | 3886 to 3813 | yes |
| `/fp:fast` | 3813 to 3830, and the sound changes | no |
| The samples in a `static` array instead of `VirtualAlloc` (uninitialised data is free) | 3813 to 3800 | yes |
| `GenerateOcean` returns nothing | 3800 to 3797 | yes |
| One `Bump` helper for the five smoothstep-up-then-down pairs | 3797 to 3790 | yes |
| No guaranteed bubble chain at the reveal (`logo_chain_started`) | 3790 to 3774 | yes; the sound changes: from 30 s the random bubbles are another draw, tones and levels the same |
| The five one-pole filters through one `Lowpass` helper | 3774 to 3774 | yes, for readability; sound identical |
| Two foam filter stages instead of three | 3774 to 3764 | no; the foam gets 5% different and brighter for 10 bytes |

## Crinkler `/TRUNCATEFLOATS` works at 16, and the number counts the whole float

`/TRUNCATEFLOATS:bits` rounds the float constants the compiler writes as `__real@...`
literals, which compresses well: 4031 bytes without it, 4017 at 20 bits, 4006 at 18,
3995 at 16, 3990 at 14, and 3977 at 12. It leaves tables (`static const float pitch[]`),
byte tables, strings and the shader text alone; this was checked by dumping the unpacked
program from memory and comparing it with the build without the option.

**The number is the bits kept of the whole float, sign and exponent included.** A float has
9 of those before its mantissa, so 12 keeps 3 mantissa bits, an error of up to 12%, and 16
keeps 7, up to 0.8%. At 12 the sound had a constant howling tone and was worse: `.99993`,
the decay of the struck tone, was rounded to exactly 1, so the tone never died, and every
literal in the filters and the clock was off by percent. A constant that is a hair from 1
cannot survive the option at any useful width, so it is written as a power of two instead
(`accent -= accent * (1.f / 16384)`, which is also 14% slower than before). The other
literals are only off by up to 0.8%, which moves the filter corners and the clock a hair
and keeps the pitches, since those sit in a table.

Judge a width by running the whole Compo build and listening: the Debug `-Optimize` build
does not truncate, so its `ocean.wav` cannot show it. Do not use a value below 16 without
listening first.

## Crinkler

- **`/TINYHEADER` was a loss on both counts.** The docs said `/TINYHEADER` and
  `/TINYIMPORT` together were worth about 100 bytes, and the long start-up was accepted as
  the price. Measured on the same object files, the normal header is 20 bytes smaller
  with `/COMPMODE:VERYSLOW` (`/TINYHEADER` ignores `/COMPMODE`, `/SATURATE` and the hash
  options), and the window opens after 0.3 s instead of 11.5 s. NUTS saw the same: 11
  bytes larger, 16 s against 0.3 s. The gain was `/TINYIMPORT` alone.
- **Measure a claim again when the program has changed.** A flag that won once can lose
  later; the 100 bytes were probably measured when the code was much smaller, or against a
  faster `/COMPMODE`.
- Time the start-up after every Crinkler change: start the executable and poll the
  process for a main window handle (the process has one thread while it unpacks).
- Small changes move the size by a few bytes either way through Crinkler's section
  ordering, even moving a function in the source. Judge a change on a full build.

## Windows

- **Take one message a frame, also in Compo.** Without it `IsHungAppWindow` reported the
  Compo window hung within seconds, and Windows may then cover the picture with a frozen
  "Not responding" copy, for example when someone at the party moves the mouse. One
  `PeekMessageA(&message, 0, 0, 0, PM_REMOVE)` without dispatching is enough (13 bytes).
- Test the Compo executable for the whole run, with a timeout longer than the run plus
  start-up, and check that it ends by itself.

## C for size with MSVC (`/O1 /Os /Oy- /Ob0`, no CRT)

- **Initialised constants instead of filling in locals**: the `PIXELFORMATDESCRIPTOR`, 45
  bytes. Mostly-zero data costs almost nothing.
- **No error checks in Compo** and no wrapper around the main function: 33 bytes. Keep the
  checks under `#ifdef DEBUG`, where they find the problems.
- Folding a function that is called once into its caller saved 8 bytes in NUTS but cost 3
  here. Measure; do not assume.
- Already in Plankton before NUTS: whole-number arguments instead of float ones
  (`Smoothstep`), and `__vectorcall` for the float helpers so the result stays in `xmm0`
  (see `docs/architecture.md`, Audio).

## OpenGL and shaders

- **Fixed uniform locations** (`layout(location = N)`): 41 bytes, more than the 24 in NUTS,
  because Plankton looked up six uniforms in three programs. The same location can be
  reused across programs, so one constant (`UNIFORM_TIME`) serves all three.
- **Blend state set once.** `GL_ONE, GL_SRC_ALPHA` lets one blend function both replace
  (source alpha 0) and add (source alpha 1), so the background pass needs no
  `glDisable`/`glEnable` around it: 7 bytes.
- **Shader Minifier does not catch shadowing.** Adding `out vec4 colour` to a shader with a
  local `vec3 colour` minified to `vec3 l ... l = vec4(...)`, which the driver rejects; the
  build itself succeeds. Look at the minified output, or run Debug with `-MinifyShaders`
  and compare frames, after renaming anything that shaders share.
- A shader without `#version` cannot use `layout(location = N)`; with `#version 430` it
  must write an `out` variable instead of `gl_FragColor`.

## Where the bytes are (3886, before round two, from the Crinkler report)

Compressed: the shaders about 1,480 (vertex 541, compute 549, background 325, sprite 67),
the audio code about 1,150 (`GenerateOcean` alone 859), the main function 429, the OpenGL
name table 97, the loader 34. The C side of the graphics is only about 560 bytes, much of
it window and context setup that every intro has. A simpler rendering engine cannot win
much more than a few tens of bytes there; the shaders and the synthesizer are where the
big savings are.

## Worth trying next

1. A simpler engine, measured: one program for both the background and the points (a
   vertex shader that tells the `glRecti` quad from the sprites, one fragment shader with
   a branch) removes one program's create, attach, link and uniform calls, but adds GLSL.
   Probably a few tens of bytes either way; only a full build can say.
2. The audio code, the largest C item: look for `(float)` of unsigned values, float
   arguments, divisions and modulo by constants and nested loops with computed indices,
   with an assembly listing (`/FAs`), as NUTS did (its synthesizer is about 600 bytes).
3. The compute and vertex shaders: repeated maths that can share one function, and tables
   that are a formula (`vec4(.07, .035, .0175, .00875)[i]` is `.07 / exp2(i)`).
