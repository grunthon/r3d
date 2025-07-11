#include "./common.h"
#include "r3d.h"
#include <rlgl.h>

/* === Resources === */

static R3D_Model sponza = { 0 };
static R3D_Skybox skybox = { 0 };
static Camera3D camera = { 0 };
static R3D_Light lights[2] = { 0 };

static bool sky = false;


/* === Examples === */

const char* Init(void)
{
    R3D_Init(GetScreenWidth(), GetScreenHeight(), 0);
    SetTargetFPS(60);

    R3D_SetSSAO(true);
    R3D_SetSSAORadius(4.0f);
    R3D_SetBloomMode(R3D_BLOOM_MIX);

    R3D_SetAmbientColor(GRAY);

    R3D_SetModelImportScale(0.01f);
    sponza = R3D_LoadModel(RESOURCES_PATH "sponza.glb");
    skybox = R3D_LoadSkybox(RESOURCES_PATH "sky/skybox3.png", CUBEMAP_LAYOUT_AUTO_DETECT);

    // Useful if you use directional lights
    R3D_SetSceneBounds(sponza.aabb);

    for (int i = 0; i < 2; i++) {
        lights[i] = R3D_CreateLight(R3D_LIGHT_OMNI);

        R3D_SetLightPosition(lights[i], (Vector3) { i ? -10 : 10, 20, 0 });
        R3D_SetLightActive(lights[i], true);
        R3D_SetLightEnergy(lights[i], 1.0f);

        R3D_SetShadowUpdateMode(lights[i], R3D_SHADOW_UPDATE_MANUAL);
        R3D_EnableShadow(lights[i], 4096);
    }

    camera = (Camera3D){
        .position = (Vector3) { 0, 0, 0 },
        .target = (Vector3) { 0, 0, -1 },
        .up = (Vector3) { 0, 1, 0 },
        .fovy = 60,
    };

    DisableCursor();

    return "[r3d] - Sponza example";
}

void Update(float delta)
{
    UpdateCamera(&camera, CAMERA_FREE);

    if (IsKeyPressed(KEY_T)) {
        if (sky) R3D_DisableSkybox();
        else R3D_EnableSkybox(skybox);
        sky = !sky;
    }

    if (IsKeyPressed(KEY_F)) {
        bool fxaa = R3D_HasState(R3D_FLAG_FXAA);
        if (fxaa) R3D_ClearState(R3D_FLAG_FXAA);
        else R3D_SetState(R3D_FLAG_FXAA);
    }

    if (IsKeyPressed(KEY_O)) {
        R3D_SetSSAO(!R3D_GetSSAO());
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        R3D_Tonemap tonemap = R3D_GetTonemapMode();
        R3D_SetTonemapMode((tonemap + R3D_TONEMAP_COUNT - 1) % R3D_TONEMAP_COUNT);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        R3D_Tonemap tonemap = R3D_GetTonemapMode();
        R3D_SetTonemapMode((tonemap + 1) % R3D_TONEMAP_COUNT);
    }
}

void Draw(void)
{
    R3D_Begin(camera);
        R3D_DrawModel(&sponza, (Vector3) { 0 }, 1.0f);
    R3D_End();

    BeginMode3D(camera);
        DrawSphere(R3D_GetLightPosition(lights[0]), 0.5f, WHITE);
        DrawSphere(R3D_GetLightPosition(lights[1]), 0.5f, WHITE);
    EndMode3D();

    R3D_Tonemap tonemap = R3D_GetTonemapMode();

    switch (tonemap) {
    case R3D_TONEMAP_LINEAR: {
        const char* txt = "< TONEMAP LINEAR >";
        DrawText(txt, GetScreenWidth() - MeasureText(txt, 20) - 10, 10, 20, LIME);
    }
    break;
    case R3D_TONEMAP_REINHARD: {
        const char* txt = "< TONEMAP REINHARD >";
        DrawText(txt, GetScreenWidth() - MeasureText(txt, 20) - 10, 10, 20, LIME);
    }
    break;
    case R3D_TONEMAP_FILMIC: {
        const char* txt = "< TONEMAP FILMIC >";
        DrawText(txt, GetScreenWidth() - MeasureText(txt, 20) - 10, 10, 20, LIME);
    }
    break;
    case R3D_TONEMAP_ACES: {
        const char* txt = "< TONEMAP ACES >";
        DrawText(txt, GetScreenWidth() - MeasureText(txt, 20) - 10, 10, 20, LIME);

    } break;
    case R3D_TONEMAP_AGX: {
        const char* txt = "< TONEMAP AGX >";
        DrawText(txt, GetScreenWidth() - MeasureText(txt, 20) - 10, 10, 20, LIME);

    } break;
    default:
        break;
    }    
        
    DrawFPS(10, 10);
}

void Close(void)
{
    R3D_UnloadModel(&sponza, true);
    R3D_UnloadSkybox(skybox);
    R3D_Close();
}
