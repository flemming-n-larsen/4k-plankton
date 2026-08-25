#version 430

// Must match PARTICLE_COUNT in src/intro.c.
#define N 16384
#define TYPES 6.
#define RMAX .11
#define BETA .6
// Change SEED to pick a different, fully reproducible attractor.
#define SEED 3.

// The formation timeline: the cluster is the main effect, and the particles
// only ease into the word NUTS once, at REVEAL_START seconds in, over
// REVEAL_EASE seconds, staying formed for the rest of the run. REVEAL_START
// and REVEAL_EASE must match the copies in particles.vert.glsl, which eases
// the camera to a full-frame front-on view over the same seconds.
#define REVEAL_START 30.
#define REVEAL_EASE 6.

layout(local_size_x = 256) in;

// d == 0 initializes the buffer, d > 0 integrates velocity, d < 0 integrates
// position by -d. Splitting the two integrations avoids reading a neighbour
// position that another workgroup has already advanced.
uniform float d;
uniform float t;

struct Particle {
    vec4 position;
    vec4 velocity;
};

layout(std430, binding = 0) buffer Particles {
    Particle particles[];
};

float hash(float n) {
    return fract(sin(n + SEED * 17.13) * 43758.5453);
}

// Off-diagonal pairs get a signed random weight; a type always attracts itself,
// which is what makes cohesive cells form instead of a uniform gas.
float attraction(float a, float b) {
    float weight = hash(a * 7.3 + b * 13.7 + 3.1) * 2. - 1.;
    return a == b ? .5 + .5 * abs(weight) : weight;
}

// Eases from 0 (free swimming) to 1 (fully formed word) once, at REVEAL_START,
// and stays at 1 for the rest of the run.
float RevealBlend(float time) {
    return smoothstep(REVEAL_START, REVEAL_START + REVEAL_EASE, time);
}

#define FONT_STROKES 10
// A point on stroke index'th centerline, at fraction u along it, offset by
// jitter (perpendicular for a straight stroke, radial for an arc) so a swarm
// of points reads as a stroke with real width rather than a bare wire. NUTS
// is drawn as N (3 line segments), U (2 segments and a half-circle), T (2
// segments), and S (two 3/4-circles) - no embedded glyph image, just these
// closed-form lines and arcs.
vec2 StrokePoint(int stroke, float u, float jitter) {
    if (stroke == 0) return vec2(jitter, mix(0., 1., u));                                       // N: left vertical
    if (stroke == 1) return vec2(.6 + jitter, mix(0., 1., u));                                  // N: right vertical
    if (stroke == 2) {
        vec2 a = vec2(0., 1.), b = vec2(.6, 0.), dir = normalize(b - a);
        return mix(a, b, u) + vec2(-dir.y, dir.x) * jitter;                                     // N: diagonal
    }
    if (stroke == 3) return vec2(.85 + jitter, mix(.35, 1., u));                                // U: left vertical
    if (stroke == 4) return vec2(1.45 + jitter, mix(.35, 1., u));                                // U: right vertical
    if (stroke == 5) {
        float a = mix(3.14159, 6.28319, u);
        return vec2(1.15, .35) + (.3 + jitter) * vec2(cos(a), sin(a));                          // U: bottom half-circle
    }
    if (stroke == 6) return vec2(mix(1.7, 2.3, u), 1. + jitter);                                // T: top bar
    if (stroke == 7) return vec2(2. + jitter, mix(0., 1., u));                                  // T: stem
    // Each S arc sweeps 3/4 of a circle rather than half, so the terminals
    // curl back into a hook past vertical instead of ending flat. Both are
    // also rotated 30 degrees the same way: with no rotation, the two circles
    // are exactly tangent on the vertical centerline, so every particle near
    // the middle converges on that single point from four symmetric
    // directions and pinches into a bowtie instead of a flowing diagonal.
    if (stroke == 8) {
        float a = mix(.7854, 5.4978, u) - .5236;
        return vec2(2.85, .75) + (.25 + jitter) * vec2(cos(a), sin(a));                         // S: upper 3/4-circle, bulging left
    }
    float a = mix(-2.3562, 2.3562, u) - .5236;
    return vec2(2.85, .25) + (.25 + jitter) * vec2(cos(a), sin(a));                              // S: lower 3/4-circle, bulging right
}

