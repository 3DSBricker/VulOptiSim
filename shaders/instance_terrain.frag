#version 450

layout(set = 1, binding = 1) uniform sampler2DArray texture_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in flat float in_tex_index;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 tex_color = texture(texture_sampler, vec3(in_uv, in_tex_index));

    if (tex_color.a < 0.01) {
        discard;
    }

    out_color = tex_color;
}
