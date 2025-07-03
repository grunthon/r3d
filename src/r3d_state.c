/*
 * Copyright (c) 2025 Le Juez Victor
 *
 * This software is provided "as-is", without any express or implied warranty. In no event
 * will the authors be held liable for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose, including commercial
 * applications, and to alter it and redistribute it freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must not claim that you
 *   wrote the original software. If you use this software in a product, an acknowledgment
 *   in the product documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must not be misrepresented
 *   as being the original software.
 *
 *   3. This notice may not be removed or altered from any source distribution.
 */

#include "./r3d_state.h"

#include <assert.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <glad.h>

#include "./details/misc/r3d_dds_loader_ext.h"
#include "./details/misc/r3d_half.h"

#include "shaders.h"
#include "assets.h"

/* === Global state definition === */

struct R3D_State R3D = { 0 };

/* === Internal Functions === */

static char* r3d_shader_inject_defines(const char* code, const char* defines[], int count)
{
    if (!code) return NULL;

    // Calculate the size of the final buffer
    size_t codeLen = strlen(code);
    size_t definesLen = 0;

    // Calculate the total size of the #define statements
    for (int i = 0; i < count; i++) {
        definesLen += strlen(defines[i]) + 1;  // +1 for '\n'
    }

    // Allocate memory for the new shader
    size_t newSize = codeLen + definesLen + 1;
    char* newShader = (char*)RL_MALLOC(newSize);
    if (!newShader) return NULL;

    const char* versionStart = strstr(code, "#version");
    assert(versionStart && "Shader must have version");

    // Copy everything up to the end of the `#version` line
    const char* afterVersion = strchr(versionStart, '\n');
    if (!afterVersion) afterVersion = versionStart + strlen(versionStart);

    size_t prefix_len = afterVersion - code + 1;
    strncpy(newShader, code, prefix_len);
    newShader[prefix_len] = '\0';

    // Add the `#define` statements
    for (int i = 0; i < count; i++) {
        strcat(newShader, defines[i]);
        strcat(newShader, "\n");
    }

    // Add the rest of the shader after `#version`
    strcat(newShader, afterVersion + 1);

    return newShader;
}

static void r3d_texture_create_hdr(int width, int height)
{
    if (R3D.support.TEX_R11G11B10F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
    }
    else if (R3D.support.TEX_RGB16F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
    }
    else if (R3D.support.TEX_RGB32F) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
    }
    else /* 8-bit fallback - non-HDR */ {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    }
}


/* === Helper functions === */

bool r3d_check_texture_format_support(unsigned int format)
{
    GLint supported = GL_FALSE;
    glGetInternalformativ(GL_TEXTURE_2D, format, GL_INTERNALFORMAT_SUPPORTED, 1, &supported);
    return (supported == GL_TRUE);
}

void r3d_calculate_bloom_prefilter_data()
{
    float knee = R3D.env.bloomThreshold * R3D.env.bloomSoftThreshold;
    R3D.env.bloomPrefilter.x = R3D.env.bloomThreshold;
    R3D.env.bloomPrefilter.y = R3D.env.bloomPrefilter.x - knee;
    R3D.env.bloomPrefilter.z = 2.0f * knee;
    R3D.env.bloomPrefilter.w = 0.25f / (knee + 0.00001f);
}


/* === Main loading functions === */

void r3d_framebuffers_load(int width, int height)
{
    r3d_framebuffer_load_gbuffer(width, height);
    r3d_framebuffer_load_deferred(width, height);
    r3d_framebuffer_load_scene(width, height);
    r3d_framebuffer_load_pingpong_post(width, height);

    if (R3D.env.ssaoEnabled) {
        r3d_framebuffer_load_pingpong_ssao(width, height);
    }

    if (R3D.env.bloomMode != R3D_BLOOM_DISABLED) {
        r3d_framebuffer_load_mipchain_bloom(width, height);
    }
}

void r3d_framebuffers_unload(void)
{
    r3d_framebuffer_unload_gbuffer();
    r3d_framebuffer_unload_deferred();
    r3d_framebuffer_unload_scene();
    r3d_framebuffer_unload_pingpong_post();

    if (R3D.framebuffer.pingPongSSAO.id != 0) {
        r3d_framebuffer_unload_pingpong_ssao();
    }

    if (R3D.framebuffer.mipChainBloom.id != 0) {
        r3d_framebuffer_unload_mipchain_bloom();
    }
}

void r3d_textures_load(void)
{
    r3d_texture_load_white();
    r3d_texture_load_black();
    r3d_texture_load_normal();
    r3d_texture_load_blue_noise();
    r3d_texture_load_ibl_brdf_lut();

    if (R3D.env.ssaoEnabled) {
        r3d_texture_load_ssao_noise();
        r3d_texture_load_ssao_kernel();
    }
}

void r3d_textures_unload(void)
{
    rlUnloadTexture(R3D.texture.white);
    rlUnloadTexture(R3D.texture.black);
    rlUnloadTexture(R3D.texture.normal);
    rlUnloadTexture(R3D.texture.blueNoise);
    rlUnloadTexture(R3D.texture.iblBrdfLut);

    if (R3D.texture.ssaoNoise != 0) {
        rlUnloadTexture(R3D.texture.ssaoNoise);
    }

    if (R3D.texture.ssaoKernel != 0) {
        rlUnloadTexture(R3D.texture.ssaoKernel);
    }
}

void r3d_shaders_load(void)
{
    // Load generation shaders
    r3d_shader_load_generate_cubemap_from_equirectangular();
    r3d_shader_load_generate_irradiance_convolution();
    r3d_shader_load_generate_prefilter();

    // Load raster shaders
    r3d_shader_load_raster_geometry();
    r3d_shader_load_raster_geometry_inst();
    r3d_shader_load_raster_forward();
    r3d_shader_load_raster_forward_inst();
    r3d_shader_load_raster_skybox();
    r3d_shader_load_raster_depth();
    r3d_shader_load_raster_depth_inst();
    r3d_shader_load_raster_depth_cube();
    r3d_shader_load_raster_depth_cube_inst();

    // Load screen shaders
    r3d_shader_load_screen_ambient_ibl();
    r3d_shader_load_screen_ambient();
    r3d_shader_load_screen_lighting();
    r3d_shader_load_screen_scene();
    r3d_shader_load_screen_tonemap();
    r3d_shader_load_screen_adjustment();

    if (R3D.env.ssaoEnabled) {
        r3d_shader_load_generate_gaussian_blur_dual_pass();
        r3d_shader_load_screen_ssao();
    }
    if (R3D.env.bloomMode != R3D_BLOOM_DISABLED) {
        r3d_shader_load_generate_downsampling();
        r3d_shader_load_generate_upsampling();
        r3d_shader_load_screen_bloom();
    }
    if (R3D.env.fogMode != R3D_FOG_DISABLED) {
        r3d_shader_load_screen_fog();
    }
    if (R3D.state.flags & R3D_FLAG_FXAA) {
        r3d_shader_load_screen_fxaa();
    }
}

