#version 430 core
out vec4 o_color;

in VS_OUT {
    vec2 texCoords;
    vec3 fragPos;
} fs_in;

layout (binding = 0) uniform samplerCube u_flowMap;
layout (binding = 1) uniform sampler2D u_texture;
layout (binding = 2) uniform samplerCube u_cubemapTexture;
uniform bool u_showFlow;
uniform bool u_isTextureCubemap;
uniform bool u_blurPreview = true;
uniform float u_time;
uniform float u_flowIntensity;

const vec3 gridSamplingDisk[20] = vec3[]
(
   vec3(1, 1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1, 1,  1), 
   vec3(1, 1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
   vec3(1, 1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1, 1,  0),
   vec3(1, 0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1, 0, -1),
   vec3(0, 1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0, 1, -1)
);
vec4 textureBlurred(samplerCube cube, vec3 dir, float diskRadius)
{
    dir = normalize(dir);
    vec4 res = vec4(0);
    for(int i = 0; i < gridSamplingDisk.length(); ++i)
    {
        res += texture(cube, dir + normalize(gridSamplingDisk[i]) * diskRadius);
    }
    res /= float(gridSamplingDisk.length());

    return res;
}

void main() 
{
    vec2 flow;
    if(u_blurPreview)
        flow = textureBlurred(u_flowMap, fs_in.fragPos, 0.03).rg;
    else 
        flow = texture(u_flowMap, fs_in.fragPos).rg;

    flow *= u_flowIntensity;

    if(u_showFlow)
    {
        o_color.rgb = vec3(flow, 0);
    }
    else
    {
        float p1 = fract(u_time);
        float p2 = fract(p1 + 0.5);
        float flow_mix = abs((p1 - 0.5) * 2.0);

        vec3 main_tex1;
        vec3 main_tex2;
        if(u_isTextureCubemap)
        {
            vec3 dir = normalize(fs_in.fragPos);
            vec3 worldUp = dot(dir, vec3(0,1,0)) > 0.99 ? vec3(1,0,0) : vec3(0,1,0);
            vec3 right = normalize(cross(dir, worldUp));
            vec3 up = normalize(cross(right, dir));

            main_tex1 = texture(u_cubemapTexture, dir - right * (flow * p1).x + up * (flow * p1).y).rgb;
            main_tex2 = texture(u_cubemapTexture, dir - right * (flow * p2).x + up * (flow * p2).y).rgb;
        } else
        {
            main_tex1 = texture(u_texture, fs_in.texCoords + (flow * p1)).rgb;
            main_tex2 = texture(u_texture, fs_in.texCoords + (flow * p2)).rgb;
        }
        vec3 main_tex_mix = mix(main_tex1, main_tex2, flow_mix);
        o_color.rgb = main_tex_mix;
    }

    o_color.a = 1;
}
