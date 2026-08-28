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

    // Depth gradient, from deep water at the bottom to shallow at the top,
    // written as the deep colour plus the difference rather than a mix().
    vec3 colour = vec3(.0, .03, .08) + vec3(.05, .32, .37) * p.y;

    // Caustic filaments: a sharpened sine ridge, fading out with depth.
    float caustic = pow(abs(sin(q.x * 10. + sin(q.y * 8. + t * .6) * 2.)), 4.);
    colour += caustic * vec3(.6, 1., .9) * .12 * p.y;

    // God rays from a sun above the top edge, converging downward.
    vec2 sun = q - vec2(.5, 1.15);
    float rays = pow(abs(sin(atan(sun.x, sun.y) * 9. + sin(t * .3) * .4)), 6.);
    colour += rays * vec3(.9, 1., .85) * .5 * p.y / (1. + dot(sun, sun) * .6);

    gl_FragColor = vec4(colour * (1. - .35 * length(p - .5)) * fade, 1.);
}
