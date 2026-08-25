uniform float t;
varying vec4 c;

void main() {
    float o = cos(t), s = sin(t);
    gl_Position = vec4(o * gl_Vertex.x - s * gl_Vertex.y,
                       s * gl_Vertex.x + o * gl_Vertex.y, 0., 1.);
    c = gl_Color;
}
