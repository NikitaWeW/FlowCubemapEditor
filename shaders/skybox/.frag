#version 430 core

in vec3 v_texCoords;
layout (binding = 0) uniform samplerCube u_skybox;
out vec4 o_color;

void main() {
    o_color = texture(u_skybox, v_texCoords);
    o_color = vec4(1.0 - exp(-o_color.rgb * /* exposure -- */ 1), o_color.a); // tone mapping
}