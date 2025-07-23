#version 430 core
out vec4 o_color;

in VS_OUT {
    vec2 texCoords;
    vec3 fragPos;
    vec3 normal;
} fs_in;

layout (binding = 0) uniform samplerCube u_flowMap;
layout (binding = 1) uniform sampler2D u_texture;
uniform bool u_showFlow;
uniform bool u_hdrFlowMap;
uniform float u_time;

const float flowIntencity = 0.05;

void main() 
{
    vec2 flow = texture(u_flowMap, fs_in.fragPos).rg;
    if(!u_hdrFlowMap)
    {
        flow = flow * 2 - 1;
    }

    // if(u_showFlow)
    // {
        o_color.rgb = vec3(flow, 0);
    // }
    // else
    // {
    //     float p1 = fract(u_time);
    //     float p2 = fract(p1 + 0.5);
    //     float flow_mix = abs((p1 - 0.5) * 2.0);

    //     vec3 main_tex1 = texture(u_texture, fs_in.texCoords + (flow * p1 * flowIntencity)).rgb;
    //     vec3 main_tex2 = texture(u_texture, fs_in.texCoords + (flow * p2 * flowIntencity)).rgb;
    //     vec3 main_tex_mix = mix(main_tex1, main_tex2, flow_mix);
    //     o_color.rgb = main_tex_mix;
    // }

    o_color.a = 1;
}
