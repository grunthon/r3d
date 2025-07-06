#include "./common.h"
#include "r3d.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>

/* === Resources === */

static R3D_Model model = { 0 };
static Matrix modelMatrix = { 0 };
static R3D_Skybox skybox = { 0 };
static Camera3D camera = { 0 };

static float modelScale = 10.0f;

/* === Example === */

const char* Init(void)
{
	R3D_Init(GetScreenWidth(), GetScreenHeight(), R3D_FLAG_FXAA);
	//SetTargetFPS(60);

	R3D_SetSSAO(true);
	R3D_SetSSAORadius(4.0f);

	R3D_SetTonemapMode(R3D_TONEMAP_ACES);
	R3D_SetTonemapExposure(0.5f);
	//R3D_SetTonemapWhite(1.25f);

	R3D_SetBloomMode(R3D_BLEND_ADDITIVE);
	R3D_SetBloomThreshold(18.0f);

	R3D_SetAmbientColor(BLACK);
	//R3D_SetAmbientColor((Color) { 0.3f, 0.3f, 0.3f, 1.0f });

	//R3D_SetBackgroundColor((Color) { 0.2f, 0.2f, 0.2f, 1.0f });

	model = R3D_LoadModel(RESOURCES_PATH "pbr/DamagedHelmet.glb");
	{
		Matrix transform = MatrixRotateY(PI / 2);

		for (int i = 0; i < model.materialCount; i++) {
			GenTextureMipmaps(&model.materials[i].albedo.texture);
			SetTextureFilter(model.materials[i].albedo.texture, TEXTURE_FILTER_ANISOTROPIC_16X);
			SetTextureFilter(model.materials[i].albedo.texture, TEXTURE_FILTER_BILINEAR);

			GenTextureMipmaps(&model.materials[i].orm.texture);
			SetTextureFilter(model.materials[i].orm.texture, TEXTURE_FILTER_ANISOTROPIC_16X);
			SetTextureFilter(model.materials[i].orm.texture, TEXTURE_FILTER_BILINEAR);

			GenTextureMipmaps(&model.materials[i].emission.texture);
			SetTextureFilter(model.materials[i].emission.texture, TEXTURE_FILTER_ANISOTROPIC_16X);
			SetTextureFilter(model.materials[i].emission.texture, TEXTURE_FILTER_BILINEAR);
		}
	}

	modelMatrix = MatrixIdentity();

	//skybox = R3D_LoadSkyboxHDR(RESOURCES_PATH "sky/buikslotermeerplein_4k.hdr", 4096);
	//skybox = R3D_LoadSkyboxHDR(RESOURCES_PATH "sky/golden_bay_4k.hdr", 2048);
	skybox = R3D_LoadSkyboxHDR(RESOURCES_PATH "sky/tokyo.hdr", 4096);
	SetTextureFilter(skybox.cubemap, TEXTURE_FILTER_BILINEAR);

	R3D_EnableSkybox(skybox);

	camera = (Camera3D){
		.position = (Vector3) { 0, 0, 0.5f },
		.target = (Vector3) { 0, 0, 0 },
		.up = (Vector3) { 0, 1, 0 },
		.fovy = 60,
	};

	R3D_Light light = R3D_CreateLight(R3D_LIGHT_DIR);
	{
		R3D_SetLightDirection(light, (Vector3) { 0, -1, -1 });
		R3D_SetLightActive(light, true);
	}

	return "[r3d] - PBR musket example";
}

void Update(float delta)
{
	modelScale = Clamp(modelScale + GetMouseWheelMove() * 0.1, 0.25f, 62.5f);

	if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
	{
		float pitch = (GetMouseDelta().y * 0.05f) / modelScale;
		float yaw = (GetMouseDelta().x * 0.05f) / modelScale;

		Matrix rotate = MatrixRotateXYZ((Vector3) { pitch, yaw, 0.0f });
		modelMatrix = MatrixMultiply(modelMatrix, rotate);
	}

	if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
	{
		Vector3 skyboxRotation = R3D_GetSkyboxRotation();
		//skyboxRotation.x += (GetMouseDelta().y * 0.005f);
		skyboxRotation.y += (GetMouseDelta().x * 0.005f);

		const float maxRot = (89.0f * PI / 180.0f);

		// Clamp pitch to avoid gimbal lock
		if (skyboxRotation.y > maxRot) {
			skyboxRotation.y = maxRot;
		}
		else if (skyboxRotation.y < -maxRot) {
			skyboxRotation.y = -maxRot;
		}

		// Normalize yaw to keep it within the range of 0 to 2*PI (optional)
		if (skyboxRotation.x > 2 * PI) {
			skyboxRotation.x -= 2 * PI;
		}
		else if (skyboxRotation.x < 0) {
			skyboxRotation.x += 2 * PI;
		}

		//printf("%f\n", skyboxRotation.y);


		R3D_SetSkyboxRotation(skyboxRotation.x, skyboxRotation.y, 0.0f);


	}

	//if (IsKeyDown(KEY_KP_ADD)) {
	//	R3D_SetBloomThreshold(R3D_GetBloomThreshold() + 0.001f);

	//}

	//if (IsKeyDown(KEY_KP_SUBTRACT)) {
	//	R3D_SetBloomThreshold(R3D_GetBloomThreshold() - 0.001f);
	//}


	if (IsKeyDown(KEY_KP_ADD)) {
		R3D_SetSkyboxRotation(0.0f, R3D_GetSkyboxRotation().y + 0.001f, 0.0f);
	}

	if (IsKeyDown(KEY_KP_SUBTRACT)) {
		R3D_SetSkyboxRotation(0.0f, R3D_GetSkyboxRotation().y - 0.001f, 0.0f);
	}

	//for (int i = 0; i < model.materialCount; i++) {
	//	//if (model.materials[i].emission.texture.id != 0)
	//		model.materials[i].emission.multiplier = (float)(sin(GetTime()*2) + 1);
	//		//model.materials[i].emission.multiplier = 0.0f;
	//}
}

void Draw(void)
{
	R3D_Begin(camera);
		Matrix scale = MatrixScale(modelScale, modelScale, modelScale);
		Matrix transform = MatrixMultiply(modelMatrix, scale);
		R3D_DrawModelPro(&model, transform);
	R3D_End();

	//DrawCredits("Model made by TommyLingL");
	DrawFPS(10, 10);
}

void Close(void)
{
	R3D_UnloadModel(&model, true);
	R3D_UnloadSkybox(skybox);
	R3D_Close();
}
