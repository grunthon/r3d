/* r3d_probe.h -- R3D Probe Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_PROBE_H
#define R3D_PROBE_H

#include "./r3d_platform.h"
#include <raylib.h>
#include <stdint.h>

/**
 * @defgroup Probe
 * @{
 */

// ========================================
// ENUM TYPES
// ========================================

typedef enum R3D_ProbeType {
    R3D_PROBE_ILLUMINATION,
    R3D_PROBE_REFLECTION,
} R3D_ProbeType;

// ========================================
// STRUCT TYPES
// ========================================

typedef struct R3D_Probe {
    R3D_ProbeType type;
    int layer;
    Vector3 position;
    float falloff;
    float range;
    bool interior;
    bool shadows;
} R3D_Probe;

// ========================================
// PUBLIC API
// ========================================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocates a probe of the given type.
 *
 * @param type Whether this probe contributes indirect lighting or reflections.
 * @param interior Whether the skybox is taken into account when capturing this probe.
 * @param shadow Whether shadow casters are taken into account when capturing this probe.
 */
R3DAPI R3D_Probe R3D_LoadProbe(R3D_ProbeType type, bool interior, bool shadow);

/**
 * @brief Releases a probe, freeing its layer for reuse.
 */
R3DAPI void R3D_UnloadProbe(R3D_Probe probe);

#ifdef __cplusplus
} // extern "C"
#endif

/** @} */ // end of Probe

#endif // R3D_PROBE_H
