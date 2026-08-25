uniform float t;
uniform vec2 r;

void main() {
    vec2 p = gl_FragCoord.xy / r;

    // Waving water surface: warp x by a couple of slow, offset sine waves in y.
    vec2 q = p;
    q.x += .015 * sin(q.y * 14. + t * 1.1) + .008 * sin(q.y * 27. - t * 1.7);

    vec3 deep = vec3(.0, .03, .08);
    vec3 shallow = vec3(.05, .35, .45);
    vec3 colour = mix(deep, shallow, p.y);

    // Caustic filaments: a sharpened sine ridge, fading out with depth.
    float caustic = pow(abs(sin(q.x * 10. + sin(q.y * 8. + t * .6) * 2.)), 4.);
    colour += caustic * vec3(.6, 1., .9) * .12 * p.y;

    // God rays from a sun above the top edge, converging downward.
    vec2 sun = q - vec2(.5, 1.15);
    float angle = atan(sun.x, sun.y);
    float rays = pow(abs(sin(angle * 9. + sin(t * .3) * .4)), 6.);
    colour += rays * vec3(.9, 1., .85) * .5 * p.y / (1. + dot(sun, sun) * .6);

    colour *= 1. - .35 * length(p - .5);
    gl_FragColor = vec4(colour, 1.);
}
