# Learnings

Lessons for a 4k intro on Windows with C, OpenGL, Shader Minifier and Crinkler 3.0b, measured on Plankton. Savings are bytes of the Compo executable. The sister intro NUTS (`4k-nuts`) shares the toolchain, so a lesson in one is worth trying in the other. Measure it again before keeping it: sizes move with the program.

## Tips & Tricks

### Measuring

- Judge a change on a full Compo build. Small changes move the size by a few bytes either way through Crinkler's section ordering, even moving a function in the source.
- A claim that held once can stop holding. Measure again when the program changes.
- Prove that a size change left the sound alone: build Debug with `-Optimize` before and after, and `cmp` the two `ocean.wav` files. The Debug `-Optimize` build does not truncate floats, so it cannot show the effect of `/TRUNCATEFLOATS`; listen to the Compo build for that.
- Prove that a shader change left the picture alone: compare `-shot` captures, with and without `-MinifyShaders`.
- Check the Compo executable for the whole run, with a timeout longer than the run plus start-up, and check that it ends by itself.
- Time the start-up after any Crinkler change: start the executable and poll for a main window handle.

### Crinkler

- Use the normal header. `/TINYHEADER` was 20 bytes larger than the normal header with `/COMPMODE:VERYSLOW`, because it ignores `/COMPMODE`, `/SATURATE` and the hash options, and its slow decompressor kept the screen black for about 11.5 s against 0.3 s. The gain of the pair was `/TINYIMPORT` alone.
- `/SATURATE` saves 2 bytes with the normal header and is ignored with `/TINYHEADER`.
- `/TRUNCATEFLOATS:16` rounds the compiler's `__real@` float literals, which compresses well: 4,031 bytes without it, 4,017 at 20 bits, 4,006 at 18, 3,995 at 16, 3,990 at 14. It leaves tables (`static const float pitch[]`), byte tables, strings and shader text alone.
  - The number is the bits kept of the whole float, sign and exponent included. A float has 9 of those before the mantissa, so 12 keeps 3 mantissa bits (error up to 12%) and 16 keeps 7 (up to 0.8%).
  - At 12 the sound had a constant howl: the decay `.99993` rounded to exactly 1, so the tone never died, and every filter and clock literal was off by percent.
  - A constant a hair from 1 cannot survive the option at any useful width. Write it as a power of two: `accent -= accent * (1.f / 16384)`.
  - Do not go below 16 without listening to the Compo build first.
- `/UNSAFEIMPORT` adds nothing on top of `/TINYIMPORT`. `/ORDERTRIES` plateaus at 10000, and `/HASHTRIES` does not help.

### C and MSVC (`/O1 /Os /Oy- /Ob0`, no CRT)

- Initialised constants instead of filling in locals. A `PIXELFORMATDESCRIPTOR` and the wave format as initialised `static`s are worth 45 and a few bytes: mostly-zero data costs almost nothing.
- No error checks in Compo. No checks, no fallback clock and no wrapper around the main function: the main function is the entry point and ends in `ExitProcess(0)` (33 bytes), and the audio start-up checks and fallback clock are another 73. Keep the checks under `#ifdef DEBUG`, where they find the problems.
- Put big arrays in `static` storage, not `VirtualAlloc`: uninitialised data is free (13 bytes).
- Do not fold a once-called function into its caller by assumption. It saved 8 bytes in NUTS and cost 3 in Plankton.
- Float helpers as `__vectorcall` keep the result in `xmm0`. The default x86 convention returns floats in the x87 register, so the compiler stores and reloads each result (about 10 bytes per call) and mixes x87 and SSE arithmetic in one expression. `__vectorcall` saved 50 bytes. It also made optimised and unoptimised builds bit-identical, which is what makes the Debug `ocean.wav` a valid reference for Compo. Without it, the two differ by one least significant bit in 3 of 1.65 million samples.
- Pass small whole numbers, not floats. `Smoothstep` takes its edges as ints in half seconds: a float constant is a five-byte `push` at every call, an int under 128 is two. Doubling is exact in floating point, so the result is bit-identical (26 bytes).
- `/Oy-` and `/Ob0`. Keeping the frame pointer makes each of the audio code's many float spills one byte shorter (25 bytes), and `/Ob0` stops the inlining of small helpers (37 bytes). `/Gy` changes nothing, `/Gy /Gw` is 10 bytes worse, and `/Gr`, `/Gv` and `/Oi-` change nothing.
- Share helpers: one `Lowpass` for all five one-pole filters and one `Bump` for the smoothstep-up-then-down pairs (7 bytes).
- Avoid casts to unsigned. A float-to-unsigned cast pulls in a CRT helper that `/NODEFAULTLIB` cannot resolve. Cast through a signed `int`.
- `/arch:IA32` is a loss. x87 float-to-int conversion calls a CRT helper. With a `fisttp` stub for `_ftol2_sse` it still came out 50 bytes larger.
- `/fp:fast` was 17 bytes larger and changed the sound.
- A fourth motif statement is cheap. Reusing the oscillator and the note trigger costs about 10-30 bytes per extra statement; a new quack voice with its own resonator cost about 105.

