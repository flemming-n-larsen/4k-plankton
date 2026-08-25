#version 430

in vec3 tint;
in float fade;

out vec4 colour;

// gl_PointCoord is only defined because the C side enables GL_POINT_SPRITE; in a
// compatibility profile it otherwise reads zero and every fragment is discarded.
void main() {
    vec2 offset = gl_PointCoord * 2. - 1.;
    float falloff = 1. - dot(offset, offset);
    if (falloff < 0.) discard;
    colour = vec4(tint * fade * falloff * falloff * .2, 1.);
}
