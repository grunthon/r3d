/* color.glsl -- Color math functions.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

vec3 C_LinearToSrgb(vec3 color)
{
    // Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    return max(vec3(1.055) * pow(color, vec3(0.416666667)) - vec3(0.055), vec3(0.0));
}

vec4 C_LinearToSrgb(vec4 color)
{
    return vec4(C_LinearToSrgb(color.rgb), color.a);
}

vec3 C_SrgbToLinear(vec3 color)
{
    // Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

vec4 C_SrgbToLinear(vec4 color)
{
    return vec4(C_SrgbToLinear(color.rgb), color.a);
}