void r3d_shaders_unload(void)
{
    // Unload generation shaders
    rlUnloadShaderProgram(R3D.shader.generate.gaussianBlurDualPass.id);
    rlUnloadShaderProgram(R3D.shader.generate.cubemapFromEquirectangular.id);
    rlUnloadShaderProgram(R3D.shader.generate.irradianceConvolution.id);
    rlUnloadShaderProgram(R3D.shader.generate.prefilter.id);

    // Unload raster shaders
    rlUnloadShaderProgram(R3D.shader.raster.geometry.id);
    rlUnloadShaderProgram(R3D.shader.raster.geometryInst.id);
    rlUnloadShaderProgram(R3D.shader.raster.forward.id);
    rlUnloadShaderProgram(R3D.shader.raster.forwardInst.id);
    rlUnloadShaderProgram(R3D.shader.raster.skybox.id);
    rlUnloadShaderProgram(R3D.shader.raster.depth.id);
    rlUnloadShaderProgram(R3D.shader.raster.depthInst.id);
    rlUnloadShaderProgram(R3D.shader.raster.depthCube.id);
    rlUnloadShaderProgram(R3D.shader.raster.depthCubeInst.id);

    // Unload screen shaders
    rlUnloadShaderProgram(R3D.shader.screen.ambientIbl.id);
    rlUnloadShaderProgram(R3D.shader.screen.ambient.id);
    rlUnloadShaderProgram(R3D.shader.screen.lighting.id);
    rlUnloadShaderProgram(R3D.shader.screen.scene.id);
    rlUnloadShaderProgram(R3D.shader.screen.tonemap.id);
    rlUnloadShaderProgram(R3D.shader.screen.adjustment.id);

    if (R3D.shader.screen.ssao.id != 0) {
        rlUnloadShaderProgram(R3D.shader.screen.ssao.id);
    }
    if (R3D.shader.screen.bloom.id != 0) {
        rlUnloadShaderProgram(R3D.shader.screen.bloom.id);
    }
    if (R3D.shader.screen.fog.id != 0) {
        rlUnloadShaderProgram(R3D.shader.screen.fog.id);
    }
    if (R3D.shader.screen.fxaa.id != 0) {
        rlUnloadShaderProgram(R3D.shader.screen.fxaa.id);
    }
}


/* === Framebuffer loading functions === */

