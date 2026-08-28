#version 430

uniform float t;
uniform vec2 r;

// Flat vec4 array, matching particles.comp.glsl: particle n's position is
// v[n * 2]. The velocity at v[n * 2 + 1] is not read here.
layout(std430, binding = 0) buffer P {
    vec4 v[];
};

// Colour, already multiplied by the distance fade and the master fade. A point
// sprite has a single vertex, so folding the fade in here is exactly what the
// fragment shader used to compute from a separate varying.
out vec3 c;

// Six plankton hues, indexed by the type in the position's w component, which
// particles.comp.glsl writes once at init as floor(hash(..) * 6.) - the entry
// count here must match that 6. A cosine palette cannot reach both the cool
// water tones and the warm ones without sweeping through hues that do not
// belong in the water, so the six are written out.
const vec3 PLANKTON[6] = vec3[6](
    vec3(.10, .45, .50),  // teal - copepod water
    vec3(.14, .62, .68),  // bioluminescent cyan
    vec3(.16, .52, .30),  // sea green - green algae
    vec3(.10, .26, .62),  // deep blue - cyanobacteria
    vec3(.88, .24, .18),  // light red - red-tide dinoflagellate
    vec3(.82, .68, .20)   // faint yellow - diatom gold
);

void main() {
    vec4 particle = v[gl_VertexID * 2];

    // Must match particles.comp.glsl's timeline exactly, so the camera settles
    // into its front-on view over the same 30..36 second window the particles
    // take to spell the word out.
    float reveal = smoothstep(30., 36., t);
    float rest = 1. - reveal;
    float azimuth = t * .13 * rest;
    float elevation = sin(t * .07) * .5 * rest;
    float sa = sin(azimuth), ca = cos(azimuth), se = sin(elevation), ce = cos(elevation);

    // The camera basis in closed form. The orbit direction is unit length
    // already, so forward needs no normalize; and cross(forward, up_world)
    // reduces to (cos azimuth, 0, -sin azimuth) exactly, since cos(elevation)
    // stays positive over the +-.5 radian the elevation covers. That leaves
    // one sin/cos pair per angle and no cross products at all.
    vec3 direction = vec3(sa * ce, se, ca * ce);
    // The camera's free-swim orbit radius is .5, kept inside the containment
    // shell (radius .9 in particles.comp.glsl) so the camera swims within the
    // swarm rather than looking at it from outside. That is what fills the
    // corners: from outside, however close, the shell still projects as a
    // silhouette, and a ray toward a corner only grazes it, so the corners stay
    // empty no matter how far the camera dollies in. From inside, every ray
    // exits through the swarm and there is no edge anywhere on screen. It also
    // puts the whole cluster within ~1.4 of the camera, so the depth absorption
    // below barely engages and the creature colours stay saturated. .5 rather
    // than closer still: this is also the radius of the circle the camera
    // travels, and a smaller one leaves too little parallax for the drift to
    // read. It doubles as the reference depth of the distance fade below, so
    // brightness does not track the camera distance.
    //
    // As the word forms the camera pulls back out of the swarm, from .5 to 1.3:
    // at that distance and the .8 word scale in particles.comp.glsl, the word
    // fills the frame horizontally (its 4-letter aspect ratio is much wider
    // than the screen's, so it is always width-bound, with room left above and
    // below). The camera has to leave the cluster to read the word at all, so
    // the reveal is a retreat from the plankton rather than a push in.
    vec3 offset = particle.xyz - direction * (.5 + .8 * reveal);
    vec3 view = vec3(dot(offset, vec3(ca, 0., -sa)),
                     dot(offset, vec3(-sa * se, ce, -ca * se)),
                     -dot(offset, direction));

    // 1.7 is the focal length.
    gl_Position = vec4(view.x * 1.7 * r.y / r.x, view.y * 1.7, view.z * .01, view.z);
    // Both the size and its ceiling scale with viewport height, so 1080p, 720p
    // and any downscaled capture show the same image. The ceiling stops a
    // particle drifting close to the camera from filling the screen with one
    // sprite; as an absolute pixel constant it instead capped every sprite at
    // the compo resolutions - the camera sits inside the swarm, so the whole
    // cluster is near - and the sea looked half empty on anything above 540p.
    gl_PointSize = clamp(.0374 * r.y / view.z, 1., .033 * r.y);

    c = PLANKTON[int(particle.w)];

    // Light absorption with distance: far particles pull toward deep-water blue,
    // the way colour drops out of real water with depth.
    c = mix(c, vec3(.02, .08, .15), clamp((view.z - 1.) / 2.5, 0., 1.) * .5);
    // Fades in from black slowly, so the procedural background (depth gradient,
    // caustics, god rays) reads clearly before the particles compete for
    // attention, then fades back to black a few seconds after NUTS is fully
    // formed, ending the intro on the held word rather than an abrupt cut. Must
    // match the copy in background.frag.glsl.
    c *= min(smoothstep(0., 8., t), 1. - smoothstep(45., 53., t));
    // The per-particle distance fade, exp(-.55 * (view.z - .5)), folded in.
    c *= exp(.275 - .55 * view.z);
}