### Windows

- Take one message a frame in Compo. Without `PeekMessageA(&message, 0, 0, 0, PM_REMOVE)` (13 bytes), `IsHungAppWindow` reports the window hung within seconds and Windows may cover the picture with a frozen "Not responding" copy, for example when someone at the party moves the mouse.
- Borderless fullscreen is `WS_POPUP` plus `WS_EX_TOPMOST` plus `ShowCursor(0)`, 9 bytes. `ChangeDisplaySettings` risks a forced mode change failing on the compo machine and costs more.
- A GDI screen grab of the fullscreen window is black. Capture the back buffer in-process (Debug `-shot`).

### OpenGL and shaders

- Fixed uniform locations (`layout(location = N)`) drop `glGetUniformLocation`, its function-table entry and the name strings: 41 bytes with six uniforms across three programs. One constant serves all programs.
- Set the blend state once. `GL_ONE, GL_SRC_ALPHA` lets one function both replace (source alpha 0) and add (source alpha 1), so the background pass needs no `glDisable` and `glEnable` and no `glClear` (7 bytes).
- Draw points with no vertex buffer: compatibility-profile `GL_POINTS` read from the SSBO by `gl_VertexID`. Enable `GL_POINT_SPRITE` or `gl_PointCoord` is zero and the screen stays black without a GL error.
- A fragment-only program runs the fixed-function vertex stage, so a fullscreen `glRecti(-1, -1, 1, 1)` needs no vertex shader.
- Shader Minifier does not catch shadowing. An `out vec4 colour` next to a local `vec3 colour` minified to a program the driver rejects, while the build succeeded. After renaming anything shaders share, read the minified output or run Debug with `-MinifyShaders` and compare frames.
- A shader without `#version` cannot use `layout(location = N)`. With `#version 430` it must write an `out` variable instead of `gl_FragColor`.
- Shader Minifier does not substitute object-like macros, so a `#define` costs its line and its full name at every use. Write literals and name them in comments, which are stripped.
- `--field-names stpq` compresses 5 bytes better than the default (`rgba` is 1 better, `xyzw` equal). `--preprocess` is a no-op, `--aggressive-inlining` and `--no-sequence` change nothing, and `--move-declarations` is 31 bytes worse.
- A single `vec4` flat array in the SSBO instead of structs: `--preserve-externals` keeps the struct and its long field names at every use site.
- Look for tables that are a formula, such as `vec4(.07, .035, .0175, .00875)[i]`, which is `.07 / exp2(i)`.

### Sound

- A sound that does not repeat needs a one-shot buffer, and then its timing must follow the sound. Make the sound the clock (`waveOutGetPosition`), and keep the simulation on a fixed step so the picture is deterministic.
- Keep the pad out of the water's way: pad pitches above the surf body and below the foam band, and a chord change no voice takes by more than a whole step.
- Saturate the ambience before adding the tonal layer, and limit the sum with a cubic soft clip instead of a second knee. Two foam stages instead of three saved 10 bytes but changed the foam's balance, so it was not kept.

## Where the bytes are

Compressed sizes from the Crinkler report at about 3,890 bytes: shaders about 1,480 (vertex 541, compute 549, background 325, sprite 67), audio code about 1,150 (`GenerateOcean` alone 859), the main function 429, the OpenGL name table 97 and the loader 34. The C side of the graphics is about 560 bytes, much of it window and context setup every intro has. The shaders and the synthesizer are where the large savings are.

## Worth trying

1. One program for both the background and the points: a vertex shader that tells the `glRecti` quad from the sprites and one fragment shader with a branch. It removes one program's create, attach, link and uniform calls but adds GLSL, probably a few tens of bytes either way.
2. The audio code, the largest C item: look for `(float)` of unsigned values, float arguments, divisions and modulo by constants, and nested loops with computed indices, using an assembly listing (`/FAs`). NUTS' synthesizer is about 600 bytes.
3. The compute and vertex shaders: repeated maths that can share one function.
