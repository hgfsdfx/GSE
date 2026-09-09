#version 330 core
in vec2 v_UV;
layout(location = 0) out vec4 FragColor;
uniform sampler2D u_Source;
uniform vec2 u_Texel;
uniform bool u_Extract;
uniform float u_Threshold;
uniform float u_Knee;

vec3 Filter(vec2 uv) {
    vec3 color = max(texture(u_Source, uv).rgb, vec3(0.0));
    if (!u_Extract) return color;
    // Channel maximum preserves saturated cyan/magenta neon highlights.
    float brightness = max(color.r, max(color.g, color.b));
    float knee = max(u_Knee, 0.0001);
    float soft = clamp(brightness - u_Threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(brightness - u_Threshold, soft) / max(brightness, 0.0001);
    return color * contribution;
}
void main() {
    // Prefilter each tap before averaging, retaining narrow emissive lines.
    vec2 offset = u_Texel * 0.5;
    vec3 color = Filter(v_UV + vec2(-offset.x, -offset.y));
    color += Filter(v_UV + vec2(offset.x, -offset.y));
    color += Filter(v_UV + vec2(-offset.x, offset.y));
    color += Filter(v_UV + offset);
    FragColor = vec4(color * 0.25, 1.0);
}
