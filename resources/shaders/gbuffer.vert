#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0 ) out VS_OUT
{
    vec2 texCoord;
} vOut;

vec2 positions[6] = vec2[6](
    vec2(0.0f, 0.0f),
    vec2(0.0f, 1.0f),
    vec2(1.0f, 0.0f),
    vec2(0.0f, 1.0f),
    vec2(1.0f, 0.0f),
    vec2(1.0f, 1.0f)
);

void main()
{
    vec2 position = positions[gl_VertexIndex];
    gl_Position   = vec4(position * 2.0f - vec2(1.0f), 0.0f, 1.0f);
    vOut.texCoord = position;
}