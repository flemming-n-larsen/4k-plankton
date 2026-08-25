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

void main() {
    vec4 particle = particles[gl_VertexID].position;

    float azimuth = t * .13;
    float elevation = sin(t * .07) * .5;
    vec3 eye = vec3(sin(azimuth) * cos(elevation), sin(elevation), cos(azimuth) * cos(elevation)) * 2.0;
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

    // The 6. matches TYPES in particles.comp.glsl.
    tint = .5 + .5 * cos(6.2831 * (particle.w / 6. + vec3(0., .33, .67)));
    fade = exp(-.55 * (view.z - 2.));
}
