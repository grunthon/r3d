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

#ifndef R3D_STATE_H
#define R3D_STATE_H

#include "r3d.h"

#include "./details/r3d_shaders.h"
#include "./details/r3d_frustum.h"
#include "./details/r3d_primitives.h"
#include "./details/containers/r3d_array.h"
#include "./details/containers/r3d_registry.h"

/* === Defines === */

#define R3D_GBUFFER_COUNT 4


/* === Global r3d state === */

extern struct R3D_State {

    // GPU Supports
    struct {
        bool TEX_RG16F;
        bool TEX_RGB16F;
        bool TEX_RGB32F;
        bool TEX_R11G11B10F;
    } support;

    // Framebuffers
    struct {

        // G-Buffer
        struct r3d_fb_gbuffer_t {
            unsigned int id;
            unsigned int albedo;            ///< RGB[8|8|8]
            unsigned int emission;          ///< RGB[11|11|10] (if compatible, otherwise 16F || 32F || 8UI)
            unsigned int normal;            ///< RG[16|16] (or) RG[8|8] (R3D_FLAGS_8_BIT_NORMALS or 16F not supported)
            unsigned int orm;               ///< RGB[5|6|5]
            unsigned int depth;             ///< DS[24|8]
        } gBuffer;

        // Ping-pong buffer for SSAO blur processing (half internal resolution)
        struct r3d_fb_pingpong_ssao_t {
            unsigned int id;
            unsigned int source;            ///< R[8] -> Used for initial SSAO rendering + blur effect
            unsigned int target;            ///< R[8] -> Used for initial SSAO rendering + blur effect
        } pingPongSSAO;

        // Deferred lighting
        // Receive in order:
        //  - IBL (from skybox)
        //  - Lit from lights
        struct r3d_fb_deferred_t {
            unsigned int id;
            unsigned int diffuse;           ///< RGB[11|11|10] (or 16F || 32F || 8UI) -> Diffuse contribution
            unsigned int specular;          ///< RGB[11|11|10] (or 16F || 32F || 8UI) -> Specular contribution
        } deferred;

        // Final scene (before post process)
        // Receive in order:
        //  - Environment
        //  - Deferred
        //  - Forward
        struct r3d_fb_scene_t {
            unsigned int id;
            unsigned int color;             ///< RGB[11|11|10] (or 16F || 32F || 8UI) -> Also used for bloom
        } scene;

        // Ping-pong buffer for bloom blur processing (start at half internal resolution)
        struct r3d_fb_mipchain_bloom_t {
            unsigned int id;
            struct r3d_mip_bloom_t {
                unsigned int id;            //< RGB[11|11|10] (or 16F || 32F || 8UI) //< 8UI: Break bloom
                int iW, iH;
                float fW, fH;
            } *mipChain;
            int mipCount;
        } mipChainBloom;

        // Post-processing ping-pong buffer
        struct r3d_fb_pingpong_post_t {
            unsigned int id;
            unsigned int source;            ///< RGB[11|11|10] (or 16F || 32F || 8UI)
            unsigned int target;            ///< RGB[11|11|10] (or 16F || 32F || 8UI)
        } post;

        // Custom target (optional)
        RenderTexture customTarget;

    } framebuffer;

    // Containers
    struct {

        r3d_array_t aDrawDeferred;
        r3d_array_t aDrawDeferredInst;

        r3d_array_t aDrawForward;
        r3d_array_t aDrawForwardInst;

        r3d_registry_t rLights;
        r3d_array_t aLightBatch;

    } container;

    // Internal shaders
    struct {

        // Generation shaders
        struct {
            r3d_shader_generate_gaussian_blur_dual_pass_t gaussianBlurDualPass;
            r3d_shader_generate_downsampling_t downsampling;
            r3d_shader_generate_upsampling_t upsampling;
            r3d_shader_generate_cubemap_from_equirectangular_t cubemapFromEquirectangular;
            r3d_shader_generate_irradiance_convolution_t irradianceConvolution;
            r3d_shader_generate_prefilter_t prefilter;
        } generate;

        // Raster shaders
        struct {
            r3d_shader_raster_geometry_t geometry;
            r3d_shader_raster_geometry_inst_t geometryInst;
            r3d_shader_raster_forward_t forward;
            r3d_shader_raster_forward_inst_t forwardInst;
            r3d_shader_raster_skybox_t skybox;
            r3d_shader_raster_depth_t depth;
            r3d_shader_raster_depth_inst_t depthInst;
            r3d_shader_raster_depth_cube_t depthCube;
            r3d_shader_raster_depth_cube_inst_t depthCubeInst;
        } raster;

        // Screen shaders
        struct {
            r3d_shader_screen_ssao_t ssao;
            r3d_shader_screen_ambient_ibl_t ambientIbl;
            r3d_shader_screen_ambient_t ambient;
            r3d_shader_screen_lighting_t lighting;
            r3d_shader_screen_scene_t scene;
            r3d_shader_screen_bloom_t bloom;
            r3d_shader_screen_fog_t fog;
            r3d_shader_screen_tonemap_t tonemap;
            r3d_shader_screen_adjustment_t adjustment;
            r3d_shader_screen_fxaa_t fxaa;
        } screen;

    } shader;

    // Environment data
    struct {

        Vector3 backgroundColor;    // Used as default albedo color when skybox is disabled (raster pass)
        Vector3 ambientColor;       // Used as default ambient light when skybox is disabled (light pass)

        Quaternion quatSky;         // Rotation of the skybox (raster / light passes)
        R3D_Skybox sky;             // Skybox textures (raster / light passes)
        bool useSky;                // Flag to indicate if skybox is enabled (light pass)

        bool ssaoEnabled;           // (pre-light pass)
        float ssaoRadius;           // (pre-light pass)
        float ssaoBias;             // (pre-light pass)
        int ssaoIterations;         // (pre-light pass)

        R3D_Bloom bloomMode;        // (post pass)
        float bloomIntensity;       // (post pass)
        int bloomFilterRadius;      // (gen pass)
        float bloomMipChainScale;   // (gen pass)
        float bloomThreshold;       // (gen pass)
        float bloomSoftThreshold;   // (gen pass)
        Vector4 bloomPrefilter;     // (gen pass)

        R3D_Fog fogMode;            // (post pass)
        Vector3 fogColor;           // (post pass)
        float fogStart;             // (post pass)
        float fogEnd;               // (post pass)
        float fogDensity;           // (post pass)

        R3D_Tonemap tonemapMode;    // (post pass)
        float tonemapExposure;      // (post pass)
        float tonemapWhite;         // (post pass)

        float brightness;           // (post pass)
        float contrast;             // (post pass)
        float saturation;           // (post pass)

    } env;

    // Default textures
    struct {
        unsigned int white;
        unsigned int black;
        unsigned int normal;
        unsigned int blueNoise;
        unsigned int ssaoNoise;
        unsigned int ssaoKernel;
        unsigned int iblBrdfLut;
    } texture;

    // Primitives
    struct {
        unsigned int dummyVAO;      //< VAO with no buffers, used to generate geometry in the shader via glDrawArrays
        r3d_primitive_t quad;
        r3d_primitive_t cube;
    } primitive;

    // State data
    struct {

        // Camera transformations
        struct {
            Matrix view, invView;
            Matrix proj, invProj;
            Vector3 position;
        } transform;

        // Frustum data
        struct {
            r3d_frustum_t shape;
            BoundingBox aabb;
        } frustum;

        // Scene data
        struct {
            BoundingBox bounds;
        } scene;

        // Resolution
        struct {
            int width;
            int height;
            float texelX;
            float texelY;
        } resolution;

        // Render config
        struct {
            R3D_RenderMode mode;
            R3D_BlendMode blendMode;
            R3D_ShadowCastMode shadowCastMode;
            R3D_BillboardMode billboardMode;
            float alphaScissorThreshold;
        } render;

        // Miscellaneous flags
        unsigned int flags;

    } state;

    // Misc data
    struct {
        Matrix matCubeViews[6];
    } misc;

} R3D;

