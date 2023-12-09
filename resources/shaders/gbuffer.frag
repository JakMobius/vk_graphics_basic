#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout(location = 0) out vec3 out_fragCoord;
layout(location = 1) out vec3 out_fragNormal;

layout (location = 0) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} vIn;

void main()
{
    out_fragCoord = vIn.wPos;
    out_fragNormal = normalize(vIn.wNorm);
}