void r3d_framebuffer_load_gbuffer(int width, int height)
{
    struct r3d_fb_gbuffer_t* gBuffer = &R3D.framebuffer.gBuffer;

    gBuffer->id = rlLoadFramebuffer();
    if (gBuffer->id == 0) {
        TraceLog(LOG_WARNING, "Failed to create framebuffer");
    }

    rlEnableFramebuffer(gBuffer->id);

    // Generate (albedo / orm) buffers
    gBuffer->albedo = rlLoadTexture(NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8, 1);
    gBuffer->orm = rlLoadTexture(NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, 1);

    // Generate emission buffer
    glGenTextures(1, &gBuffer->emission);
    glBindTexture(GL_TEXTURE_2D, gBuffer->emission);

    r3d_texture_create_hdr(width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // We generate the normal buffer here.
    // The setup for the normal buffer requires direct API calls
    // since RLGL does not support the creation of 16-bit two-component textures.
    // Normals will be encoded and decoded using octahedral mapping for efficient storage and reconstruction.
    glGenTextures(1, &gBuffer->normal);
    glBindTexture(GL_TEXTURE_2D, gBuffer->normal);
    if ((R3D.state.flags & R3D_FLAG_8_BIT_NORMALS) || (R3D.support.TEX_RG16F == false)) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
    }
    else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, NULL);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Generate depth (+stencil) texture
    glGenTextures(1, &gBuffer->depth);
    glBindTexture(GL_TEXTURE_2D, gBuffer->depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Unbind last texture
    glBindTexture(GL_TEXTURE_2D, 0);

    // Activate the draw buffers for all the attachments
    rlActiveDrawBuffers(R3D_GBUFFER_COUNT);

    // Attach the textures to the framebuffer
    rlFramebufferAttach(gBuffer->id, gBuffer->albedo, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(gBuffer->id, gBuffer->emission, RL_ATTACHMENT_COLOR_CHANNEL1, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(gBuffer->id, gBuffer->normal, RL_ATTACHMENT_COLOR_CHANNEL2, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(gBuffer->id, gBuffer->orm, RL_ATTACHMENT_COLOR_CHANNEL3, RL_ATTACHMENT_TEXTURE2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer->id); // rlFramebufferAttach unbind the framebuffer...
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gBuffer->depth, 0);

    // Check if the framebuffer is complete
    if (!rlFramebufferComplete(gBuffer->id)) {
        TraceLog(LOG_WARNING, "Framebuffer is not complete");
    }
}

void r3d_framebuffer_load_pingpong_ssao(int width, int height)
{
    struct r3d_fb_pingpong_ssao_t* ssao = &R3D.framebuffer.pingPongSSAO;

    width /= 2, height /= 2;

    ssao->id = rlLoadFramebuffer();
    if (ssao->id == 0) {
        TraceLog(LOG_WARNING, "Failed to create framebuffer");
    }

    rlEnableFramebuffer(ssao->id);

    // Generate (ssao) buffers
    GLuint textures[2];
    glGenTextures(2, textures);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    ssao->target = textures[0];
    ssao->source = textures[1];

    // Activate the draw buffers for all the attachments
    rlActiveDrawBuffers(1);

    // Attach the textures to the framebuffer
    rlFramebufferAttach(ssao->id, ssao->target, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    // Check if the framebuffer is complete
    if (!rlFramebufferComplete(ssao->id)) {
        TraceLog(LOG_WARNING, "Framebuffer is not complete");
    }
}

void r3d_framebuffer_load_deferred(int width, int height)
{
    struct r3d_fb_deferred_t* deferred = &R3D.framebuffer.deferred;

    deferred->id = rlLoadFramebuffer();
    if (deferred->id == 0) {
        TraceLog(LOG_WARNING, "Failed to create framebuffer");
    }

    rlEnableFramebuffer(deferred->id);

    // Generate diffuse/specular textures
    GLuint textures[2];
    glGenTextures(2, textures);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        r3d_texture_create_hdr(width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    deferred->diffuse = textures[0];
    deferred->specular = textures[1];

    // Activate the draw buffers for all the attachments
    rlActiveDrawBuffers(2);

    // Attach the textures to the framebuffer
    rlFramebufferAttach(deferred->id, deferred->diffuse, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(deferred->id, deferred->specular, RL_ATTACHMENT_COLOR_CHANNEL1, RL_ATTACHMENT_TEXTURE2D, 0);

    // Check if the framebuffer is complete
    if (!rlFramebufferComplete(deferred->id)) {
        TraceLog(LOG_WARNING, "Framebuffer is not complete");
    }
}

void r3d_framebuffer_load_scene(int width, int height)
{
    struct r3d_fb_scene_t* scene = &R3D.framebuffer.scene;

    scene->id = rlLoadFramebuffer();
    if (scene->id == 0) {
        TraceLog(LOG_WARNING, "Failed to create framebuffer");
    }

    rlEnableFramebuffer(scene->id);

    // Generate color texture
    glGenTextures(1, &scene->color);
    glBindTexture(GL_TEXTURE_2D, scene->color);

    r3d_texture_create_hdr(width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Attach the depth-stencil buffer from the G-buffer
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_TEXTURE_2D, R3D.framebuffer.gBuffer.depth, 0
    );

    // Activate the draw buffers for all the attachments
    rlActiveDrawBuffers(1);

    // Attach the textures to the framebuffer
    rlFramebufferAttach(scene->id, scene->color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    // Check if the framebuffer is complete
    if (!rlFramebufferComplete(scene->id)) {
        TraceLog(LOG_WARNING, "Framebuffer is not complete");
    }
}

void r3d_framebuffer_load_mipchain_bloom(int width, int height)
{
    struct r3d_fb_mipchain_bloom_t* bloom = &R3D.framebuffer.mipChainBloom;

    glGenFramebuffers(1, &bloom->id);
    glBindFramebuffer(GL_FRAMEBUFFER, bloom->id);

    int mipChainLength = (int)(floor(log2(fminf(width, height))) * R3D.env.bloomMipChainScale);
    if (mipChainLength < 1) mipChainLength = 1;

    bloom->mipChain = MemAlloc(mipChainLength * sizeof(struct r3d_mip_bloom_t));
    if (bloom->mipChain == NULL) {
        TraceLog(LOG_ERROR, "R3D: Failed to allocate memory to store bloom mip chain");
    }

    bloom->mipCount = mipChainLength;

    int iMipW = width;
    int iMipH = height;
    float fMipW = (float)iMipW;
    float fMipH = (float)iMipH;

    for (GLuint i = 0; i < mipChainLength; i++) {

        struct r3d_mip_bloom_t* mip = &bloom->mipChain[i];
    
        iMipW /= 2;
        iMipH /= 2;
        fMipW *= 0.5f;
        fMipH *= 0.5f;

        mip->iW = iMipW;
        mip->iH = iMipH;
        mip->fW = fMipW;
        mip->fH = fMipH;

        glGenTextures(1, &mip->id);
        glBindTexture(GL_TEXTURE_2D, mip->id);

        // we are downscaling an HDR color buffer, so we need a float texture format
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, iMipW, iMipH, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    }

    // Attach first mip to the framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom->mipChain[0].id, 0);

    GLenum attachments[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        TraceLog(LOG_WARNING, "Framebuffer is not complete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void r3d_framebuffer_load_pingpong_post(int width, int height)
{
    struct r3d_fb_pingpong_post_t* post = &R3D.framebuffer.post;

    post->id = rlLoadFramebuffer();
    if (post->id == 0) {
        TraceLog(LOG_WARNING, "Failed to create framebuffer");
    }

    rlEnableFramebuffer(post->id);

    // Generate (color) buffers
    GLuint textures[2];
    glGenTextures(2, textures);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        r3d_texture_create_hdr(width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    post->target = textures[0];
    post->source = textures[1];

    // Activate the draw buffers for all the attachments
    rlActiveDrawBuffers(1);

    // Attach the textures to the framebuffer
    rlFramebufferAttach(post->id, post->target, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    // Check if the framebuffer is complete
    if (!rlFramebufferComplete(post->id)) {
        TraceLog(LOG_WARNING, "Framebuffer is not complete");
    }
}

void r3d_framebuffer_unload_gbuffer(void)
{
    struct r3d_fb_gbuffer_t* gBuffer = &R3D.framebuffer.gBuffer;

    rlUnloadTexture(gBuffer->albedo);
    rlUnloadTexture(gBuffer->emission);
    rlUnloadTexture(gBuffer->normal);
    rlUnloadTexture(gBuffer->orm);
    rlUnloadTexture(gBuffer->depth);

    rlUnloadFramebuffer(gBuffer->id);

    memset(gBuffer, 0, sizeof(struct r3d_fb_gbuffer_t));
}

void r3d_framebuffer_unload_pingpong_ssao(void)
{
    struct r3d_fb_pingpong_ssao_t* ssao = &R3D.framebuffer.pingPongSSAO;

    rlUnloadTexture(ssao->source);
    rlUnloadTexture(ssao->target);

    rlUnloadFramebuffer(ssao->id);

    memset(ssao, 0, sizeof(struct r3d_fb_pingpong_ssao_t));
}

void r3d_framebuffer_unload_deferred(void)
{
    struct r3d_fb_deferred_t* deferred = &R3D.framebuffer.deferred;

    rlUnloadTexture(deferred->diffuse);
    rlUnloadTexture(deferred->specular);

    rlUnloadFramebuffer(deferred->id);

    memset(deferred, 0, sizeof(struct r3d_fb_deferred_t));
}

void r3d_framebuffer_unload_scene(void)
{
    struct r3d_fb_scene_t* scene = &R3D.framebuffer.scene;

    rlUnloadTexture(scene->color);

    rlUnloadFramebuffer(scene->id);

    memset(scene, 0, sizeof(struct r3d_fb_scene_t));
}

void r3d_framebuffer_unload_mipchain_bloom(void)
{
    struct r3d_fb_mipchain_bloom_t* bloom = &R3D.framebuffer.mipChainBloom;

    for (int i = 0; i < bloom->mipCount; i++) {
        glDeleteTextures(1, &bloom->mipChain[i].id);
    }
    glDeleteFramebuffers(1, &bloom->id);

    MemFree(bloom->mipChain);

    bloom->mipChain = NULL;
    bloom->mipCount = 0;
    bloom->id = 0;
}

void r3d_framebuffer_unload_pingpong_post(void)
{
    struct r3d_fb_pingpong_post_t* post = &R3D.framebuffer.post;

    rlUnloadTexture(post->source);
    rlUnloadTexture(post->target);

    rlUnloadFramebuffer(post->id);

    memset(post, 0, sizeof(struct r3d_fb_pingpong_post_t));
}


/* === Shader loading functions === */

void r3d_shader_load_generate_gaussian_blur_dual_pass(void)
{
    R3D.shader.generate.gaussianBlurDualPass.id = rlLoadShaderCode(
        SCREEN_VERT, GAUSSIAN_BLUR_DUAL_PASS_FRAG
    );

    r3d_shader_get_location(generate.gaussianBlurDualPass, uTexture);
    r3d_shader_get_location(generate.gaussianBlurDualPass, uTexelDir);

    r3d_shader_enable(generate.gaussianBlurDualPass);
    r3d_shader_set_sampler2D_slot(generate.gaussianBlurDualPass, uTexture, 0);
    r3d_shader_disable();
}

void r3d_shader_load_generate_downsampling(void)
{
    R3D.shader.generate.downsampling.id = rlLoadShaderCode(
        SCREEN_VERT, DOWNSAMPLING_FRAG
    );

    r3d_shader_get_location(generate.downsampling, uTexture);
    r3d_shader_get_location(generate.downsampling, uResolution);
    r3d_shader_get_location(generate.downsampling, uMipLevel);
    r3d_shader_get_location(generate.downsampling, uPrefilter);

    r3d_shader_enable(generate.downsampling);
    r3d_shader_set_sampler2D_slot(generate.downsampling, uTexture, 0);
    r3d_shader_disable();
}

void r3d_shader_load_generate_upsampling(void)
{
    R3D.shader.generate.upsampling.id = rlLoadShaderCode(
        SCREEN_VERT, UPSAMPLING_FRAG
    );

    r3d_shader_get_location(generate.upsampling, uTexture);
    r3d_shader_get_location(generate.upsampling, uFilterRadius);

    r3d_shader_enable(generate.upsampling);
    r3d_shader_set_sampler2D_slot(generate.upsampling, uTexture, 0);
    r3d_shader_disable();
}

void r3d_shader_load_generate_cubemap_from_equirectangular(void)
{
    R3D.shader.generate.cubemapFromEquirectangular.id = rlLoadShaderCode(
        CUBEMAP_VERT, CUBEMAP_FROM_EQUIRECTANGULAR_FRAG
    );

    r3d_shader_get_location(generate.cubemapFromEquirectangular, uMatProj);
    r3d_shader_get_location(generate.cubemapFromEquirectangular, uMatView);
    r3d_shader_get_location(generate.cubemapFromEquirectangular, uTexEquirectangular);

    r3d_shader_enable(generate.cubemapFromEquirectangular);
    r3d_shader_set_sampler2D_slot(generate.cubemapFromEquirectangular, uTexEquirectangular, 0);
    r3d_shader_disable();
}

void r3d_shader_load_generate_irradiance_convolution(void)
{
    R3D.shader.generate.irradianceConvolution.id = rlLoadShaderCode(
        CUBEMAP_VERT, IRRADIANCE_CONVOLUTION_FRAG
    );

    r3d_shader_get_location(generate.irradianceConvolution, uMatProj);
    r3d_shader_get_location(generate.irradianceConvolution, uMatView);
    r3d_shader_get_location(generate.irradianceConvolution, uCubemap);

    r3d_shader_enable(generate.irradianceConvolution);
    r3d_shader_set_samplerCube_slot(generate.irradianceConvolution, uCubemap, 0);
    r3d_shader_disable();
}

void r3d_shader_load_generate_prefilter(void)
{
    R3D.shader.generate.prefilter.id = rlLoadShaderCode(
        CUBEMAP_VERT, PREFILTER_FRAG
    );

    r3d_shader_get_location(generate.prefilter, uMatProj);
    r3d_shader_get_location(generate.prefilter, uMatView);
    r3d_shader_get_location(generate.prefilter, uCubemap);
    r3d_shader_get_location(generate.prefilter, uRoughness);

    r3d_shader_enable(generate.prefilter);
    r3d_shader_set_samplerCube_slot(generate.prefilter, uCubemap, 0);
    r3d_shader_disable();
}

void r3d_shader_load_raster_geometry(void)
{
    R3D.shader.raster.geometry.id = rlLoadShaderCode(
        GEOMETRY_VERT, GEOMETRY_FRAG
    );

    r3d_shader_get_location(raster.geometry, uMatNormal);
    r3d_shader_get_location(raster.geometry, uMatModel);
    r3d_shader_get_location(raster.geometry, uMatMVP);
    r3d_shader_get_location(raster.geometry, uTexCoordOffset);
    r3d_shader_get_location(raster.geometry, uTexCoordScale);
    r3d_shader_get_location(raster.geometry, uTexAlbedo);
    r3d_shader_get_location(raster.geometry, uTexNormal);
    r3d_shader_get_location(raster.geometry, uTexEmission);
    r3d_shader_get_location(raster.geometry, uTexOcclusion);
    r3d_shader_get_location(raster.geometry, uTexRoughness);
    r3d_shader_get_location(raster.geometry, uTexMetalness);
    r3d_shader_get_location(raster.geometry, uValEmission);
    r3d_shader_get_location(raster.geometry, uValOcclusion);
    r3d_shader_get_location(raster.geometry, uValRoughness);
    r3d_shader_get_location(raster.geometry, uValMetalness);
    r3d_shader_get_location(raster.geometry, uColAlbedo);
    r3d_shader_get_location(raster.geometry, uColEmission);

    r3d_shader_enable(raster.geometry);
    r3d_shader_set_sampler2D_slot(raster.geometry, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(raster.geometry, uTexNormal, 1);
    r3d_shader_set_sampler2D_slot(raster.geometry, uTexEmission, 2);
    r3d_shader_set_sampler2D_slot(raster.geometry, uTexOcclusion, 3);
    r3d_shader_set_sampler2D_slot(raster.geometry, uTexRoughness, 4);
    r3d_shader_set_sampler2D_slot(raster.geometry, uTexMetalness, 5);
    r3d_shader_disable();
}

void r3d_shader_load_raster_geometry_inst(void)
{
    R3D.shader.raster.geometryInst.id = rlLoadShaderCode(
        GEOMETRY_INSTANCED_VERT, GEOMETRY_FRAG
    );

    r3d_shader_get_location(raster.geometryInst, uMatInvView);
    r3d_shader_get_location(raster.geometryInst, uMatModel);
    r3d_shader_get_location(raster.geometryInst, uMatVP);
    r3d_shader_get_location(raster.geometryInst, uTexCoordOffset);
    r3d_shader_get_location(raster.geometryInst, uTexCoordScale);
    r3d_shader_get_location(raster.geometryInst, uBillboardMode);
    r3d_shader_get_location(raster.geometryInst, uTexAlbedo);
    r3d_shader_get_location(raster.geometryInst, uTexNormal);
    r3d_shader_get_location(raster.geometryInst, uTexEmission);
    r3d_shader_get_location(raster.geometryInst, uTexOcclusion);
    r3d_shader_get_location(raster.geometryInst, uTexRoughness);
    r3d_shader_get_location(raster.geometryInst, uTexMetalness);
    r3d_shader_get_location(raster.geometryInst, uValEmission);
    r3d_shader_get_location(raster.geometryInst, uValOcclusion);
    r3d_shader_get_location(raster.geometryInst, uValRoughness);
    r3d_shader_get_location(raster.geometryInst, uValMetalness);
    r3d_shader_get_location(raster.geometryInst, uColAlbedo);
    r3d_shader_get_location(raster.geometryInst, uColEmission);

    r3d_shader_enable(raster.geometryInst);
    r3d_shader_set_sampler2D_slot(raster.geometryInst, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(raster.geometryInst, uTexNormal, 1);
    r3d_shader_set_sampler2D_slot(raster.geometryInst, uTexEmission, 2);
    r3d_shader_set_sampler2D_slot(raster.geometryInst, uTexOcclusion, 3);
    r3d_shader_set_sampler2D_slot(raster.geometryInst, uTexRoughness, 4);
    r3d_shader_set_sampler2D_slot(raster.geometryInst, uTexMetalness, 5);
    r3d_shader_disable();
}

void r3d_shader_load_raster_forward(void)
{
    R3D.shader.raster.forward.id = rlLoadShaderCode(
        FORWARD_VERT, FORWARD_FRAG
    );

    r3d_shader_raster_forward_t* shader = &R3D.shader.raster.forward;

    r3d_shader_get_location(raster.forward, uMatNormal);
    r3d_shader_get_location(raster.forward, uMatModel);
    r3d_shader_get_location(raster.forward, uMatMVP);
    r3d_shader_get_location(raster.forward, uTexCoordOffset);
    r3d_shader_get_location(raster.forward, uTexCoordScale);
    r3d_shader_get_location(raster.forward, uTexAlbedo);
    r3d_shader_get_location(raster.forward, uTexEmission);
    r3d_shader_get_location(raster.forward, uTexNormal);
    r3d_shader_get_location(raster.forward, uTexOcclusion);
    r3d_shader_get_location(raster.forward, uTexRoughness);
    r3d_shader_get_location(raster.forward, uTexMetalness);
    r3d_shader_get_location(raster.forward, uTexNoise);
    r3d_shader_get_location(raster.forward, uValEmission);
    r3d_shader_get_location(raster.forward, uValOcclusion);
    r3d_shader_get_location(raster.forward, uValRoughness);
    r3d_shader_get_location(raster.forward, uValMetalness);
    r3d_shader_get_location(raster.forward, uColAmbient);
    r3d_shader_get_location(raster.forward, uColAlbedo);
    r3d_shader_get_location(raster.forward, uColEmission);
    r3d_shader_get_location(raster.forward, uCubeIrradiance);
    r3d_shader_get_location(raster.forward, uCubePrefilter);
    r3d_shader_get_location(raster.forward, uTexBrdfLut);
    r3d_shader_get_location(raster.forward, uQuatSkybox);
    r3d_shader_get_location(raster.forward, uHasSkybox);
    r3d_shader_get_location(raster.forward, uAlphaScissorThreshold);
    r3d_shader_get_location(raster.forward, uViewPosition);

    r3d_shader_enable(raster.forward);

    r3d_shader_set_sampler2D_slot(raster.forward, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexEmission, 1);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexNormal, 2);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexOcclusion, 3);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexRoughness, 4);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexMetalness, 5);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexNoise, 6);
    r3d_shader_set_samplerCube_slot(raster.forward, uCubeIrradiance, 7);
    r3d_shader_set_samplerCube_slot(raster.forward, uCubePrefilter, 8);
    r3d_shader_set_sampler2D_slot(raster.forward, uTexBrdfLut, 9);

    int shadowMapSlot = 10;
    for (int i = 0; i < R3D_SHADER_FORWARD_NUM_LIGHTS; i++) {
        shader->uMatLightVP[i].loc = rlGetLocationUniform(shader->id, TextFormat("uMatLightVP[%i]", i));
        shader->uLights[i].shadowMap.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowMap", i));
        shader->uLights[i].shadowCubemap.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowCubemap", i));
        shader->uLights[i].color.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].color", i));
        shader->uLights[i].position.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].position", i));
        shader->uLights[i].direction.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].direction", i));
        shader->uLights[i].specular.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].specular", i));
        shader->uLights[i].energy.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].energy", i));
        shader->uLights[i].range.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].range", i));
        shader->uLights[i].near.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].near", i));
        shader->uLights[i].far.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].far", i));
        shader->uLights[i].attenuation.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].attenuation", i));
        shader->uLights[i].innerCutOff.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].innerCutOff", i));
        shader->uLights[i].outerCutOff.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].outerCutOff", i));
        shader->uLights[i].shadowSoftness.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowSoftness", i));
        shader->uLights[i].shadowMapTxlSz.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowMapTxlSz", i));
        shader->uLights[i].shadowBias.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowBias", i));
        shader->uLights[i].type.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].type", i));
        shader->uLights[i].enabled.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].enabled", i));
        shader->uLights[i].shadow.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadow", i));

        r3d_shader_set_sampler2D_slot(raster.forward, uLights[i].shadowMap, shadowMapSlot++);
        r3d_shader_set_samplerCube_slot(raster.forward, uLights[i].shadowCubemap, shadowMapSlot++);
    }

    r3d_shader_disable();
}

