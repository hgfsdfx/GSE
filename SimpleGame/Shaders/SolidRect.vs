#version 330 core
// Both 2D and skinned 3D triangles share the ordered instance stream.
layout(location = 0) in uvec2 a_Reference;
uniform samplerBuffer u_MeshVertices;
uniform samplerBuffer u_ModelVertices;
uniform samplerBuffer u_Bones;
uniform samplerBuffer u_Instances;
uniform vec2 u_Viewport;
uniform float u_DepthStep;
out vec4 v_Color;
out float v_Emission;
out vec3 v_Glow;
out float v_Cutoff;
flat out int v_Model;

mat4 Bone(int palette, int joint)
{
    int start = palette + 4 + joint * 4;
    return mat4(texelFetch(u_Bones, start), texelFetch(u_Bones, start + 1),
                texelFetch(u_Bones, start + 2), texelFetch(u_Bones, start + 3));
}

void main()
{
    int instance = int(a_Reference.y) * 4;
    vec4 transformX = texelFetch(u_Instances, instance);
    vec4 transformY = texelFetch(u_Instances, instance + 1);
    vec4 tint = texelFetch(u_Instances, instance + 2);
    vec4 extra = texelFetch(u_Instances, instance + 3);
    vec2 screen;
    float depth = 1.0 - (transformY.z + 1.0) * u_DepthStep;
    v_Model = transformY.w > 0.0 ? 1 : 0;
    v_Glow = vec3(0.0);
    v_Cutoff = 0.0;
    v_Emission = 0.0;
    if (v_Model == 0)
    {
        int vertex = (int(a_Reference.x) + gl_VertexID) * 2;
        vec4 geometry = texelFetch(u_MeshVertices, vertex);
        vec4 material = texelFetch(u_MeshVertices, vertex + 1);
        screen = transformX.xy + geometry.x * transformX.zw + geometry.y * transformY.xy;
        v_Color = vec4(geometry.zw, material.xy) * tint;
        v_Emission = material.z + extra.x;
    }
    else
    {
        int vertex = (int(a_Reference.x) + gl_VertexID) * 6;
        vec4 geometry = texelFetch(u_ModelVertices, vertex);
        vec4 albedo = texelFetch(u_ModelVertices, vertex + 1);
        vec4 normal = texelFetch(u_ModelVertices, vertex + 2);
        ivec4 joints = ivec4(texelFetch(u_ModelVertices, vertex + 3));
        vec4 weights = texelFetch(u_ModelVertices, vertex + 4);
        vec4 emission = texelFetch(u_ModelVertices, vertex + 5);
        int palette = int(transformY.w) - 1;
        mat4 skin = Bone(palette, joints.x) * weights.x;
        if (weights.y > 0.0)
        {
            skin += Bone(palette, joints.y) * weights.y;
        }
        if (weights.z > 0.0)
        {
            skin += Bone(palette, joints.z) * weights.z;
        }
        if (weights.w > 0.0)
        {
            skin += Bone(palette, joints.w) * weights.w;
        }
        vec3 p = (skin * vec4(geometry.xyz, 1.0)).xyz;
        vec3 stretch = vec3(transformY.x, 1.0, transformY.y) * extra.y;
        p *= stretch;
        mat3 yaw = mat3(extra.z, 0.0, -extra.w, 0.0, 1.0, 0.0, extra.w, 0.0, extra.z);
        p = yaw * p;
        // GLB (x,y,z) -> world (x,-z), height y, with the existing camera projection.
        screen = transformX.xy + vec2((p.x + p.z) * 0.82, (p.x - p.z) * 0.43 - p.y);
        depth -= transformX.w * u_DepthStep
                 * clamp(dot(p, vec3(1.0, 0.86, -1.0)) / transformX.z, -1.0, 1.0);
        vec3 n = normal.xyz;
        mat3 skinNormal = mat3(skin);
        if (abs(determinant(skinNormal)) > 0.000001)
        {
            n = transpose(inverse(skinNormal)) * n;
        }
        n = yaw * (n / stretch);
        n = length(n) > 0.000001 ? normalize(n) : vec3(0.0, 1.0, 0.0);
        float diffuse = max(dot(n, normalize(vec3(-0.5, 1.0, 0.6))), 0.0);
        vec3 light = normal.w > 0.5 ? vec3(1.0)
                     : vec3(0.33, 0.42, 0.57) + diffuse * vec3(0.7, 0.76, 0.85);
        int role = int(geometry.w);
        if (role > 0 && role <= 4)
        {
            vec4 replacement = texelFetch(u_Bones, palette + role - 1);
            if (replacement.r >= 0.0)
            {
                if (role == 4)
                {
                    float intensity = max(max(emission.r, emission.g), emission.b)
                                      / max(max(max(albedo.r, albedo.g), albedo.b), 0.001);
                    emission.rgb = replacement.rgb * intensity;
                }
                albedo.rgb = replacement.rgb;
            }
        }
        float power = max(max(emission.r, emission.g), emission.b) > 0.0
                      ? mix(0.08, 1.0, clamp(extra.x, 0.0, 1.0)) : 1.0;
        v_Color = vec4(albedo.rgb * light * power, albedo.a) * tint;
        v_Glow = emission.rgb * extra.x;
        v_Cutoff = emission.a;
    }
    vec2 position = screen / u_Viewport * 2.0 - 1.0;
    gl_Position = vec4(position.x, -position.y, depth, 1.0);
}
