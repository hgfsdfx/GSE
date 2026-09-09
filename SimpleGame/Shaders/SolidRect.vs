#version 330 core
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in float a_Emission;
uniform vec2 u_Viewport;
out vec4 v_Color;
out float v_Emission;
void main() {
    vec2 position = a_Position / u_Viewport * 2.0 - 1.0;
    gl_Position = vec4(position.x, -position.y, 0.0, 1.0);
    v_Color = a_Color;
    v_Emission = a_Emission;
}
