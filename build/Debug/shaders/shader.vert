#version 450

layout(push_constant) uniform vertconstants
{
    mat4 model;
} pcs;

layout(set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
} ubo;


layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in uint intexID;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out uint texID;

void main()
{
    gl_Position = ubo.proj * ubo.view * pcs.model * vec4(inPos, 1);
    texCoord = inTexCoord;
    texID = intexID;
}
