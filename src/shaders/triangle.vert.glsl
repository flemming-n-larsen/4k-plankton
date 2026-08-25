uniform float t;
uniform vec2 r;
varying vec4 c;

void main() {
    float o = cos(t), s = sin(t);
    gl_Position = vec4((o * gl_Vertex.x - s * gl_Vertex.y) * r.y / r.x,
                       s * gl_Vertex.x + o * gl_Vertex.y, 0., 1.);
    c = gl_Color;
}
