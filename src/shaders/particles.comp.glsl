#version 430

// Must match PARTICLE_COUNT in src/intro.c.
#define N 16384
#define TYPES 6.
#define RMAX .11
#define BETA .6
// Change SEED to pick a different, fully reproducible attractor.
#define SEED 3.

layout(local_size_x = 256) in;

// d == 0 initializes the buffer, d > 0 integrates velocity, d < 0 integrates
// position by -d. Splitting the two integrations avoids reading a neighbour
// position that another workgroup has already advanced.
uniform float d;

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

    vec3 velocity = particles[i].velocity.xyz + force * d;
    particles[i].velocity = vec4(velocity * exp(-17. * d), 0.);
}
