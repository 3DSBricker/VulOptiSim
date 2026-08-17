#version 450

// 36 vertices voor een volledige kubus (6 faces x 6 vertices)
const vec3 CUBE_POSITIONS[36] = vec3[](
    // FRONT (-Z)
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    vec3(-0.5,  0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    // BACK (+Z)
    vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    // LEFT (-X)
    vec3(-0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    // RIGHT (+X)
    vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    // TOP (+Y)
    vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),
    // BOTTOM (-Y)
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5),
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5, -0.5, -0.5)
);

// UVs herbruikt voor elk van de 6 faces
const vec2 CUBE_UVS[36] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

layout(location = 0) in vec4 in_position_tex; // xyz: center pos, w: tex index
layout(location = 1) in vec4 in_scale_pad;    // xyz: scale (w, h, l), w: pad (niet meer in gebruik)

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out flat float out_tex_index;

void main() {
    // Haal de lokale vertex positie op (nu veilig tot index 35)
    vec3 local_pos = CUBE_POSITIONS[gl_VertexIndex];

    vec3 center    = in_position_tex.xyz;
    float tex_idx  = in_position_tex.w;
    vec3 scale     = in_scale_pad.xyz;

    // Converteer de vertex van [-0.5, 0.5] range naar de juiste dimensies en tel center erbij op
    vec3 world_pos = center + (local_pos * scale);

    gl_Position = ubo.proj * ubo.view * vec4(world_pos, 1.0);

    out_uv = CUBE_UVS[gl_VertexIndex];
    out_tex_index = tex_idx;
}