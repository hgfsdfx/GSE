#version 330 core
in vec4 v_Color;
in float v_Emission;
in vec3 v_Glow;
in float v_Cutoff;
flat in int v_Model;
uniform bool u_LinearScene;
layout(location = 0) out vec4 FragColor;

vec3 SRGBToLinear(vec3 color)
{
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}

vec3 LinearToSRGB(vec3 color)
{
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

void main()
{
    if (v_Color.a <= 0.001 || v_Color.a < v_Cutoff)
    {
        discard;
    }
    vec3 color;
    if (v_Model != 0)
    {
        // glTF factors and catalog palettes are already linear RGB.
        color = max(v_Color.rgb + v_Glow, vec3(0.0));
        if (!u_LinearScene)
        {
            color = LinearToSRGB(clamp(color, 0.0, 1.0));
        }
    }
    else
    {
        color = clamp(v_Color.rgb, 0.0, 1.0);
        if (u_LinearScene)
        {
            color = SRGBToLinear(color) * (1.0 + clamp(v_Emission, 0.0, 32.0));
        }
    }
    FragColor = vec4(color, clamp(v_Color.a, 0.0, 1.0));
}