/* === Helper functions === */

bool r3d_check_texture_format_support(unsigned int format);

void r3d_calculate_bloom_prefilter_data();


/* === Main loading functions === */

void r3d_framebuffers_load(int width, int height);
void r3d_framebuffers_unload(void);

void r3d_textures_load(void);
void r3d_textures_unload(void);

void r3d_shaders_load(void);
void r3d_shaders_unload(void);


/* === Framebuffer loading functions === */

void r3d_framebuffer_load_gbuffer(int width, int height);
void r3d_framebuffer_load_pingpong_ssao(int width, int height);
void r3d_framebuffer_load_deferred(int width, int height);
void r3d_framebuffer_load_scene(int width, int height);
void r3d_framebuffer_load_mipchain_bloom(int width, int height);
void r3d_framebuffer_load_pingpong_post(int width, int height);

void r3d_framebuffer_unload_gbuffer(void);
void r3d_framebuffer_unload_pingpong_ssao(void);
void r3d_framebuffer_unload_deferred(void);
void r3d_framebuffer_unload_scene(void);
void r3d_framebuffer_unload_mipchain_bloom(void);
void r3d_framebuffer_unload_pingpong_post(void);


/* === Shader loading functions === */

void r3d_shader_load_generate_gaussian_blur_dual_pass(void);
void r3d_shader_load_generate_downsampling(void);
void r3d_shader_load_generate_upsampling(void);
void r3d_shader_load_generate_cubemap_from_equirectangular(void);
void r3d_shader_load_generate_irradiance_convolution(void);
void r3d_shader_load_generate_prefilter(void);
void r3d_shader_load_raster_geometry(void);
void r3d_shader_load_raster_geometry_inst(void);
void r3d_shader_load_raster_forward(void);
void r3d_shader_load_raster_forward_inst(void);
void r3d_shader_load_raster_skybox(void);
void r3d_shader_load_raster_depth(void);
void r3d_shader_load_raster_depth_inst(void);
void r3d_shader_load_raster_depth_cube(void);
void r3d_shader_load_raster_depth_cube_inst(void);
void r3d_shader_load_screen_ssao(void);
void r3d_shader_load_screen_ambient_ibl(void);
void r3d_shader_load_screen_ambient(void);
void r3d_shader_load_screen_lighting(void);
void r3d_shader_load_screen_scene(void);
void r3d_shader_load_screen_bloom(void);
void r3d_shader_load_screen_fog(void);
void r3d_shader_load_screen_tonemap(void);
void r3d_shader_load_screen_adjustment(void);
void r3d_shader_load_screen_fxaa(void);


