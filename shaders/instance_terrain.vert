#version 450

// Hardcoded base unit quad positions (range [-0.5, 0.5])
const vec2 QUAD_POSITIONS[6] = vec2[](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5, -0.5),
        vec2( 0.5,  0.5)
);

const vec2 QUAD_UVS[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0)
);

layout(location = 0) in vec4 in_position_tex; // xyz: center pos, w: tex index
layout(location = 1) in vec4 in_scale_pad;    // xyz: scale (w, h, l), w: face_type

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out flat float out_tex_index;

void main() {
    vec2 q = QUAD_POSITIONS[gl_VertexIndex]; // q.x in [-0.5, 0.5], q.y in [-0.5, 0.5]

    vec3 center    = in_position_tex.xyz;
    float tex_idx  = in_position_tex.w;
    vec3 scale     = in_scale_pad.xyz;
    int face_type  = int(in_scale_pad.w + 0.5);

    vec3 local_offset = vec3(0.0);

    // 0 = TOP (XZ plane op Y = 0)
    if (face_type == 0) {
        local_offset = vec3(q.x * scale.x, 0.0, q.y * scale.z);
    }
    // 1 = NORTH (+Z muur, strekt over X en Y)
    else if (face_type == 1) {
        local_offset = vec3(q.x * scale.x, q.y * scale.y, 0.0);
    }
    // 2 = SOUTH (-Z muur, strekt over X en Y)
    else if (face_type == 2) {
        local_offset = vec3(-q.x * scale.x, q.y * scale.y, 0.0);
    }
    // 3 = EAST (+X muur, strekt over Z en Y)
    else if (face_type == 3) {
        local_offset = vec3(0.0, q.y * scale.y, q.x * scale.z);
    }
    // 4 = WEST (-X muur, strekt over Z en Y)
    else if (face_type == 4) {
        local_offset = vec3(0.0, q.y * scale.y, -q.x * scale.z);
    }

    vec3 world_pos = center + local_offset;
    gl_Position = ubo.proj * ubo.view * vec4(world_pos, 1.0);

    // UV Coördinaten doorgeven
    vec2 uv = QUAD_UVS[gl_VertexIndex];

    // Optioneel: herhaal de muurtextuur verticaal bij hoge gaten (voorkomt uitrekking)
    if (face_type > 0) {
        float tile_height_unit = scale.x; // Veronderstelt dat tile_width == tile_height
        uv.y *= (scale.y / tile_height_unit);
    }

    out_uv = uv;
    out_tex_index = tex_idx;
}