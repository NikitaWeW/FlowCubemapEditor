#version 430 core
out vec4 o_color;

in VS_OUT {
    vec2 texCoords;
    vec3 fragPos;
    vec3 normal;
} fs_in;

void main() 
{
    o_color = vec4(fs_in.fragPos, 1);

    o_color.rgb = pow(1 - exp(-o_color.rgb * /*exposure -- */ 1), vec3(1/2.2)); // apply gamma correction and exposure
}
