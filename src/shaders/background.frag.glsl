uniform float t;
uniform vec2 r;

void main() {
    // Fades the whole scene in from black at the start and back out a few
    // seconds after NUTS forms, rather than an abrupt cut. The 0..8 and 45..53
    // second windows must match the copy in particles.vert.glsl.
    float fade = min(smoothstep(0., 8., t), 1. - smoothstep(45., 53., t));
    vec2 p = gl_FragCoord.xy / r;

    // Waving water surface: warp x by a couple of slow, offset sine waves in y.
    vec2 q = p;
    q.x += .015 * sin(q.y * 14. + t * 1.1) + .008 * sin(q.y * 27. - t * 1.7);

    // The ascent. The intro opens deep and dark and rises toward the surface
    // over seconds 4 to 40, so the water brightens, the caustics come up and the
    // god rays strengthen as the word approaches - the one thing that develops
    // across the free-swim stretch, which otherwise never changes. The camera
    // does not actually travel: moving the eye upward would drag the bounded
    // swarm off centre, and the rising bubbles below sell the motion instead.
    // Every lit term already scaled with screen height, so folding the ascent
    // into that one factor drives all three at once and costs one expression.
    // The 4 keeps it clear of the fade-in and the 40 puts the brightest water
    // under the settled word. Must match the copy in particles.vert.glsl.
    float rise = smoothstep(4., 40., t);
    float lit = p.y * (.3 + .7 * rise);

    // Depth gradient, from deep water at the bottom to shallow at the top,
    // written as the deep colour plus the difference rather than a mix().
    vec3 colour = vec3(.0, .03, .08) + vec3(.05, .32, .37) * lit;

    // Caustic filaments: a sharpened sine ridge, fading out with depth.
    float caustic = pow(abs(sin(q.x * 10. + sin(q.y * 8. + t * .6) * 2.)), 4.);
    colour += caustic * vec3(.6, 1., .9) * .12 * lit;

    // God rays from a sun above the top edge, converging downward.
    vec2 sun = q - vec2(.5, 1.15);
    float rays = pow(abs(sin(atan(sun.x, sun.y) * 9. + sin(t * .3) * .4)), 6.);
    colour += rays * vec3(.9, 1., .85) * .5 * lit / (1. + dot(sun, sun) * .6);

    // Rising bubbles: three parallax layers of a hashed cell grid, each
    // scrolling upward. This is what sells the ascent - the light alone says
    // the water is getting shallower, but nothing says the camera is moving
    // until something streams past it. Screen space is legitimate here because
    // the camera never rolls (up in particles.vert.glsl is derived from world
    // up), so screen-vertical is world-vertical.
    //
    // The grid is built on an aspect-corrected copy of p; on the raw [0,1]
    // coordinates the cells would be 16:9 wide and every bubble a visible
    // ellipse. Each layer scrolls by the same -t, so the coarser layers drift
    // more slowly in screen units purely because their cells are larger - the
    // parallax comes free from the scale rather than from a per-layer rate.
    // Only cells hashing above .78 carry a bubble, so the water is not a
    // uniform lattice; the radius stays positive for every cell either way,
    // since smoothstep with two equal edges is undefined.
    vec2 cells = vec2(p.x * r.x / r.y, p.y);
    float bubbles = 0.;
    for (int layer = 0; layer < 2; layer++) {
        float depth = float(layer);
        vec2 grid = cells * (6. + depth * 4.) + vec2(depth * 3.7, -t);
        vec2 index = floor(grid);
        float h = fract(sin(index.x * 12.9898 + index.y * 78.233 + depth * 37.7) * 43758.5453);
        float g = fract(h * 31.7);
        float radius = .07 + .15 * h;
        vec2 centre = fract(grid) - vec2(.5 + .18 * sin(t * 1.3 + h * 20.), .15 + .7 * g);
        // An annulus, not a disc: a filled blob reads as out-of-focus dust,
        // while a bright rim around a dark middle reads as a bubble catching
        // the light from above. The second hash decorrelates the vertical
        // placement from the size, so the layers do not show their lattice.
        float d = length(centre);
        bubbles += (smoothstep(radius, radius * .8, d) - smoothstep(radius * .76, radius * .5, d))
                 * max(h - .78, 0.) * 5.;
    }
    colour += bubbles * vec3(.5, .8, .9) * .26 * (.3 + .7 * rise);

    gl_FragColor = vec4(colour * (1. - .35 * length(p - .5)) * fade, 1.);
}
