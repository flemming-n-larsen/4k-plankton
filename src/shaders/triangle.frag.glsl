uniform float t;
uniform vec2 r;

void main() {
    vec2 p = (2. * gl_FragCoord.xy - r) / r.y;
    gl_FragColor = vec4(.5 + .5 * cos(t + p.xyx + vec3(0., 2., 4.)), 1.);
}
