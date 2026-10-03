# Plankton for the browser

This is a browser showcase port of the native Plankton intro. It runs the
particle simulation and procedural underwater background with WebGPU, and
generates the ocean audio into a Web Audio buffer when playback starts. It has
no package manager or build step.

## Run locally

From this directory, start any local static HTTP server. For example:

```bash
python3 -m http.server 8000
```

Then open <http://localhost:8000> in a browser with WebGPU enabled. Opening
`index.html` as a `file://` URL is not supported because browsers restrict
module and shader fetches. A hosted copy needs HTTPS.

## Controls

- **Space** pauses and resumes.
- **M** toggles sound.
- **F** toggles fullscreen.
- Use **Play again** after the 59-second run to reset the particles and replay.

Balanced is the default and simulates 4,096 particles. Full uses 8,192
particles. The 16,384-particle preset matches the native count but is much
heavier: every particle checks every other particle on each simulation step.

The web version translates the native GLSL shaders to WGSL and draws each
particle as a small quad, since WebGPU does not expose OpenGL's variable point
sprites. Timing, floating-point behavior, and output are therefore close
rather than pixel-identical to the Windows executable. The web page is a
showcase; it does not meet the native 4,096-byte competition executable
format.
