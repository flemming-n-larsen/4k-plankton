uniform float t;
uniform vec2 r;

float scene(vec3 p) {
    return length(p) - .7;
}

vec3 normal(vec3 p) {
    vec2 e = vec2(.001, 0.);
    return normalize(vec3(scene(p + e.xyy) - scene(p - e.xyy),
                          scene(p + e.yxy) - scene(p - e.yxy),
                          scene(p + e.yyx) - scene(p - e.yyx)));
}

void main() {
    vec2 p = (2. * gl_FragCoord.xy - r) / r.y;
    vec3 origin = vec3(0., 0., 3.);
    vec3 ray = normalize(vec3(p, -2.));
    float distance = 0.;
    float hit = 0.;
    for (int i = 0; i < 64; i++) {
        vec3 point = origin + ray * distance;
        hit = scene(point);
        if (hit < .001) break;
        distance += hit;
    }
    if (hit < .001) {
        vec3 point = origin + ray * distance;
        vec3 light = normalize(vec3(sin(t), 1., cos(t)));
        float brightness = max(dot(normal(point), light), 0.);
        gl_FragColor = vec4(vec3(.08, .35, .85) * (.15 + brightness), 1.);
    } else {
        gl_FragColor = vec4(.01, .015, .03, 1.);
    }
}