// World-space target for particle index i: picks one of the 10 strokes and a
// point along it, so the same particle always seeks the same spot. The word
// is centered and scaled from the local font units StrokePoint works in.
// Coefficients kept small enough that i's full range stays under the argument
// magnitude where sin()'s range reduction is still precise on the GPU - a
// larger multiplier here once aliased into visible clumps instead of a smooth
// spread along each stroke, since nearby particle indices no longer hashed to
// distinct values.
vec3 LetterTarget(float i) {
    int stroke = int(hash(i * 1.3 + 77.) * float(FONT_STROKES));
    vec2 local = StrokePoint(stroke, hash(i * 2.9 + 12.), (hash(i * 1.9 + 41.) - .5) * .05);
    // .8: scaled up together with the camera dolly-in in particles.vert.glsl
    // so the finished word fills as much of the frame as its wide, 4-letter
    // aspect ratio allows without clipping horizontally.
    return vec3((local.x - 1.575) * .8, (local.y - .5) * .8, 0.);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    vec4 self = particles[i].position;

    if (d == 0.) {
        float n = float(i);
        vec3 direction = normalize(vec3(hash(n * .37), hash(n * .71 + 1.), hash(n * 1.13 + 2.)) - .5);
        particles[i].position = vec4(direction * pow(hash(n * 2.3 + 5.), .3333) * .9,
                                     floor(hash(n * 3.7 + 9.) * TYPES));
        particles[i].velocity = vec4(0.);
        return;
    }

    if (d < 0.) {
        particles[i].position.xyz += particles[i].velocity.xyz * -d;
        return;
    }

    vec3 force = vec3(0.);
    for (int j = 0; j < N; j++) {
        vec4 other = particles[j].position;
        vec3 offset = other.xyz - self.xyz;
        float distance = length(offset);
        if (distance < 1e-5 || distance > RMAX) continue;
        float r = distance / RMAX;
        float strength = r < BETA
            ? r / BETA - 1.
            : attraction(self.w, other.w) * (1. - abs(2. * r - 1. - BETA) / (1. - BETA));
        force += offset / distance * strength;
    }
    force *= 3.;

    // Soft containment, applied once per particle rather than per pair: only
    // particles past .85 are pulled back, so the interior stays undisturbed.
    // The pull is quadratic in the overshoot: gentle at the boundary, but stiff
    // enough further out that a cell cannot drift away over the five minutes the
    // intro may run. Raising the linear term instead does not work - it dims the
    // whole cluster and still lets the outliers escape.
    float radius = length(self.xyz);
    if (radius > .85) {
        float over = radius - .85;
        force -= self.xyz * over * (26. + 60. * over) / radius;
    }

    // Once, at REVEAL_START, ease the organic force out entirely and a strong
    // pull toward an assigned point on the word NUTS in, so the cluster
    // spells it out and stays that way for the rest of the run.
    float reveal = RevealBlend(t);
    if (reveal > .001) {
        // A full override at reveal 1, not a blend that leaves some pairwise
        // force active: 16384 particles compressed onto a thin curve sit far
        // closer together than in the free-swimming sphere, so even a small
        // residual of that force is hugely amplified by the extra density and
        // breaks the letter shapes into clumps instead of smooth strokes.
        vec3 target = LetterTarget(float(i));
        force = mix(force, (target - self.xyz) * 9., reveal);
    }

    vec3 velocity = particles[i].velocity.xyz + force * d;
    particles[i].velocity = vec4(velocity * exp(-17. * d), 0.);
}
