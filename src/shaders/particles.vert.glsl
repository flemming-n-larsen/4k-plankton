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

// Six plankton hues, indexed by the type in particle.w, which particles.comp.glsl
// writes once at init as floor(hash(..) * TYPES) - the entry count here must
// match TYPES there. A cosine palette cannot reach both the cool water tones and
// the warm ones without sweeping through hues that do not belong in the water,
// so the six are written out.
const vec3 PLANKTON[6] = vec3[6](
    vec3(.10, .45, .50),  // teal - copepod water
    vec3(.14, .62, .68),  // bioluminescent cyan
    vec3(.16, .52, .30),  // sea green - green algae
    vec3(.10, .26, .62),  // deep blue - cyanobacteria
    vec3(.88, .24, .18),  // light red - red-tide dinoflagellate
    vec3(.82, .68, .20)   // faint yellow - diatom gold
);

// The camera's free-swim orbit radius. A particle on the containment shell
// (radius .9 in particles.comp.glsl) projects to .9 * focal / ORBIT in NDC
// height, so 1.5 puts the sphere just past the top and bottom edges and the
// cluster fills the frame; the screen is wider than tall, so the sides keep a
// small margin. Also the reference depth for the distance fade below, which
// keeps overall brightness where it was before the camera moved in.
#define ORBIT 1.5

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
    vec3 eye = vec3(sin(azimuth) * cos(elevation), sin(elevation), cos(azimuth) * cos(elevation)) * mix(ORBIT, 1.3, reveal);
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

    tint = PLANKTON[int(particle.w)];

    // Light absorption with distance: far particles pull toward deep-water blue,
    // the way colour drops out of real water with depth.
    tint = mix(tint, vec3(.02, .08, .15), clamp((view.z - 1.) / 2.5, 0., 1.) * .5);
    // Brighten toward white while the word is held, for legibility.
    tint = mix(tint, vec3(1.), reveal * .4);
    tint *= MasterFade(t);

    fade = exp(-.55 * (view.z - ORBIT));
}
