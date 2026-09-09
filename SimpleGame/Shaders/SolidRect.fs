#version 330 core
in vec4 v_Color;
in float v_Emission;
uniform bool u_LinearScene;
layout(location = 0) out vec4 FragColor;
vec3 SRGBToLinear(vec3 color) {
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}
void main() {
    vec3 color = clamp(v_Color.rgb, 0.0, 1.0);
    if (u_LinearScene)
        color = SRGBToLinear(color) * (1.0 + clamp(v_Emission, 0.0, 32.0));
    FragColor = vec4(color, clamp(v_Color.a, 0.0, 1.0));
}
