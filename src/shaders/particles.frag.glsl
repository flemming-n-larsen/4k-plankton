#version 430

in vec3 c;

out vec4 colour;

// gl_PointCoord is only defined because the C side enables GL_POINT_SPRITE; in a
// compatibility profile it otherwise reads zero and every fragment is discarded.
// Clamping the falloff at zero rather than discarding the corner fragments is
// the same image: blending is GL_ONE, GL_ONE, so a black fragment adds nothing.
//
// A tight core over a faint wide halo, rather than one soft blob. A plain
// squared falloff is still at half its peak 54% of the way out, so sixteen
// thousand of them read as fog; the eighth power reaches half peak at 29%, and
// carries most of the light. The two weights hold the total emitted light about
// where the single term had it, so the exposure of the scene does not jump:
// radially, the squared term integrates to 1/6 and the eighth to 1/18, so
// .04/6 + .5/18 lands just above the .2/6 it replaces.
void main() {
    vec2 offset = gl_PointCoord * 2. - 1.;
    float falloff = max(1. - dot(offset, offset), 0.);
    float halo = falloff * falloff;
    float core = halo * halo;
    core *= core;
    colour = vec4(c * (halo * .04 + core * .44), 1.);
}
