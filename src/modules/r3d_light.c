/* r3d_light.c -- Internal R3D light module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include "./r3d_light.h"

#include <r3d/r3d_frustum.h>
#include <r3d/r3d_color.h>
#include <r3d_config.h>
#include <raymath.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <float.h>

#include "../r3d_core_state.h"

// ========================================
// CONSTANTS
// ========================================

#define SHADOW_DIR_LAYER_GROWTH     2
#define SHADOW_SPOT_LAYER_GROWTH    4
#define SHADOW_OMNI_LAYER_GROWTH    4

// ========================================
// MODULE STATE
// ========================================

struct r3d_light R3D_MOD_LIGHT;

// ========================================
// CONSTANTS
// ========================================

static const GLenum SHADOW_TEXTURE_TARGET[] = {
    [R3D_LIGHT_DIR]  = GL_TEXTURE_2D_ARRAY,
    [R3D_LIGHT_SPOT] = GL_TEXTURE_2D_ARRAY,
    [R3D_LIGHT_OMNI] = GL_TEXTURE_CUBE_MAP_ARRAY,
};

static const int SHADOW_LAYER_GROWTH[] = {
    [R3D_LIGHT_DIR]  = SHADOW_DIR_LAYER_GROWTH,
    [R3D_LIGHT_SPOT] = SHADOW_SPOT_LAYER_GROWTH,
    [R3D_LIGHT_OMNI] = SHADOW_OMNI_LAYER_GROWTH,
};

// ========================================
// SHADOW ARRAY FUNCTIONS
// ========================================

static void shadow_array_allocate_texture(GLuint texture, GLenum target, int size, uint32_t layers)
{
    int actualLayers = (target == GL_TEXTURE_CUBE_MAP_ARRAY) ? (int)layers * 6 : (int)layers;

    glBindTexture(target, texture);
    glTexImage3D(
        target, 0, GL_DEPTH_COMPONENT16, size, size, actualLayers,
        0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, NULL
    );

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (target == GL_TEXTURE_CUBE_MAP_ARRAY)
    {
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glBindTexture(target, 0);
}

static r3d_light_shadow_array_t shadow_array_create(GLenum target, int size, int growth)
{
    r3d_light_shadow_array_t arr = {0};

    arr.target   = target;
    arr.size     = size;
    arr.growth   = growth;
    arr.freeList = R3D_LIST_CREATE(int, 16);
    arr.cache    = R3D_LIST_CREATE(r3d_light_shadow_cache_t, 16);

    glGenFramebuffers(1, &arr.framebuffer);

    return arr;
}

static void shadow_array_destroy(r3d_light_shadow_array_t* arr)
{
    if (arr->texture != 0)     glDeleteTextures(1, &arr->texture);
    if (arr->framebuffer != 0) glDeleteFramebuffers(1, &arr->framebuffer);

    R3D_LIST_DESTROY(arr->cache);
    R3D_LIST_DESTROY(arr->freeList);

    *arr = (r3d_light_shadow_array_t){0};
}

static bool shadow_array_expand(r3d_light_shadow_array_t* arr, uint32_t growth)
{
    uint32_t newLayerCount = arr->layerCount + growth;

    GLuint newTexture;
    glGenTextures(1, &newTexture);
    shadow_array_allocate_texture(newTexture, arr->target, arr->size, newLayerCount);

    // Copy existing content into the new texture if any
    if (arr->layerCount > 0 && arr->texture != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, arr->framebuffer);

        int facesPerLayer = (arr->target == GL_TEXTURE_CUBE_MAP_ARRAY) ? 6 : 1;
        for (uint32_t layer = 0; layer < arr->layerCount; layer++)
        {
            for (int face = 0; face < facesPerLayer; face++)
            {
                int layerFace = (int)layer * facesPerLayer + face;
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, arr->texture, 0, layerFace);
                glBindTexture(arr->target, newTexture);
                glCopyTexSubImage3D(arr->target, 0, 0, 0, layerFace, 0, 0, arr->size, arr->size);
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(arr->target, 0);
    }

    if (arr->texture != 0)
    {
        glDeleteTextures(1, &arr->texture);
    }
    arr->texture = newTexture;

    // Resize the cache (new entries zero-initialized => valid = false)
    R3D_LIST_RESIZE(arr->cache, newLayerCount);

    // Newly allocated layers become available
    for (uint32_t layer = arr->layerCount; layer < newLayerCount; layer++)
    {
        int l = (int)layer;
        R3D_LIST_PUSH(arr->freeList, l);
    }

    arr->layerCount = newLayerCount;

    return true;
}

static int shadow_array_acquire_layer(r3d_light_shadow_array_t* arr)
{
    if (R3D_LIST_EMPTY(arr->freeList))
    {
        if (!shadow_array_expand(arr, (uint32_t)arr->growth))
        {
            return -1;
        }
    }

    int layer = -1;
    R3D_LIST_POP(arr->freeList, &layer);
    return layer;
}

static void shadow_array_release_layer(r3d_light_shadow_array_t* arr, int layer)
{
    r3d_light_shadow_cache_t* cache = &R3D_LIST_GET(arr->cache, r3d_light_shadow_cache_t, layer);
    cache->valid = false;

    R3D_LIST_PUSH(arr->freeList, layer);
}

// ========================================
// LIGHT FUNCTIONS
// ========================================

static Matrix light_dir_view_proj(Vector3 dir, float range, R3D_Camera camera, double aspect, float* outNear, float* outFar)
{
    float camNear   = range / 1000.0f;
    float camFar    = range;
    float camFovy   = (float)camera.fovy;
    float camAspect = (float)aspect;

    float farH      = camFar * tanf(camFovy * (DEG2RAD * 0.5f));
    float halfDepth = (camFar - camNear) * 0.5f;
    float radius    = sqrtf(farH * farH * (1.0f + camAspect * camAspect) + halfDepth * halfDepth);

    Vector3 forward       = R3D_GetCameraForward(camera);
    Vector3 frustumCenter = Vector3Add(camera.position, Vector3Scale(forward, (camNear + camFar) * 0.5f));

    float ax = fabsf(dir.x);
    float ay = fabsf(dir.y);
    float az = fabsf(dir.z);

    Vector3 up         = (ax <= ay && ax <= az) ? (Vector3){1,0,0} : (ay <= az) ? (Vector3){0,1,0} : (Vector3){0,0,1};
    Vector3 lightRight = Vector3Normalize(Vector3CrossProduct(up, dir));
    Vector3 lightUp    = Vector3CrossProduct(dir, lightRight);

    float texelSize = (radius * 2.0f) / (float)R3D_HINT(R3D_HINT_SHADOW_DIR_SIZE);
    float cx = floorf(Vector3DotProduct(frustumCenter, lightRight) / texelSize) * texelSize;
    float cy = floorf(Vector3DotProduct(frustumCenter, lightUp) / texelSize) * texelSize;
    float cz = Vector3DotProduct(frustumCenter, dir);

    Vector3 snappedCenter = Vector3Add(
        Vector3Add(
            Vector3Scale(lightRight, cx),
            Vector3Scale(lightUp, cy)
        ),
        Vector3Scale(dir, cz)
    );

    const float zExtension = 100.0f; // Extent to capture objects behind the camera
    Vector3 eye = Vector3Subtract(snappedCenter, Vector3Scale(dir, radius + zExtension));
    Matrix view = MatrixLookAt(eye, snappedCenter, lightUp);

    *outNear = 0.0f;
    *outFar  = zExtension + radius * 2.0f;

    Matrix proj = MatrixOrtho(-radius, radius, -radius, radius, *outNear, *outFar);

    return MatrixMultiply(view, proj);
}

static Matrix light_spot_view_proj(Vector3 pos, Vector3 dir, float range, float* outNear, float* outFar)
{
    *outNear = 0.05f;
    *outFar  = range;

    Vector3 up  = {0, 1, 0};
    float upDot = fabsf(Vector3DotProduct(dir, up));
    if (upDot > 0.99f) up = (Vector3){1, 0, 0};

    Matrix view = MatrixLookAt(pos, Vector3Add(pos, dir), up);
    Matrix proj = MatrixPerspective(90 * DEG2RAD, 1.0, *outNear, *outFar);

    return MatrixMultiply(view, proj);
}

static void light_omni_view_proj(Vector3 pos, float range, Matrix* outMatrices, float* outNear, float* outFar)
{
    static const Vector3 DIRS[6] = {
        {  1.0,  0.0,  0.0 }, { -1.0,  0.0,  0.0 },
        {  0.0,  1.0,  0.0 }, {  0.0, -1.0,  0.0 },
        {  0.0,  0.0,  1.0 }, {  0.0,  0.0, -1.0 }
    };

    static const Vector3 UPS[6] = {
        {  0.0, -1.0,  0.0 }, {  0.0, -1.0,  0.0 },
        {  0.0,  0.0,  1.0 }, {  0.0,  0.0, -1.0 },
        {  0.0, -1.0,  0.0 }, {  0.0, -1.0,  0.0 }
    };

    *outNear = 0.05f;
    *outFar  = range;

    Matrix proj = MatrixPerspective(90 * DEG2RAD, 1.0, *outNear, *outFar);

    for (int face = 0; face < 6; face++)
    {
        Vector3 target = Vector3Add(pos, DIRS[face]);
        Matrix view = MatrixLookAt(pos, target, UPS[face]);
        outMatrices[face] = MatrixMultiply(view, proj);
    }
}

static void light_spot_bounding_sphere(Vector3* outCenter, float* outRadius, Vector3 pos, Vector3 dir, float range, float outerCos)
{
    if (outerCos >= 0.70710678f)
    {
        *outRadius = range / (2.0f * outerCos * outerCos);
        *outCenter = Vector3Add(pos, Vector3Scale(dir, *outRadius));
    }
    else
    {
        *outRadius = range * sqrtf(1.0f - outerCos * outerCos);
        *outCenter = Vector3Add(pos, Vector3Scale(dir, (range * outerCos)));
    }
}

static void light_dir_push(const R3D_Light* light, const R3D_ShadowMap* map, R3D_Camera camera, double aspect, bool updateShadow)
{
    r3d_light_data_t data = {
        .aabb = {
            .min = {-FLT_MAX, -FLT_MAX, -FLT_MAX},
            .max = {+FLT_MAX, +FLT_MAX, +FLT_MAX},
        },
        .color       = R3D_ColorSrgbToLinearVector3(light->color),
        .position    = {0},
        .direction   = Vector3Normalize(light->direction),
        .energy      = light->energy,
        .specular    = light->specular,
        .range       = light->range,
        .falloff     = 0.0f,
        .innerCutOff = 0.0f,
        .outerCutOff = 0.0f,
        .fogEnergy   = light->fogEnergy,
        .type        = light->type,
    };

    if (map && map->layer >= 0 && light->range > 0.0f)
    {
        r3d_light_shadow_cache_t* cache = r3d_light_shadow_cache(map->type, map->layer);

        if (!cache->valid || updateShadow)
        {
            cache->viewProj = light_dir_view_proj(data.direction, data.range, camera, aspect, &cache->near, &cache->far);
            cache->valid    = true;

            r3d_light_shadow_job_t job = {
                .frustum    = R3D_ComputeFrustum(cache->viewProj),
                .viewProj   = cache->viewProj,
                .cullMask   = map->cullMask,
                .lightIndex = R3D_LIST_LENGTH(R3D_MOD_LIGHT.listLightData),
                .layerFace  = 0,
            };

            R3D_LIST_PUSH(R3D_MOD_LIGHT.listShadowJobs, job);
        }

        data.viewProj        = cache->viewProj;
        data.shadowSoftness  = map->softness / (float)R3D_HINT(R3D_HINT_SHADOW_DIR_SIZE);
        data.shadowOpacity   = map->opacity;
        data.shadowDepthBias = map->depthBias;
        data.shadowSlopeBias = map->slopeBias;
        data.shadowFar       = cache->far;
        data.shadowLayer     = map->layer;
    }
    else
    {
        data.shadowLayer = -1;
    }

    R3D_LIST_PUSH(R3D_MOD_LIGHT.listLightData, data);
}

static void light_spot_push(const R3D_Light* light, const R3D_ShadowMap* map, const R3D_Frustum* frustum, bool updateShadow)
{
    float range = light->range;
    if (range <= 0.0f) return;

    Vector3 position  = light->position;
    Vector3 direction = Vector3Normalize(light->direction);
    float outerCutOff = cosf(light->outerCutOff * DEG2RAD);

    Vector3 bsCenter;
    float   bsRadius;
    light_spot_bounding_sphere(&bsCenter, &bsRadius, position, direction, range, outerCutOff);

    if (!R3D_FrustumIntersectsSphere(frustum, bsCenter, bsRadius))
    {
        return;
    }

    r3d_light_data_t data = {
        .aabb = {
            .min = Vector3AddValue(bsCenter, -bsRadius),
            .max = Vector3AddValue(bsCenter, +bsRadius),
        },
        .color       = R3D_ColorSrgbToLinearVector3(light->color),
        .position    = position,
        .direction   = direction,
        .energy      = light->energy,
        .specular    = light->specular,
        .range       = range,
        .falloff     = R3D_MAX(light->falloff, 1e-4f),
        .innerCutOff = cosf(light->innerCutOff * DEG2RAD),
        .outerCutOff = outerCutOff,
        .fogEnergy   = light->fogEnergy,
        .type        = light->type,
    };

    if (map && map->layer >= 0)
    {
        r3d_light_shadow_cache_t* cache = r3d_light_shadow_cache(map->type, map->layer);

        if (!cache->valid || updateShadow)
        {
            cache->viewProj = light_spot_view_proj(data.position, data.direction, data.range, &cache->near, &cache->far);
            cache->valid    = true;

            r3d_light_shadow_job_t job = {
                .frustum    = R3D_ComputeFrustum(cache->viewProj),
                .viewProj   = cache->viewProj,
                .cullMask   = map->cullMask,
                .lightIndex = R3D_LIST_LENGTH(R3D_MOD_LIGHT.listLightData),
                .layerFace  = 0,
            };

            R3D_LIST_PUSH(R3D_MOD_LIGHT.listShadowJobs, job);
        }

        data.viewProj        = cache->viewProj;
        data.shadowSoftness  = map->softness / (float)R3D_HINT(R3D_HINT_SHADOW_SPOT_SIZE);
        data.shadowOpacity   = map->opacity;
        data.shadowDepthBias = map->depthBias;
        data.shadowSlopeBias = map->slopeBias;
        data.shadowFar       = cache->far;
        data.shadowLayer     = map->layer;
    }
    else
    {
        data.shadowLayer = -1;
    }

    R3D_LIST_PUSH(R3D_MOD_LIGHT.listLightData, data);
}

static void light_omni_push(const R3D_Light* light, const R3D_ShadowMap* map, const R3D_Frustum* frustum, bool updateShadow)
{
    if (light->range <= 0.0f) return;

    if (!R3D_FrustumIntersectsSphere(frustum, light->position, light->range))
    {
        return;
    }

    r3d_light_data_t data = {
        .aabb = {
            .min = Vector3AddValue(light->position, -light->range),
            .max = Vector3AddValue(light->position, +light->range),
        },
        .color       = R3D_ColorSrgbToLinearVector3(light->color),
        .position    = light->position,
        .direction   = {0},
        .energy      = light->energy,
        .specular    = light->specular,
        .range       = light->range,
        .falloff     = R3D_MAX(light->falloff, 1e-4f),
        .innerCutOff = 0.0f,
        .outerCutOff = 0.0f,
        .fogEnergy   = light->fogEnergy,
        .type        = light->type,
    };

    if (map && map->layer >= 0)
    {
        r3d_light_shadow_cache_t* cache = r3d_light_shadow_cache(map->type, map->layer);

        if (!cache->valid || updateShadow)
        {
            cache->valid = true;

            Matrix viewProjs[6];
            light_omni_view_proj(data.position, data.range, viewProjs, &cache->near, &cache->far);

            for (int i = 0; i < 6; i++)
            {
                r3d_light_shadow_job_t job = {0};

                job.viewProj   = viewProjs[i];
                job.frustum    = R3D_ComputeFrustum(job.viewProj);
                job.lightIndex = R3D_LIST_LENGTH(R3D_MOD_LIGHT.listLightData);
                job.cullMask   = map->cullMask;
                job.layerFace  = i;

                R3D_LIST_PUSH(R3D_MOD_LIGHT.listShadowJobs, job);
            }
        }

        data.shadowSoftness  = map->softness / (float)R3D_HINT(R3D_HINT_SHADOW_OMNI_SIZE);
        data.shadowOpacity   = map->opacity;
        data.shadowDepthBias = map->depthBias;
        data.shadowSlopeBias = map->slopeBias;
        data.shadowFar       = cache->far;
        data.shadowLayer     = map->layer;
    }
    else
    {
        data.shadowLayer = -1;
    }

    R3D_LIST_PUSH(R3D_MOD_LIGHT.listLightData, data);
}

static const char* light_type_name(R3D_LightType type)
{
    switch (type)
    {
    case R3D_LIGHT_DIR:  return "Directional";
    case R3D_LIGHT_SPOT: return "Spot";
    case R3D_LIGHT_OMNI: return "Omni";
    default: break;
    }
    return NULL;
}

// ========================================
// MODULE FUNCTIONS
// ========================================

bool r3d_light_init(void)
{
    memset(&R3D_MOD_LIGHT, 0, sizeof(R3D_MOD_LIGHT));

    R3D_MOD_LIGHT.shadowArrays[R3D_LIGHT_DIR]  = shadow_array_create(GL_TEXTURE_2D_ARRAY, R3D_HINT(R3D_HINT_SHADOW_DIR_SIZE), SHADOW_DIR_LAYER_GROWTH);
    R3D_MOD_LIGHT.shadowArrays[R3D_LIGHT_SPOT] = shadow_array_create(GL_TEXTURE_2D_ARRAY, R3D_HINT(R3D_HINT_SHADOW_SPOT_SIZE), SHADOW_SPOT_LAYER_GROWTH);
    R3D_MOD_LIGHT.shadowArrays[R3D_LIGHT_OMNI] = shadow_array_create(GL_TEXTURE_CUBE_MAP_ARRAY, R3D_HINT(R3D_HINT_SHADOW_OMNI_SIZE), SHADOW_OMNI_LAYER_GROWTH);

    R3D_MOD_LIGHT.listShadowJobs = R3D_LIST_CREATE(r3d_light_shadow_job_t, 32);
    R3D_MOD_LIGHT.listLightData  = R3D_LIST_CREATE(r3d_light_data_t, 256);

    return true;
}

void r3d_light_quit(void)
{
    for (int i = 0; i < R3D_LIGHT_TYPE_COUNT; i++)
    {
        shadow_array_destroy(&R3D_MOD_LIGHT.shadowArrays[i]);
    }

    R3D_LIST_DESTROY(R3D_MOD_LIGHT.listShadowJobs);
    R3D_LIST_DESTROY(R3D_MOD_LIGHT.listLightData);
}

void r3d_light_push(const R3D_Light* light, const R3D_ShadowMap* map, bool updateShadow)
{
    if (map && map->type != light->type)
    {
        const char* mType = light_type_name(map->type);
        const char* lType = light_type_name(light->type);
        R3D_TRACELOG(LOG_WARNING, "Incompatible shadow map (type: %s) given with light (type: %s)", mType, lType);

        map = NULL;
    }

    switch (light->type)
    {
    case R3D_LIGHT_DIR:
        light_dir_push(light, map, R3D.viewState.camera, R3D.viewState.aspect, updateShadow);
        break;

    case R3D_LIGHT_SPOT:
        light_spot_push(light, map, &R3D.viewState.frustum, updateShadow);
        break;

    case R3D_LIGHT_OMNI:
        light_omni_push(light, map, &R3D.viewState.frustum, updateShadow);
        break;

    default:
        break;
    }
}

r3d_light_data_t* r3d_light_get(int lightIndex)
{
    return &R3D_LIST_GET(R3D_MOD_LIGHT.listLightData, r3d_light_data_t, lightIndex);
}

void r3d_light_clear(void)
{
    R3D_LIST_CLEAR(R3D_MOD_LIGHT.listShadowJobs);
    R3D_LIST_CLEAR(R3D_MOD_LIGHT.listLightData);
}

r3d_rect_t r3d_light_get_screen_rect(const r3d_light_data_t* light, const Matrix* viewProj, Vector3 camPos, int w, int h)
{
    assert(light->type != R3D_LIGHT_DIR);

    Vector3 min = light->aabb.min;
    Vector3 max = light->aabb.max;

    bool cameraInside =
        (camPos.x >= min.x && camPos.x <= max.x) &&
        (camPos.y >= min.y && camPos.y <= max.y) &&
        (camPos.z >= min.z && camPos.z <= max.z);

    if (cameraInside)
    {
        return (r3d_rect_t){0, 0, w, h};
    }

    Vector2 minNDC = {+FLT_MAX, +FLT_MAX};
    Vector2 maxNDC = {-FLT_MAX, -FLT_MAX};

    for (int i = 0; i < 8; i++)
    {
        Vector4 corner = {
            (i & 1) ? max.x : min.x,
            (i & 2) ? max.y : min.y,
            (i & 4) ? max.z : min.z,
            1.0f
        };
        Vector4 clip = r3d_vector4_transform(corner, viewProj);

        float w = clip.w;
        if (fabsf(w) < 1e-4f)
        {
            w = (w < 0.0f) ? -1e-4f : 1e-4f;
        }

        Vector2 ndc = Vector2Scale((Vector2){clip.x, clip.y}, 1.0f / w);
        minNDC = Vector2Min(minNDC, ndc);
        maxNDC = Vector2Max(maxNDC, ndc);
    }

    // NDC to screen
    int x = (int)fmaxf((minNDC.x * 0.5f + 0.5f) * w, 0.0f);
    int y = (int)fmaxf((minNDC.y * 0.5f + 0.5f) * h, 0.0f);
    int rectW = (int)fminf((maxNDC.x * 0.5f + 0.5f) * w, (float)w) - x;
    int rectH = (int)fminf((maxNDC.y * 0.5f + 0.5f) * h, (float)h) - y;

    // Security: Invalid dimensions = skip
    if (rectW <= 0 || rectH <= 0)
    {
        return (r3d_rect_t){0, 0, 0, 0};
    }

    return (r3d_rect_t){x, y, rectW, rectH};
}

int r3d_light_acquire_shadow_layer(R3D_LightType type)
{
    return shadow_array_acquire_layer(&R3D_MOD_LIGHT.shadowArrays[type]);
}

void r3d_light_release_shadow_layer(R3D_LightType type, int layer)
{
    if (layer >= 0)
    {
        shadow_array_release_layer(&R3D_MOD_LIGHT.shadowArrays[type], layer);
    }
}

void r3d_light_bind_shadow_fbo(R3D_LightType type, int layer, int face)
{
    assert((type == R3D_LIGHT_OMNI && face >= 0 && face < 6) || (type != R3D_LIGHT_OMNI && face == 0));

    r3d_light_shadow_array_t* arr = &R3D_MOD_LIGHT.shadowArrays[type];
    int stride = (type == R3D_LIGHT_OMNI) ? 6 : 1;

    glBindFramebuffer(GL_FRAMEBUFFER, arr->framebuffer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, arr->texture, 0, layer * stride + face);
    glViewport(0, 0, arr->size, arr->size);
}

int r3d_light_shadow_map_size(R3D_LightType type)
{
    return R3D_MOD_LIGHT.shadowArrays[type].size;
}

GLuint r3d_light_shadow_map(R3D_LightType type)
{
    return R3D_MOD_LIGHT.shadowArrays[type].texture;
}

r3d_light_shadow_cache_t* r3d_light_shadow_cache(R3D_LightType type, int layer)
{
    assert(layer >= 0);
    return &R3D_LIST_GET(R3D_MOD_LIGHT.shadowArrays[type].cache, r3d_light_shadow_cache_t, layer);
}
