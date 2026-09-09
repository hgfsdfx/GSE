#version 330 core
out vec2 v_UV;
void main() {
    // One oversized triangle covers the viewport without a diagonal seam.
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_UV = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