/* === Texture loading functions === */

void r3d_texture_load_white(void);
void r3d_texture_load_black(void);
void r3d_texture_load_normal(void);
void r3d_texture_load_blue_noise(void);
void r3d_texture_load_ssao_noise(void);
void r3d_texture_load_ssao_kernel(void);
void r3d_texture_load_ibl_brdf_lut(void);


/* === Framebuffer helper macros === */

#define r3d_framebuffer_swap_pingpong(fb)       \
{                                               \
    unsigned int tmp = (fb).target;             \
    (fb).target = (fb).source;                  \
    (fb).source = tmp;                          \
    glFramebufferTexture2D(                     \
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,   \
        GL_TEXTURE_2D, (fb).target, 0           \
    );                                          \
}


/* === Shader helper macros === */

#define r3d_shader_enable(shader_name)                                                          \
{                                                                                               \
    rlEnableShader(R3D.shader.shader_name.id);                                                  \
}

#define r3d_shader_disable()                                                                    \
{                                                                                               \
    rlDisableShader();                                                                          \
}

#define r3d_shader_get_location(shader_name, uniform)                                           \
{                                                                                               \
    R3D.shader.shader_name.uniform.loc = rlGetLocationUniform(                                  \
        R3D.shader.shader_name.id, #uniform                                                     \
    );                                                                                          \
}

#define r3d_shader_set_sampler1D_slot(shader_name, uniform, value)                              \
{                                                                                               \
    if (R3D.shader.shader_name.uniform.slot1D != value) {                                       \
        R3D.shader.shader_name.uniform.slot1D = value;                                          \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.slot1D,                                             \
            RL_SHADER_UNIFORM_INT, 1                                                            \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_sampler2D_slot(shader_name, uniform, value)                              \
{                                                                                               \
    if (R3D.shader.shader_name.uniform.slot2D != value) {                                       \
        R3D.shader.shader_name.uniform.slot2D = value;                                          \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.slot2D,                                             \
            RL_SHADER_UNIFORM_INT, 1                                                            \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_samplerCube_slot(shader_name, uniform, value)                            \
{                                                                                               \
    if (R3D.shader.shader_name.uniform.slotCube != value) {                                     \
        R3D.shader.shader_name.uniform.slotCube = value;                                        \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.slotCube,                                           \
            RL_SHADER_UNIFORM_INT, 1                                                            \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_bind_sampler1D(shader_name, uniform, texId)                                  \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slot1D);                       \
    glBindTexture(GL_TEXTURE_1D, (texId));                                                      \
}

#define r3d_shader_bind_sampler2D(shader_name, uniform, texId)                                  \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slot2D);                       \
    glBindTexture(GL_TEXTURE_2D, (texId));                                                      \
}

#define r3d_shader_bind_sampler2D_opt(shader_name, uniform, texId, altTex)                      \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slot2D);                       \
    if (texId != 0) glBindTexture(GL_TEXTURE_2D, (texId));                                      \
    else glBindTexture(GL_TEXTURE_2D, R3D.texture.altTex);                                      \
}

#define r3d_shader_bind_samplerCube(shader_name, uniform, texId)                                \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slotCube);                     \
    glBindTexture(GL_TEXTURE_CUBE_MAP, (texId));                                                \
}

#define r3d_shader_unbind_sampler1D(shader_name, uniform)                                       \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slot1D);                       \
    glBindTexture(GL_TEXTURE_1D, 0);                                                            \
}

