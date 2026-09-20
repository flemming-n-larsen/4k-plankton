# Credits

Plankton is by Mad Smi🤪ley / Nuts. These are the tools and ideas it is built on.

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
