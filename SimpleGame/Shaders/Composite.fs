#version 330 core
in vec2 v_UV;
layout(location = 0) out vec4 FragColor;
uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_SoftScene;
uniform float u_Exposure;
uniform float u_BloomStrength;
uniform float u_VignetteStrength;
uniform vec2 u_VignetteRange;
uniform float u_EdgeStrength;
uniform vec2 u_EdgeRange;

vec3 LinearToSRGB(vec3 value)
{
    vec3 low = 12.92 * value;
    vec3 high = 1.055 * pow(max(value, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}

void main()
{
    vec3 scene = texture(u_Scene, v_UV).rgb;
    // Normalized elliptical distance: 0 at center, 1 at side midpoints.
    float radius = length(v_UV * 2.0 - 1.0);
    float edge = smoothstep(u_EdgeRange.x, u_EdgeRange.y, radius) * u_EdgeStrength;
    if (u_EdgeStrength > 0.0)
    {
        scene = mix(scene, texture(u_SoftScene, v_UV).rgb, edge);
    }
    if (u_BloomStrength > 0.0)
    {
        scene += texture(u_Bloom, v_UV).rgb * u_BloomStrength;
    }
    float vignette = smoothstep(u_VignetteRange.x, u_VignetteRange.y, radius);
    scene *= 1.0 - vignette * u_VignetteStrength;
    vec3 exposed = max(scene * u_Exposure, vec3(0.0));
    // Reinhard's highlight shoulder preserves dark streets and compresses HDR neon.
    vec3 mapped = exposed / (vec3(1.0) + exposed);
    FragColor = vec4(LinearToSRGB(mapped), 1.0);
}
