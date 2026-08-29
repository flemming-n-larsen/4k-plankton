#version 430

// The tuning constants are written as literals rather than #defines: Shader
// Minifier does not substitute object-like macros, so every #define would cost
// both its own line and its full-length name at each use site in the Compo
// build. Comments are stripped, so naming them here is free.
//   16384  particle count, must match PARTICLE_COUNT in src/intro.c
//   6      species count, must match the palette in particles.vert.glsl
//   .11    interaction radius
//   .6     repulsion fraction of that radius, so 1. - .6 is its complement
//   30..36 the seconds over which the word forms, 44..51 the release

layout(local_size_x = 256) in;

// d == 0 initializes the buffer, d > 0 integrates velocity, d < 0 integrates
// position by -d. Splitting the two integrations avoids reading a neighbour
// position that another workgroup has already advanced.
uniform float d;
uniform float t;

// A flat vec4 array rather than an array of two-vec4 structs: the struct and
// the field names survive minification (--preserve-externals protects the
// block), so particle n's position is v[n * 2] and its velocity v[n * 2 + 1].
layout(std430, binding = 0) buffer P {
    vec4 v[];
};

// The 3. is the seed: change it to pick a different, fully reproducible
// attractor. Nothing here is seeded from the clock.
float hash(float n) {
    return fract(sin(n + 3. * 17.13) * 43758.5453);
}

// Off-diagonal pairs get a signed random weight; a type always attracts itself,
// which is what makes cohesive cells form instead of a uniform gas.
float attraction(float a, float b) {
    float weight = hash(a * 7.3 + b * 13.7 + 3.1) * 2. - 1.;
    return a == b ? .5 + .5 * abs(weight) : weight;
}

// A point on stroke index'th centerline, at fraction u along it, offset by
// jitter (perpendicular for a straight stroke, radial for an arc) so a swarm
// of points reads as a stroke with real width rather than a bare wire. NUTS
// is drawn as N (3 line segments), U (2 segments and a half-circle), T (2
// segments), and S (two 3/4-circles) - no embedded glyph image, just these
// closed-form lines and arcs. Ten strokes in all; the last one falls through
// without a test.
//
// Every angle sweep is written as a multiply-add rather than a mix(), and the
// N and U verticals are merged into one branch each, since they differ only by
// a .6 offset along x.
vec2 StrokePoint(int stroke, float u, float jitter) {
    if (stroke < 2) return vec2(float(stroke) * .6 + jitter, u);                                // N: the two verticals
    if (stroke == 2) return vec2(.6 * u, 1. - u) + vec2(.857493, .514496) * jitter;             // N: diagonal, its perpendicular precomputed
    if (stroke < 5) return vec2(.85 + float(stroke - 3) * .6 + jitter, .35 + .65 * u);          // U: the two verticals
    if (stroke == 5) {
        float a = 3.14159 + 3.1416 * u;
        return vec2(1.15, .35) + (.3 + jitter) * vec2(cos(a), sin(a));                          // U: bottom half-circle
    }
    if (stroke == 6) return vec2(1.7 + .6 * u, 1. + jitter);                                    // T: top bar
    if (stroke == 7) return vec2(2. + jitter, u);                                               // T: stem
    // Each S arc sweeps 3/4 of a circle rather than half, so the terminals
    // curl back into a hook past vertical instead of ending flat. Both are
    // also rotated 30 degrees the same way: with no rotation, the two circles
    // are exactly tangent on the vertical centerline, so every particle near
    // the middle converges on that single point from four symmetric
    // directions and pinches into a bowtie instead of a flowing diagonal.
    if (stroke == 8) {
        float a = .2618 + 4.7124 * u;
        return vec2(2.85, .75) + (.25 + jitter) * vec2(cos(a), sin(a));                         // S: upper 3/4-circle, bulging left
    }
    float a = 4.7124 * u - 2.8798;
    return vec2(2.85, .25) + (.25 + jitter) * vec2(cos(a), sin(a));                             // S: lower 3/4-circle, bulging right
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
    vec2 local = StrokePoint(int(hash(i * 1.3 + 77.) * 10.), hash(i * 2.9 + 12.), (hash(i * 1.9 + 41.) - .5) * .1);
    // .8: scaled up together with the camera dolly-in in particles.vert.glsl
    // so the finished word fills as much of the frame as its wide, 4-letter
    // aspect ratio allows without clipping horizontally.
    return vec3((local.x - 1.575) * .8, (local.y - .5) * .8, 0.);
}

void main() {
    uint i = gl_GlobalInvocationID.x, b = i * 2;
    vec4 self = v[b];

    if (d == 0.) {
        float n = float(i);
        vec3 direction = normalize(vec3(hash(n * .37), hash(n * .71 + 1.), hash(n * 1.13 + 2.)) - .5);
        v[b] = vec4(direction * pow(hash(n * 2.3 + 5.), .3333) * .9, floor(hash(n * 3.7 + 9.) * 6.));
        v[b + 1] = vec4(0.);
        return;
    }

    if (d < 0.) {
        v[b].xyz += v[b + 1].xyz * -d;
        return;
    }

    vec3 force = vec3(0.);
    for (int j = 0; j < 16384; j++) {
        vec4 other = v[j * 2];
        vec3 offset = other.xyz - self.xyz;
        float distance = length(offset);
        if (distance < 1e-5 || distance > .11) continue;
        float r = distance / .11;
        float strength = r < .6
            ? r / .6 - 1.
            : attraction(self.w, other.w) * (1. - abs(2. * r - 1. - .6) / (1. - .6));
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

    // The formation timeline: the cluster is the main effect, and the particles
    // ease into the word NUTS over the 30..36 second window, hold it, and are
    // then let go again over 44..51, so the run ends on a release rather than
    // on a dimmer switch. The 30..36 seconds must match the copy in
    // particles.vert.glsl, which eases the camera to a full-frame front-on view
    // over the same window.
    //
    // The release is deliberately NOT copied into the camera. The camera's
    // free-swim azimuth is absolute - t * .13 - so letting its reveal fall back
    // to zero would swing the shot through six radians in seven seconds while
    // the word was coming apart. The camera holds its pulled-back front-on view
    // and lets the subject dissolve in front of it.
    float reveal = smoothstep(30., 36., t) * (1. - smoothstep(44., 51., t));
    if (reveal > .001) {
        // A full override at reveal 1, not a blend that leaves some pairwise
        // force active: 16384 particles compressed onto a thin curve sit far
        // closer together than in the free-swimming sphere, so even a small
        // residual of that force is hugely amplified by the extra density and
        // breaks the letter shapes into clumps instead of smooth strokes.
        //
        // Unwinding that same blend is what makes the ending work. Handing the
        // word back to the pairwise force at the density it was never tuned for
        // is exactly the failure above, run deliberately and in reverse: the
        // short-range repulsion tears the strokes open and the type attractions
        // gather what is left into cells, so the logo comes apart into living
        // creatures rather than simply drifting or blowing outward. Scaling the
        // targets outward instead was tried, and reads as a push-in - the word
        // keeps its shape and merely grows past the edges of the frame.
        //
        // The velocity-driven brightness in particles.vert.glsl flares the swarm
        // while it breaks up, so the release answers the surge at thirty seconds
        // without a scripted flash, and the master fade at 48..56 carries away
        // what is left.
        force = mix(force, (LetterTarget(float(i)) - self.xyz) * 9., reveal);
    }

    vec3 velocity = v[b + 1].xyz + force * d;
    v[b + 1] = vec4(velocity * exp(-17. * d), 0.);
}
