#version 450

#extension GL_GOOGLE_include_directive: require

#include "common.h"
#include "unpack_attributes.h"

#define PI 3.1415926

layout (triangles) in;
layout (triangle_strip, max_vertices = 8) out;

layout (push_constant) uniform params_t
{
    mat4 projection;
    mat4 model;
} params;

layout (location = 0) in GS_IN
{
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec2 texturePos;
} input_vertices[];

layout (location = 0) out GS_OUT
{
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec2 texturePos;
} output_vertex;

layout (binding = 0, set = 0) uniform AppData {
    UniformParams global_params;
};

void main()
{
    for (uint i = 0; i < 3; i++)
    {
        output_vertex.position = input_vertices[i].position;
        output_vertex.normal = input_vertices[i].normal;
        output_vertex.tangent = input_vertices[i].tangent;
        output_vertex.texturePos = input_vertices[i].texturePos;

        gl_Position = params.projection * vec4(input_vertices[i].position, 1.0);

        EmitVertex();
    }

    EndPrimitive();

    vec3 normal = normalize(cross(
                                input_vertices[2].position - input_vertices[1].position,
                                input_vertices[0].position - input_vertices[1].position));
    vec3 center = (input_vertices[0].position + input_vertices[1].position + input_vertices[2].position) / 3.0;
    vec3 tangent1 = normalize(cross(normal, input_vertices[2].position - input_vertices[1].position));
    vec3 tangent2 = normalize(cross(normal, tangent1));
    vec2 texturePos = input_vertices[0].texturePos;

    for (uint i = 0; i < 3; i++)
    {
        float shift = PI * 2.0 / 3.0 * float(i);

        vec2 surface_offset = vec2(sin(global_params.time + shift), cos(global_params.time + shift));
        surface_offset *= 0.03;

        vec3 offset = normal * 0.03 + tangent1 * surface_offset.x + tangent2 * surface_offset.y;

        output_vertex.position = center + offset;
        output_vertex.normal = normal;
        output_vertex.tangent = tangent1;
        output_vertex.texturePos = texturePos;

        gl_Position = params.projection * vec4(center + offset, 1.0);

        EmitVertex();
    }

    EndPrimitive();
}