void r3d_shader_load_raster_forward_inst(void)
{
    R3D.shader.raster.forwardInst.id = rlLoadShaderCode(
        FORWARD_INSTANCED_VERT, FORWARD_FRAG
    );

    r3d_shader_raster_forward_inst_t* shader = &R3D.shader.raster.forwardInst;

    r3d_shader_get_location(raster.forwardInst, uMatInvView);
    r3d_shader_get_location(raster.forwardInst, uMatModel);
    r3d_shader_get_location(raster.forwardInst, uMatVP);
    r3d_shader_get_location(raster.forwardInst, uTexCoordOffset);
    r3d_shader_get_location(raster.forwardInst, uTexCoordScale);
    r3d_shader_get_location(raster.forwardInst, uBillboardMode);
    r3d_shader_get_location(raster.forwardInst, uTexAlbedo);
    r3d_shader_get_location(raster.forwardInst, uTexEmission);
    r3d_shader_get_location(raster.forwardInst, uTexNormal);
    r3d_shader_get_location(raster.forwardInst, uTexOcclusion);
    r3d_shader_get_location(raster.forwardInst, uTexRoughness);
    r3d_shader_get_location(raster.forwardInst, uTexMetalness);
    r3d_shader_get_location(raster.forwardInst, uTexNoise);
    r3d_shader_get_location(raster.forwardInst, uValEmission);
    r3d_shader_get_location(raster.forwardInst, uValOcclusion);
    r3d_shader_get_location(raster.forwardInst, uValRoughness);
    r3d_shader_get_location(raster.forwardInst, uValMetalness);
    r3d_shader_get_location(raster.forwardInst, uColAmbient);
    r3d_shader_get_location(raster.forwardInst, uColAlbedo);
    r3d_shader_get_location(raster.forwardInst, uColEmission);
    r3d_shader_get_location(raster.forwardInst, uCubeIrradiance);
    r3d_shader_get_location(raster.forwardInst, uCubePrefilter);
    r3d_shader_get_location(raster.forwardInst, uTexBrdfLut);
    r3d_shader_get_location(raster.forwardInst, uQuatSkybox);
    r3d_shader_get_location(raster.forwardInst, uHasSkybox);
    r3d_shader_get_location(raster.forwardInst, uAlphaScissorThreshold);
    r3d_shader_get_location(raster.forwardInst, uViewPosition);

    r3d_shader_enable(raster.forwardInst);

    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexEmission, 1);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexNormal, 2);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexOcclusion, 3);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexRoughness, 4);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexMetalness, 5);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexNoise, 6);
    r3d_shader_set_samplerCube_slot(raster.forwardInst, uCubeIrradiance, 7);
    r3d_shader_set_samplerCube_slot(raster.forwardInst, uCubePrefilter, 8);
    r3d_shader_set_sampler2D_slot(raster.forwardInst, uTexBrdfLut, 9);

    int shadowMapSlot = 10;
    for (int i = 0; i < R3D_SHADER_FORWARD_NUM_LIGHTS; i++) {
        shader->uMatLightVP[i].loc = rlGetLocationUniform(shader->id, TextFormat("uMatLightVP[%i]", i));
        shader->uLights[i].shadowMap.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowMap", i));
        shader->uLights[i].shadowCubemap.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowCubemap", i));
        shader->uLights[i].color.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].color", i));
        shader->uLights[i].position.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].position", i));
        shader->uLights[i].direction.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].direction", i));
        shader->uLights[i].specular.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].specular", i));
        shader->uLights[i].energy.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].energy", i));
        shader->uLights[i].range.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].range", i));
        shader->uLights[i].near.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].near", i));
        shader->uLights[i].far.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].far", i));
        shader->uLights[i].attenuation.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].attenuation", i));
        shader->uLights[i].innerCutOff.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].innerCutOff", i));
        shader->uLights[i].outerCutOff.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].outerCutOff", i));
        shader->uLights[i].shadowSoftness.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowSoftness", i));
        shader->uLights[i].shadowMapTxlSz.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowMapTxlSz", i));
        shader->uLights[i].shadowBias.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadowBias", i));
        shader->uLights[i].type.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].type", i));
        shader->uLights[i].enabled.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].enabled", i));
        shader->uLights[i].shadow.loc = rlGetLocationUniform(shader->id, TextFormat("uLights[%i].shadow", i));

        r3d_shader_set_sampler2D_slot(raster.forwardInst, uLights[i].shadowMap, shadowMapSlot++);
        r3d_shader_set_samplerCube_slot(raster.forwardInst, uLights[i].shadowCubemap, shadowMapSlot++);
    }

    r3d_shader_disable();
}

