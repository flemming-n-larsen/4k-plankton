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

    // Cosine palette (Quilez form: a + b*cos(2pi(c*x+d))) tuned to stay in the
    // teal/cyan/blue/green range: the red channel's amplitude is kept small so no
    // phase swings into warm hues. The 6. matches TYPES in particles.comp.glsl.
    float hue = particle.w / 6.;
    tint = vec3(.15, .45, .55) + vec3(.10, .35, .35) * cos(6.2831 * (vec3(.3, .5, .5) * hue + vec3(.1, .2, .35)));

    // Light absorption with distance: far particles pull toward deep-water blue,
    // the way colour drops out of real water with depth.
    tint = mix(tint, vec3(.02, .08, .15), clamp((view.z - 1.) / 2.5, 0., 1.) * .5);

    fade = exp(-.55 * (view.z - 2.));
}
