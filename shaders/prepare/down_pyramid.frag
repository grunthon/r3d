/* down_pyramid.frag - GBuffer pyramid downsampling shader
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#version 330 core

// ================================
// Inlucdes
// ================================

#include <ubo/view.glsl>

// ================================
// Out - Fragments
// ================================

layout(location = 0) out float FragDepth;
layout(location = 1) out vec2  FragNormal;
layout(location = 2) out vec3  FragDiffuse;
layout(location = 3) out vec3  FragSpecular;

// ================================
// Samplers & Uniforms
// ================================

uniform sampler2D uDepthTex;
uniform sampler2D uNormalTex;
uniform sampler2D uDiffuseTex;
uniform sampler2D uSpecularTex;

// ================================
// Sampling Offsets
// ================================

const ivec2 OFFSETS[4] = ivec2[4](
    ivec2(0, 0), ivec2(1, 0),
    ivec2(0, 1), ivec2(1, 1)
);

// ================================
// Main Function
// ================================

void main()
{
    ivec2 pixCoord = 2 * ivec2(gl_FragCoord.xy);

    ivec2 p0 = pixCoord + OFFSETS[0];
    ivec2 p1 = pixCoord + OFFSETS[1];
    ivec2 p2 = pixCoord + OFFSETS[2];
    ivec2 p3 = pixCoord + OFFSETS[3];

    float d0 = texelFetch(uDepthTex, p0, 0).r;
    float d1 = texelFetch(uDepthTex, p1, 0).r;
    float d2 = texelFetch(uDepthTex, p2, 0).r;
    float d3 = texelFetch(uDepthTex, p3, 0).r;

    FragDepth = d0;
    uint index = 0u;

    bool useMax = ((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) == 0;

    if (useMax)
    {
        if (d1 > FragDepth) { FragDepth = d1; index = 1u; }
        if (d2 > FragDepth) { FragDepth = d2; index = 2u; }
        if (d3 > FragDepth) { FragDepth = d3; index = 3u; }
    }
    else
    {
        if (d1 < FragDepth) { FragDepth = d1; index = 1u; }
        if (d2 < FragDepth) { FragDepth = d2; index = 2u; }
        if (d3 < FragDepth) { FragDepth = d3; index = 3u; }
    }

    ivec2 srcCoord = pixCoord + OFFSETS[index];

    FragNormal   = texelFetch(uNormalTex,   srcCoord, 0).rg;
    FragDiffuse  = texelFetch(uDiffuseTex,  srcCoord, 0).rgb;
    FragSpecular = texelFetch(uSpecularTex, srcCoord, 0).rgb;

	gl_FragDepth = 1.0 - float(
		(d0 < uView.far) ||
		(d1 < uView.far) ||
		(d2 < uView.far) ||
		(d3 < uView.far)
	);
}
