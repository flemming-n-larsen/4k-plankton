#version 430

layout(location = 0) uniform float t;
layout(location = 1) uniform vec2 r;

// Flat vec4 array, matching particles.comp.glsl: particle n's position is
// v[n * 2]. The velocity at v[n * 2 + 1] is not read here.
// std430 is spelled out, so the layout is the one the spec fixes. There is no
// binding qualifier: it defaults to 0, which is what the C side binds.
layout(std430) buffer P {
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
    // The free-swim orbit fades out as the word forms, and a much smaller
    // oscillation fades in behind it, so the held word keeps turning gently
    // instead of stopping dead. Without it the last stretch of the run is a
    // still frame: both angles would be exactly zero and the distance is
    // constant, so nothing but the background moves for the rest of the run.
    //
    // The residual has to swing around zero rather than be a fraction of the
    // free-swim term. Azimuth there is absolute - t * .13 - so leaving even 4%
    // of it would put the camera 10.7 degrees off axis by the time the word
    // lands and still climbing; against a word half-width of about 1.24 and a
    // camera distance of 1.3 that spreads the letters across depths 1.07 to
    // 1.53, a 43% sprite-size difference end to end and a visibly skewed logo.
    // At .045 radians the spread is about 1.30 to 1.42, which reads as the word
    // having some dimension. The periods, 25s and 37s, are long enough that the
    // eight-second hold sees well under half a cycle - a drift, not a wobble.
    float rest = 1. - reveal;
    float azimuth = t * .13 * rest + sin(t * .25) * .09 * reveal;
    float elevation = sin(t * .07) * .5 * rest + sin(t * .17) * .03 * reveal;
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
    // As the word forms the camera pulls back out of the swarm, from .5 to 1.42:
    // at that distance and the .8 word scale in particles.comp.glsl, the word
    // fills the frame horizontally (its 4-letter aspect ratio is much wider
    // than the screen's, so it is always width-bound, with room left above and
    // below). The camera has to leave the cluster to read the word at all, so
    // the reveal is a retreat from the plankton rather than a push in.
    //
    // 1.42 rather than the 1.3 that just fits a static camera. The widest point
    // of the word - the N's left vertical, at -1.30 in world units once its
    // jitter is counted - projects to -.956 of the frame half-width at 1.3 with
    // the camera dead ahead, which is a 4% margin and no room for the drift
    // above: at its extreme the same edge lands at -1.017 and the letter is cut
    // off the side. The extra distance buys the margin back and costs about a
    // tenth of the word's size.
    // The opening is a dive: the camera starts 1.8 units further out and swims in to
    // the swarm over the first ten seconds, so the intro starts on the whole
    // creature cloud and ends the dive among the colonies.
    vec3 offset = particle.xyz - direction * (.5 + .92 * reveal - .2 * smoothstep(38., 56., t) + 1.8 * (1. - smoothstep(0., 10., t)));
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
    // the way colour drops out of real water with depth. It eases off as the
    // scene rises toward the surface over seconds 4 to 40, so the plankton
    // regain saturation on the way up; must match the ascent in
    // background.frag.glsl.
    // The wave also washes the water clear: the plankton regain a third more of
    // their colour once it has passed (awake, 17..21 seconds).
    float awake = smoothstep(17., 21., t);
    c = mix(c, vec3(.02, .08, .15),
            clamp((view.z - 1.) / 2.5, 0., 1.) * (.75 - .25 * smoothstep(4., 40., t) - .3 * awake));

    // Bioluminescent pulse. The six species flash and go dark in turn, a sixth
    // of a cycle apart, over a five-second period. This is keyed to the type
    // rather than to the index on purpose: the pairwise force sorts each
    // species into its own spatially coherent cell, so pulsing by type lights
    // whole regions of the swarm in sequence and the light travels through the
    // cluster as a wave, where pulsing by index would only produce a uniform
    // per-particle flicker. It is what the free swim needs - twenty-four
    // seconds in which the camera orbits and the light slowly grows, and
    // nothing else happens.
    //
    // The sine is cubed into a flash rather than used as a swell. A plain sine
    // was tried first and is invisible: it spends most of its time near the
    // middle, and with the six phases evenly spread the frame's total light
    // never changes, so the eye reads a slow reshuffle of which species is
    // brightest rather than anything pulsing. Cubing puts each species dark for
    // most of the cycle and very bright briefly, which is both what the
    // creatures actually do and what can be seen.
    //
    // The mean of that cubed shape is exactly 5/16, so .35 + 2.08 * pulse
    // averages to one: the flash costs no exposure and the frame's average
    // brightness, tuned against a projector in a blacked-out hall, is
    // unchanged. Written as a deviation from one so the reveal can damp it to a
    // quarter with a single multiply - a strobing logo fights legibility, and
    // the speed accent below already owns that moment.
    //
    // 1.2566 is 2*PI/5 for the period, 1.0472 is 2*PI/6 for the phase step.
    float pulse = .5 + .5 * sin(t * 1.2566 + particle.w * 1.0472);
    pulse *= pulse * pulse;
    // The flashes are held to a fifth of their strength until the wave, which
    // wakes them: the free swim before it is calm, and what follows is not.
    c *= 1. + (1. - .75 * reveal) * (.2 + .8 * awake) * (1.6 * pulse - .5);

    // The wave. Between 17 and about 21 seconds a shell of light leaves the
    // camera and sweeps outward through the swarm, which is the one event in
    // the free-swim stretch. It is centred on the camera, not the swarm: a
    // shell around the swarm centre only lights the few colonies that happen to
    // lie on the camera's slice of it, while one around the camera crosses
    // every direction at once and reads as a front rolling over the whole
    // frame. There is no gate: the shell's radius is negative before 17 and
    // past the swarm after about 21, so the term is zero outside that window.
    float shell = length(offset) - (t - 17.) * .5;
    // The same shell again as the last letter locks, around 37 seconds. The held
    // word is 1.42 from the camera at its centre and up to 1.9 at its edges, so a
    // shell widening from 1.36 sweeps across it from the middle outward.
    float ring = length(offset) - 1.36 - (t - 37.) * .6;
    // Around the wave: the light dips for a moment just before it arrives, as
    // if the water drew breath, and once it has passed the swarm keeps a glow
    // that fades over the next nine seconds, so the wave leaves a mark.
    float breath = t - 17.9;
    c *= (1. - .6 * exp(-3. * breath * breath))
       * (1. + .6 * smoothstep(18., 20., t) * (1. - smoothstep(21., 30., t)))
       * (1. + 6. * exp(-30. * shell * shell) + .7 * exp(-40. * ring * ring));

    // Brightness from speed, read straight out of the velocity half of the
    // buffer that nothing else in this shader touches. This is the reveal
    // accent, and it costs one buffer read rather than a scripted flash: for
    // the six seconds the particles are migrating into the word every one of
    // them is moving hard, so the whole swarm surges and then decays as it
    // settles into the letters - an event that arrives and resolves on its own,
    // exactly on the timeline, with no constant to keep in step. While the
    // cluster is just drifting it reads as a faint twinkle on the fastest cells.
    c *= 1. + .5 * length(v[gl_VertexID * 2 + 1].xyz);
    // The duck: its gold glows brighter while it gathers, and every particle that
    // is not part of it (the same test as in particles.comp.glsl) fades back, so
    // the cells swimming past do not cover it.
    float duck = smoothstep(45., 48., t) * (1. - smoothstep(53., 57., t));
    if (particle.w == 5.) c *= 1. + 2.5 * duck;
    else if (!(particle.w == 4. && gl_VertexID % 6 == 0 || particle.w == 1. && gl_VertexID % 16 == 0)) c *= 1. - .7 * duck;
    // Fades in from black, then back to black a few seconds after NUTS is fully
    // formed, ending the intro on the held word rather than an abrupt cut. Must
    // match the copy in background.frag.glsl.
    c *= min(smoothstep(0., 5., t), 1. - smoothstep(54., 62., t));
    // The per-particle distance fade, exp(-.55 * (view.z - .5)), folded in.
    c *= exp(.275 - .55 * view.z);
}