void r3d_shader_load_raster_skybox(void)
{
    R3D.shader.raster.skybox.id = rlLoadShaderCode(
        SKYBOX_VERT, SKYBOX_FRAG
    );

    r3d_shader_get_location(raster.skybox, uMatProj);
    r3d_shader_get_location(raster.skybox, uMatView);
    r3d_shader_get_location(raster.skybox, uRotation);
    r3d_shader_get_location(raster.skybox, uCubeSky);

    r3d_shader_enable(raster.skybox);
    r3d_shader_set_samplerCube_slot(raster.skybox, uCubeSky, 0);
    r3d_shader_disable();
}

void r3d_shader_load_raster_depth(void)
{
    R3D.shader.raster.depth.id = rlLoadShaderCode(
        DEPTH_VERT, DEPTH_FRAG
    );

    r3d_shader_get_location(raster.depth, uMatMVP);
    r3d_shader_get_location(raster.depth, uAlpha);
    r3d_shader_get_location(raster.depth, uTexAlbedo);
    r3d_shader_get_location(raster.depth, uAlphaScissorThreshold);
}

void r3d_shader_load_raster_depth_inst(void)
{
    R3D.shader.raster.depthInst.id = rlLoadShaderCode(
        DEPTH_INSTANCED_VERT, DEPTH_FRAG
    );

    r3d_shader_get_location(raster.depthInst, uMatInvView);
    r3d_shader_get_location(raster.depthInst, uMatModel);
    r3d_shader_get_location(raster.depthInst, uMatVP);
    r3d_shader_get_location(raster.depthInst, uBillboardMode);
    r3d_shader_get_location(raster.depthInst, uAlpha);
    r3d_shader_get_location(raster.depthInst, uTexAlbedo);
    r3d_shader_get_location(raster.depthInst, uAlphaScissorThreshold);
}

