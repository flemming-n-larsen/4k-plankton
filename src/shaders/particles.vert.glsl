#version 430

uniform float t;
uniform vec2 r;

struct Particle {
    vec4 position;
    vec4 velocity;
};

layout(std430, binding = 0) buffer Particles {
    Particle particles[];
};

out vec3 tint;
out float fade;

// Must match particles.comp.glsl's timeline exactly, so the camera settles
// into its front-on view over the same seconds the particles take to spell
// the word out.
#define REVEAL_START 30.
#define REVEAL_EASE 6.

float RevealBlend(float time) {
    return smoothstep(REVEAL_START, REVEAL_START + REVEAL_EASE, time);
}

// Fades in from black slowly, so the procedural background (depth gradient,
// caustics, god rays) reads clearly before the particles compete for
// attention, then fades back to black a few seconds after NUTS is fully
// formed, ending the intro on the held word rather than an abrupt cut. Must
// match the copy in background.frag.glsl.
#define FADE_IN_END 8.
#define FADE_OUT_START (REVEAL_START + REVEAL_EASE + 5.)
#define FADE_OUT_DURATION 4.

float MasterFade(float time) {
    return min(smoothstep(0., FADE_IN_END, time),
               1. - smoothstep(FADE_OUT_START, FADE_OUT_START + FADE_OUT_DURATION, time));
}

void main() {
    vec4 particle = particles[gl_VertexID].position;

    float reveal = RevealBlend(t);
    float azimuth = mix(t * .13, 0., reveal);
    float elevation = mix(sin(t * .07) * .5, 0., reveal);
    // Dollies in from the free-swim orbit radius to 1.3 as the word forms:
    // at that distance and the .8 word scale in particles.comp.glsl, the word
    // fills the frame horizontally (its 4-letter aspect ratio is much wider
    // than the screen's, so it is always width-bound, with room left above
    // and below).
    vec3 eye = vec3(sin(azimuth) * cos(elevation), sin(elevation), cos(azimuth) * cos(elevation)) * mix(2.0, 1.3, reveal);
    vec3 forward = normalize(-eye);
    vec3 right = normalize(cross(forward, vec3(0., 1., 0.)));
    vec3 up = cross(right, forward);

    vec3 offset = particle.xyz - eye;
    vec3 view = vec3(dot(offset, right), dot(offset, up), dot(offset, forward));

    float focal = 1.7;
    gl_Position = vec4(view.x * focal * r.y / r.x, view.y * focal, view.z * .01, view.z);
    // Scaled by viewport height, not a pixel constant, so 1080p and 720p match.
    // The upper clamp stops a particle drifting near the camera from filling the
    // screen with one sprite.
    gl_PointSize = clamp(.022 * focal * r.y / view.z, 1., 18.);

    // Cosine palette (Quilez form: a + b*cos(2pi(c*x+d))) tuned to stay in the
    // teal/cyan/blue/green range: the red channel's amplitude is kept small so no
    // phase swings into warm hues. The 6. matches TYPES in particles.comp.glsl.
    float hue = particle.w / 6.;
    tint = vec3(.15, .45, .55) + vec3(.10, .35, .35) * cos(6.2831 * (vec3(.3, .5, .5) * hue + vec3(.1, .2, .35)));

    // Light absorption with distance: far particles pull toward deep-water blue,
    // the way colour drops out of real water with depth.
    tint = mix(tint, vec3(.02, .08, .15), clamp((view.z - 1.) / 2.5, 0., 1.) * .5);
    // Brighten toward white while the word is held, for legibility.
    tint = mix(tint, vec3(1.), reveal * .4);
    tint *= MasterFade(t);

    fade = exp(-.55 * (view.z - 2.));
}
