#version 430

in vec3 c;

out vec4 colour;

// gl_PointCoord is only defined because the C side enables GL_POINT_SPRITE; in a
// compatibility profile it otherwise reads zero and every fragment is discarded.
// Clamping the falloff at zero rather than discarding the corner fragments is
// the same image: blending is GL_ONE, GL_ONE, so a black fragment adds nothing.
void main() {
    vec2 offset = gl_PointCoord * 2. - 1.;
    float falloff = max(1. - dot(offset, offset), 0.);
    colour = vec4(c * falloff * falloff * .2, 1.);
}