void r3d_shader_load_raster_depth_cube(void)
{
    R3D.shader.raster.depthCube.id = rlLoadShaderCode(
        DEPTH_CUBE_VERT, DEPTH_CUBE_FRAG
    );

    r3d_shader_get_location(raster.depthCube, uViewPosition);
    r3d_shader_get_location(raster.depthCube, uMatModel);
    r3d_shader_get_location(raster.depthCube, uMatMVP);
    r3d_shader_get_location(raster.depthCube, uFar);
    r3d_shader_get_location(raster.depthCube, uAlpha);
    r3d_shader_get_location(raster.depthCube, uTexAlbedo);
    r3d_shader_get_location(raster.depthCube, uAlphaScissorThreshold);
}

void r3d_shader_load_raster_depth_cube_inst(void)
{
    R3D.shader.raster.depthCubeInst.id = rlLoadShaderCode(
        DEPTH_CUBE_INSTANCED_VERT, DEPTH_CUBE_FRAG
    );

    r3d_shader_get_location(raster.depthCubeInst, uViewPosition);
    r3d_shader_get_location(raster.depthCubeInst, uMatInvView);
    r3d_shader_get_location(raster.depthCubeInst, uMatModel);
    r3d_shader_get_location(raster.depthCubeInst, uMatVP);
    r3d_shader_get_location(raster.depthCubeInst, uFar);
    r3d_shader_get_location(raster.depthCubeInst, uBillboardMode);
    r3d_shader_get_location(raster.depthCubeInst, uAlpha);
    r3d_shader_get_location(raster.depthCubeInst, uTexAlbedo);
    r3d_shader_get_location(raster.depthCubeInst, uAlphaScissorThreshold);
}

void r3d_shader_load_screen_ssao(void)
{
    R3D.shader.screen.ssao.id = rlLoadShaderCode(
        SCREEN_VERT, SSAO_FRAG
    );

    r3d_shader_get_location(screen.ssao, uTexDepth);
    r3d_shader_get_location(screen.ssao, uTexNormal);
    r3d_shader_get_location(screen.ssao, uTexKernel);
    r3d_shader_get_location(screen.ssao, uTexNoise);
    r3d_shader_get_location(screen.ssao, uMatInvProj);
    r3d_shader_get_location(screen.ssao, uMatInvView);
    r3d_shader_get_location(screen.ssao, uMatProj);
    r3d_shader_get_location(screen.ssao, uMatView);
    r3d_shader_get_location(screen.ssao, uResolution);
    r3d_shader_get_location(screen.ssao, uNear);
    r3d_shader_get_location(screen.ssao, uFar);
    r3d_shader_get_location(screen.ssao, uRadius);
    r3d_shader_get_location(screen.ssao, uBias);

    r3d_shader_enable(screen.ssao);
    r3d_shader_set_sampler2D_slot(screen.ssao, uTexDepth, 0);
    r3d_shader_set_sampler2D_slot(screen.ssao, uTexNormal, 1);
    r3d_shader_set_sampler1D_slot(screen.ssao, uTexKernel, 2);
    r3d_shader_set_sampler2D_slot(screen.ssao, uTexNoise, 3);
    r3d_shader_disable();
}