#define r3d_shader_unbind_sampler2D(shader_name, uniform)                                       \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slot2D);                       \
    glBindTexture(GL_TEXTURE_2D, 0);                                                            \
}

#define r3d_shader_unbind_samplerCube(shader_name, uniform)                                     \
{                                                                                               \
    glActiveTexture(GL_TEXTURE0 + R3D.shader.shader_name.uniform.slotCube);                     \
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);                                                      \
}

#define r3d_shader_set_int(shader_name, uniform, value)                                         \
{                                                                                               \
    if (R3D.shader.shader_name.uniform.val != (value)) {                                        \
        R3D.shader.shader_name.uniform.val = (value);                                           \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_INT, 1                                                            \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_float(shader_name, uniform, value)                                       \
{                                                                                               \
    if (R3D.shader.shader_name.uniform.val != (value)) {                                        \
        R3D.shader.shader_name.uniform.val = (value);                                           \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_FLOAT, 1                                                          \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_vec2(shader_name, uniform, value)                                        \
{                                                                                               \
    if (!Vector2Equals(R3D.shader.shader_name.uniform.val, (value))) {                          \
        R3D.shader.shader_name.uniform.val = (value);                                           \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_VEC2, 1                                                           \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_vec3(shader_name, uniform, value)                                        \
{                                                                                               \
    if (!Vector3Equals(R3D.shader.shader_name.uniform.val, (value))) {                          \
        R3D.shader.shader_name.uniform.val = (value);                                           \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_VEC3, 1                                                           \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_vec4(shader_name, uniform, value)                                        \
{                                                                                               \
    if (!Vector4Equals(R3D.shader.shader_name.uniform.val, (value))) {                          \
        R3D.shader.shader_name.uniform.val = (value);                                           \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_VEC4, 1                                                           \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_col3(shader_name, uniform, value)                                        \
{                                                                                               \
    Vector3 v = { value.r / 255.0f, value.g / 255.0f, value.b / 255.0f };                       \
    if (!Vector3Equals(R3D.shader.shader_name.uniform.val, v)) {                                \
        R3D.shader.shader_name.uniform.val = v;                                                 \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_VEC3, 1                                                           \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_col4(shader_name, uniform, value)                                        \
{                                                                                               \
    Vector4 v = { value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f };     \
    if (!Vector4Equals(R3D.shader.shader_name.uniform.val, v)) {                                \
        R3D.shader.shader_name.uniform.val = v;                                                 \
        rlSetUniform(                                                                           \
            R3D.shader.shader_name.uniform.loc,                                                 \
            &R3D.shader.shader_name.uniform.val,                                                \
            RL_SHADER_UNIFORM_VEC4, 1                                                           \
        );                                                                                      \
    }                                                                                           \
}

#define r3d_shader_set_mat4(shader_name, uniform, value)                                        \
{                                                                                               \
    rlSetUniformMatrix(R3D.shader.shader_name.uniform.loc, value);                              \
}


/* === Primitive helper macros */

#define r3d_primitive_draw_quad()                           \
{                                                           \
    r3d_primitive_draw(&R3D.primitive.quad);                \
}

#define r3d_primitive_draw_cube()                           \
{                                                           \
    r3d_primitive_draw(&R3D.primitive.cube);                \
}

#define r3d_primitive_draw_screen()                         \
{                                                           \
    glBindVertexArray(R3D.primitive.dummyVAO);              \
    glDrawArrays(GL_TRIANGLES, 0, 3);                       \
    glBindVertexArray(0);                                   \
}

#endif // R3D_STATE_H
