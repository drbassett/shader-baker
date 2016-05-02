#version 330

out vec3 vertColor;

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(-0.5f, -0.5f),
        vec2(0.5f, -0.5f),
        vec2(0.0f, 0.5f));

    const vec3 colors[3] = vec3[3](
        vec3(1.0f, 0.0f, 0.0f),
        vec3(0.0f, 1.0f, 0.0f),
        vec3(0.0f, 0.0f, 1.0f));

    gl_Position = vec4(positions[gl_VertexID], 0.0f, 1.0f);
    vertColor = colors[gl_VertexID];
}