void r3d_shader_load_screen_ambient_ibl(void)
{
    const char* defines[] = { "#define IBL" };
    char* fsCode = r3d_shader_inject_defines(AMBIENT_FRAG, defines, 1);
    R3D.shader.screen.ambientIbl.id = rlLoadShaderCode(SCREEN_VERT, fsCode);

    RL_FREE(fsCode);

    r3d_shader_screen_ambient_ibl_t* shader = &R3D.shader.screen.ambientIbl;

    r3d_shader_get_location(screen.ambientIbl, uTexAlbedo);
    r3d_shader_get_location(screen.ambientIbl, uTexNormal);
    r3d_shader_get_location(screen.ambientIbl, uTexDepth);
    r3d_shader_get_location(screen.ambientIbl, uTexSSAO);
    r3d_shader_get_location(screen.ambientIbl, uTexORM);
    r3d_shader_get_location(screen.ambientIbl, uCubeIrradiance);
    r3d_shader_get_location(screen.ambientIbl, uCubePrefilter);
    r3d_shader_get_location(screen.ambientIbl, uTexBrdfLut);
    r3d_shader_get_location(screen.ambientIbl, uQuatSkybox);
    r3d_shader_get_location(screen.ambientIbl, uViewPosition);
    r3d_shader_get_location(screen.ambientIbl, uMatInvProj);
    r3d_shader_get_location(screen.ambientIbl, uMatInvView);

    r3d_shader_enable(screen.ambientIbl);

    r3d_shader_set_sampler2D_slot(screen.ambientIbl, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(screen.ambientIbl, uTexNormal, 1);
    r3d_shader_set_sampler2D_slot(screen.ambientIbl, uTexDepth, 2);
    r3d_shader_set_sampler2D_slot(screen.ambientIbl, uTexSSAO, 3);
    r3d_shader_set_sampler2D_slot(screen.ambientIbl, uTexORM, 4);

    r3d_shader_set_samplerCube_slot(screen.ambientIbl, uCubeIrradiance, 5);
    r3d_shader_set_samplerCube_slot(screen.ambientIbl, uCubePrefilter, 6);
    r3d_shader_set_sampler2D_slot(screen.ambientIbl, uTexBrdfLut, 7);

    r3d_shader_disable();
}

void r3d_shader_load_screen_ambient(void)
{
    R3D.shader.screen.ambient.id = rlLoadShaderCode(
        SCREEN_VERT, AMBIENT_FRAG
    );

    r3d_shader_get_location(screen.ambient, uTexSSAO);
    r3d_shader_get_location(screen.ambient, uTexORM);
    r3d_shader_get_location(screen.ambient, uColor);

    r3d_shader_enable(screen.ambient);

    r3d_shader_set_sampler2D_slot(screen.ambient, uTexSSAO, 0);
    r3d_shader_set_sampler2D_slot(screen.ambient, uTexORM, 1);

    r3d_shader_disable();
}

void r3d_shader_load_screen_lighting(void)
{
    R3D.shader.screen.lighting.id = rlLoadShaderCode(SCREEN_VERT, LIGHTING_FRAG);
    r3d_shader_screen_lighting_t* shader = &R3D.shader.screen.lighting;

    r3d_shader_get_location(screen.lighting, uTexAlbedo);
    r3d_shader_get_location(screen.lighting, uTexNormal);
    r3d_shader_get_location(screen.lighting, uTexDepth);
    r3d_shader_get_location(screen.lighting, uTexORM);
    r3d_shader_get_location(screen.lighting, uTexNoise);
    r3d_shader_get_location(screen.lighting, uViewPosition);
    r3d_shader_get_location(screen.lighting, uMatInvProj);
    r3d_shader_get_location(screen.lighting, uMatInvView);

    r3d_shader_get_location(screen.lighting, uLight.matVP);
    r3d_shader_get_location(screen.lighting, uLight.shadowMap);
    r3d_shader_get_location(screen.lighting, uLight.shadowCubemap);
    r3d_shader_get_location(screen.lighting, uLight.color);
    r3d_shader_get_location(screen.lighting, uLight.position);
    r3d_shader_get_location(screen.lighting, uLight.direction);
    r3d_shader_get_location(screen.lighting, uLight.specular);
    r3d_shader_get_location(screen.lighting, uLight.energy);
    r3d_shader_get_location(screen.lighting, uLight.range);
    r3d_shader_get_location(screen.lighting, uLight.near);
    r3d_shader_get_location(screen.lighting, uLight.far);
    r3d_shader_get_location(screen.lighting, uLight.attenuation);
    r3d_shader_get_location(screen.lighting, uLight.innerCutOff);
    r3d_shader_get_location(screen.lighting, uLight.outerCutOff);
    r3d_shader_get_location(screen.lighting, uLight.shadowSoftness);
    r3d_shader_get_location(screen.lighting, uLight.shadowMapTxlSz);
    r3d_shader_get_location(screen.lighting, uLight.shadowBias);
    r3d_shader_get_location(screen.lighting, uLight.type);
    r3d_shader_get_location(screen.lighting, uLight.shadow);

    r3d_shader_enable(screen.lighting);

    r3d_shader_set_sampler2D_slot(screen.lighting, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(screen.lighting, uTexNormal, 1);
    r3d_shader_set_sampler2D_slot(screen.lighting, uTexDepth, 2);
    r3d_shader_set_sampler2D_slot(screen.lighting, uTexORM, 3);
    r3d_shader_set_sampler2D_slot(screen.lighting, uTexNoise, 4);

    r3d_shader_set_sampler2D_slot(screen.lighting, uLight.shadowMap, 5);
    r3d_shader_set_samplerCube_slot(screen.lighting, uLight.shadowCubemap, 6);

    r3d_shader_disable();
}

void r3d_shader_load_screen_scene(void)
{
    R3D.shader.screen.scene.id = rlLoadShaderCode(SCREEN_VERT, SCENE_FRAG);
    r3d_shader_screen_scene_t* shader = &R3D.shader.screen.scene;

    r3d_shader_get_location(screen.scene, uTexAlbedo);
    r3d_shader_get_location(screen.scene, uTexEmission);
    r3d_shader_get_location(screen.scene, uTexDiffuse);
    r3d_shader_get_location(screen.scene, uTexSpecular);

    r3d_shader_enable(screen.scene);

    r3d_shader_set_sampler2D_slot(screen.scene, uTexAlbedo, 0);
    r3d_shader_set_sampler2D_slot(screen.scene, uTexEmission, 1);
    r3d_shader_set_sampler2D_slot(screen.scene, uTexDiffuse, 2);
    r3d_shader_set_sampler2D_slot(screen.scene, uTexSpecular, 3);

    r3d_shader_disable();
}

void r3d_shader_load_screen_bloom(void)
{
    R3D.shader.screen.bloom.id = rlLoadShaderCode(
        SCREEN_VERT, BLOOM_FRAG
    );

    r3d_shader_get_location(screen.bloom, uTexColor);
    r3d_shader_get_location(screen.bloom, uTexBloomBlur);
    r3d_shader_get_location(screen.bloom, uBloomMode);
    r3d_shader_get_location(screen.bloom, uBloomIntensity);

    r3d_shader_enable(screen.bloom);
    r3d_shader_set_sampler2D_slot(screen.bloom, uTexColor, 0);
    r3d_shader_set_sampler2D_slot(screen.bloom, uTexBloomBlur, 1);
    r3d_shader_disable();
}

void r3d_shader_load_screen_fog(void)
{
    R3D.shader.screen.fog.id = rlLoadShaderCode(
        SCREEN_VERT, FOG_FRAG
    );

    r3d_shader_get_location(screen.fog, uTexColor);
    r3d_shader_get_location(screen.fog, uTexDepth);
    r3d_shader_get_location(screen.fog, uNear);
    r3d_shader_get_location(screen.fog, uFar);
    r3d_shader_get_location(screen.fog, uFogMode);
    r3d_shader_get_location(screen.fog, uFogColor);
    r3d_shader_get_location(screen.fog, uFogStart);
    r3d_shader_get_location(screen.fog, uFogEnd);
    r3d_shader_get_location(screen.fog, uFogDensity);

    r3d_shader_enable(screen.fog);
    r3d_shader_set_sampler2D_slot(screen.fog, uTexColor, 0);
    r3d_shader_set_sampler2D_slot(screen.fog, uTexDepth, 1);
    r3d_shader_disable();
}

void r3d_shader_load_screen_tonemap(void)
{
    R3D.shader.screen.tonemap.id = rlLoadShaderCode(
        SCREEN_VERT, TONEMAP_FRAG
    );

    r3d_shader_get_location(screen.tonemap, uTexColor);
    r3d_shader_get_location(screen.tonemap, uTonemapMode);
    r3d_shader_get_location(screen.tonemap, uTonemapExposure);
    r3d_shader_get_location(screen.tonemap, uTonemapWhite);

    r3d_shader_enable(screen.tonemap);
    r3d_shader_set_sampler2D_slot(screen.tonemap, uTexColor, 0);
    r3d_shader_disable();
}

void r3d_shader_load_screen_adjustment(void)
{
    R3D.shader.screen.adjustment.id = rlLoadShaderCode(
        SCREEN_VERT, ADJUSTMENT_FRAG
    );

    r3d_shader_get_location(screen.adjustment, uTexColor);
    r3d_shader_get_location(screen.adjustment, uBrightness);
    r3d_shader_get_location(screen.adjustment, uContrast);
    r3d_shader_get_location(screen.adjustment, uSaturation);

    r3d_shader_enable(screen.adjustment);
    r3d_shader_set_sampler2D_slot(screen.adjustment, uTexColor, 0);
    r3d_shader_disable();
}

void r3d_shader_load_screen_fxaa(void)
{
    R3D.shader.screen.fxaa.id = rlLoadShaderCode(
        SCREEN_VERT, FXAA_FRAG
    );

    r3d_shader_get_location(screen.fxaa, uTexture);
    r3d_shader_get_location(screen.fxaa, uTexelSize);

    r3d_shader_enable(screen.fxaa);
    r3d_shader_set_sampler2D_slot(screen.fxaa, uTexture, 0);
    r3d_shader_disable();
}

/* === Texture loading functions === */

void r3d_texture_load_white(void)
{
    static const char DATA = 0xFF;
    R3D.texture.white = rlLoadTexture(&DATA, 1, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
}

void r3d_texture_load_black(void)
{
    static const char DATA = 0x00;
    R3D.texture.black = rlLoadTexture(&DATA, 1, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
}

void r3d_texture_load_normal(void)
{
    static const unsigned char DATA[3] = { 127, 127, 255 };
    R3D.texture.normal = rlLoadTexture(&DATA, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8, 1);
}

void r3d_texture_load_blue_noise(void)
{
    Image image = LoadImageFromMemory(".png", (unsigned char*)BLUE_NOISE_128_PNG, BLUE_NOISE_128_PNG_SIZE);
    R3D.texture.blueNoise = rlLoadTexture(image.data, image.width, image.height, image.format, 1);
}

void r3d_texture_load_ssao_noise(void)
{
#   define R3D_RAND_NOISE_RESOLUTION 4

    r3d_half_t noise[3 * R3D_RAND_NOISE_RESOLUTION * R3D_RAND_NOISE_RESOLUTION] = { 0 };

    for (int i = 0; i < R3D_RAND_NOISE_RESOLUTION * R3D_RAND_NOISE_RESOLUTION; i++) {
        noise[i * 3 + 0] = r3d_cvt_fh(((float)GetRandomValue(0, INT16_MAX) / INT16_MAX) * 2.0f - 1.0f);
        noise[i * 3 + 1] = r3d_cvt_fh(((float)GetRandomValue(0, INT16_MAX) / INT16_MAX) * 2.0f - 1.0f);
        noise[i * 3 + 2] = r3d_cvt_fh((float)GetRandomValue(0, INT16_MAX) / INT16_MAX);
    }

    R3D.texture.ssaoNoise = rlLoadTexture(noise,
        R3D_RAND_NOISE_RESOLUTION,
        R3D_RAND_NOISE_RESOLUTION,
        PIXELFORMAT_UNCOMPRESSED_R16G16B16,
        1
    );
}

void r3d_texture_load_ssao_kernel(void)
{
#   define R3D_SSAO_KERNEL_SIZE 32

    r3d_half_t kernel[3 * R3D_SSAO_KERNEL_SIZE] = { 0 };

    for (int i = 0; i < R3D_SSAO_KERNEL_SIZE; i++)
    {
        Vector3 sample = { 0 };

        sample.x = ((float)GetRandomValue(0, INT16_MAX) / INT16_MAX) * 2.0f - 1.0f;
        sample.y = ((float)GetRandomValue(0, INT16_MAX) / INT16_MAX) * 2.0f - 1.0f;
        sample.z = (float)GetRandomValue(0, INT16_MAX) / INT16_MAX;

        sample = Vector3Normalize(sample);
        sample = Vector3Scale(sample, (float)GetRandomValue(0, INT16_MAX) / INT16_MAX);

        float scale = (float)i / R3D_SSAO_KERNEL_SIZE;
        scale = Lerp(0.1f, 1.0f, scale * scale);
        sample = Vector3Scale(sample, scale);

        kernel[i * 3 + 0] = r3d_cvt_fh(sample.x);
        kernel[i * 3 + 1] = r3d_cvt_fh(sample.y);
        kernel[i * 3 + 2] = r3d_cvt_fh(sample.z);
    }

    glGenTextures(1, &R3D.texture.ssaoKernel);
    glBindTexture(GL_TEXTURE_1D, R3D.texture.ssaoKernel);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB16F, R3D_SSAO_KERNEL_SIZE, 0, GL_RGB, GL_HALF_FLOAT, kernel);

    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
}

void r3d_texture_load_ibl_brdf_lut(void)
{
    // TODO: Review in case 'R3D.support.TEX_RG16F' is false

    Image img = { 0 };

    uint32_t width = 0, height = 0;
    uint32_t special_format_size = 0; // should be 4 or 8 (RG16F or RG32F)

    img.data = r3d_load_dds_from_memory_ext(
        (unsigned char*)IBL_BRDF_256_DDS, IBL_BRDF_256_DDS_SIZE,
        &width, &height, &special_format_size
    );

    img.width = (int)width;
    img.height = (int)height;

    if (img.data && (special_format_size == 4 || special_format_size == 8)) {
        GLuint texId;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);

        GLenum internal_format = (special_format_size == 4) ? GL_RG16F : GL_RG32F;
        GLenum data_type = (special_format_size == 4) ? GL_HALF_FLOAT : GL_FLOAT;

        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, img.width, img.height, 0, GL_RG, data_type, img.data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        R3D.texture.iblBrdfLut = texId;
        RL_FREE(img.data);
    }
    else {
        img = LoadImageFromMemory(".dds", (unsigned char*)IBL_BRDF_256_DDS, IBL_BRDF_256_DDS_SIZE);
        R3D.texture.iblBrdfLut = rlLoadTexture(img.data, img.width, img.height, img.format, img.mipmaps);
        UnloadImage(img);
    }
}
