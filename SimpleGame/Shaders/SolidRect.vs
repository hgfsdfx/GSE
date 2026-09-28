#version 330 core
// Per-triangle instance: first atlas vertex and owning mesh-instance index.
layout(location = 0) in uvec2 a_Reference;
uniform samplerBuffer u_MeshVertices;
uniform samplerBuffer u_Instances;
uniform vec2 u_Viewport;
out vec4 v_Color;
out float v_Emission;

void main()
{
    int vertex = (int(a_Reference.x) + gl_VertexID) * 2;
    vec4 geometry = texelFetch(u_MeshVertices, vertex);
    vec4 material = texelFetch(u_MeshVertices, vertex + 1);
    int instance = int(a_Reference.y) * 4;
    vec4 transformX = texelFetch(u_Instances, instance);
    vec4 transformY = texelFetch(u_Instances, instance + 1);
    vec4 tint = texelFetch(u_Instances, instance + 2);
    float emission = texelFetch(u_Instances, instance + 3).x;
    vec2 screen = transformX.xy + geometry.x * transformX.zw + geometry.y * transformY.xy;
    vec2 position = screen / u_Viewport * 2.0 - 1.0;
    gl_Position = vec4(position.x, -position.y, 0.0, 1.0);
    v_Color = vec4(geometry.zw, material.xy) * tint;
    v_Emission = material.z + emission;
}
