#include "details/r3d_shaders.h"
#include "r3d.h"

#include "./details/r3d_primitives.h"
#include "./details/r3d_simd.h"
#include "./r3d_state.h"
#include "raylib.h"

#include <raymath.h>
#include <glad.h>

#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <time.h>

/* === Public Mesh Functions === */

R3D_Mesh R3D_GenMeshPoly(int sides, float radius, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Validation of parameters
    if (sides < 3 || radius <= 0.0f) return mesh;

    // Memory allocation
    // For a polygon: 1 central vertex + peripheral vertices
    mesh.vertexCount = sides + 1;
    mesh.indexCount = sides * 3; // sides triangles, 3 indices per triangle

    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Pre-compute some values
    const float angleStep = 2.0f * PI / sides;
    const Vector3 normal = {0.0f, 0.0f, 1.0f}; // Normal up (XY plane)
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // Opaque white

    // Central vertex (index 0)
    mesh.vertices[0] = (R3D_Vertex){
        .position = {0.0f, 0.0f, 0.0f},
        .texcoord = {0.5f, 0.5f}, // Texture center
        .normal = normal,
        .color = defaultColor,
        .tangent = {1.0f, 0.0f, 0.0f, 1.0f} // Tangent to X+
    };

    // Generation of peripheral vertices and indices
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;

    for (int i = 0; i < sides; i++) {
        const float angle = i * angleStep;
        const float cosAngle = cosf(angle);
        const float sinAngle = sinf(angle);
    
        // Position on the circle
        const float x = radius * cosAngle;
        const float y = radius * sinAngle;
    
        // AABB Update
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    
        // Peripheral vertex
        mesh.vertices[i + 1] = (R3D_Vertex){
            .position = {x, y, 0.0f},
            .texcoord = {
                0.5f + 0.5f * cosAngle, // Circular UV mapping
                0.5f + 0.5f * sinAngle
            },
            .normal = normal,
            .color = defaultColor,
            .tangent = {-sinAngle, cosAngle, 0.0f, 1.0f} // Tangent perpendicular to the radius
        };
    
        // Indices for the triangle (center, current vertex, next vertex)
        const int baseIdx = i * 3;
        mesh.indices[baseIdx] = 0; // Center
        mesh.indices[baseIdx + 1] = i + 1; // Current vertex
        mesh.indices[baseIdx + 2] = (i + 1) % sides + 1; // Next vertex (with wrap)
    }

    // Final AABB calculation
    mesh.aabb = (BoundingBox){
        .min = {minX, minY, 0.0f},
        .max = {maxX, maxY, 0.0f}
    };

    // Optional upload to GPU
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshPlane(float width, float length, int resX, int resZ, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Validation of parameters
    if (width <= 0.0f || length <= 0.0f || resX < 1 || resZ < 1) return mesh;

    // Calculating grid dimensions
    const int verticesPerRow = resX + 1;
    const int verticesPerCol = resZ + 1;
    mesh.vertexCount = verticesPerRow * verticesPerCol;
    mesh.indexCount = resX * resZ * 6; // 2 triangles per quad, 3 indices per triangle

    // Memory allocation
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Pre-compute some values
    const float halfWidth = width * 0.5f;
    const float halfLength = length * 0.5f;
    const float stepX = width / resX;
    const float stepZ = length / resZ;
    const float uvStepX = 1.0f / resX;
    const float uvStepZ = 1.0f / resZ;

    const Vector3 normal = { 0.0f, 1.0f, 0.0f }; // Normal to Y+ (horizontal plane)
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    const Vector4 tangent = { 1.0f, 0.0f, 0.0f, 1.0f }; // Tangent to X+

    // Vertex generation
    int vertexIndex = 0;
    for (int z = 0; z <= resZ; z++) {
        const float posZ = -halfLength + z * stepZ;
        const float uvZ = (float)z * uvStepZ;
    
        for (int x = 0; x <= resX; x++) {
            const float posX = -halfWidth + x * stepX;
            const float uvX = (float)x * uvStepX;
        
            mesh.vertices[vertexIndex] = (R3D_Vertex){
                .position = {posX, 0.0f, posZ},
                .texcoord = {uvX, uvZ},
                .normal = normal,
                .color = defaultColor,
                .tangent = tangent
            };
            vertexIndex++;
        }
    }

    // Generation of indices (counter-clockwise order)
    int indexOffset = 0;
    for (int z = 0; z < resZ; z++) {
        const int rowStart = z * verticesPerRow;
        const int nextRowStart = (z + 1) * verticesPerRow;
    
        for (int x = 0; x < resX; x++) {
            // Clues from the 4 corners of the quad
            const unsigned int topLeft = rowStart + x;
            const unsigned int topRight = rowStart + x + 1;
            const unsigned int bottomLeft = nextRowStart + x;
            const unsigned int bottomRight = nextRowStart + x + 1;
        
            // First triangle (topLeft, bottomLeft, topRight)
            mesh.indices[indexOffset++] = topLeft;
            mesh.indices[indexOffset++] = bottomLeft;
            mesh.indices[indexOffset++] = topRight;
        
            // Second triangle (topRight, bottomLeft, bottomRight)
            mesh.indices[indexOffset++] = topRight;
            mesh.indices[indexOffset++] = bottomLeft;
            mesh.indices[indexOffset++] = bottomRight;
        }
    }

    // AABB Calculation
    mesh.aabb = (BoundingBox){
        .min = {-halfWidth, 0.0f, -halfLength},
        .max = {halfWidth, 0.0f, halfLength}
    };

    // Optional upload to GPU
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshCube(float width, float height, float length, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Validation of parameters
    if (width <= 0.0f || height <= 0.0f || length <= 0.0f) return mesh;

    // Cube dimensions
    mesh.vertexCount = 24; // 4 vertices per face, 6 faces
    mesh.indexCount = 36;  // 2 triangles per face, 3 indices per triangle, 6 faces

    // Memory allocation
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Pre-compute some values
    const float halfW = width * 0.5f;
    const float halfH = height * 0.5f;
    const float halfL = length * 0.5f;
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Standard UV coordinates for each face
    const Vector2 uvs[4] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
    };

    // Generation of the 6 faces of the cube
    int vertexOffset = 0;
    int indexOffset = 0;

    // Back face (+Z)
    const Vector3 frontNormal = {0.0f, 0.0f, 1.0f};
    const Vector4 frontTangent = {1.0f, 0.0f, 0.0f, 1.0f};
    mesh.vertices[vertexOffset + 0] = (R3D_Vertex){{-halfW, -halfH, halfL}, uvs[0], frontNormal, defaultColor, frontTangent};
    mesh.vertices[vertexOffset + 1] = (R3D_Vertex){{halfW, -halfH, halfL}, uvs[1], frontNormal, defaultColor, frontTangent};
    mesh.vertices[vertexOffset + 2] = (R3D_Vertex){{halfW, halfH, halfL}, uvs[2], frontNormal, defaultColor, frontTangent};
    mesh.vertices[vertexOffset + 3] = (R3D_Vertex){{-halfW, halfH, halfL}, uvs[3], frontNormal, defaultColor, frontTangent};
    vertexOffset += 4;

    // Front face (-Z)
    const Vector3 backNormal = {0.0f, 0.0f, -1.0f};
    const Vector4 backTangent = {-1.0f, 0.0f, 0.0f, 1.0f};
    mesh.vertices[vertexOffset + 0] = (R3D_Vertex){{halfW, -halfH, -halfL}, uvs[0], backNormal, defaultColor, backTangent};
    mesh.vertices[vertexOffset + 1] = (R3D_Vertex){{-halfW, -halfH, -halfL}, uvs[1], backNormal, defaultColor, backTangent};
    mesh.vertices[vertexOffset + 2] = (R3D_Vertex){{-halfW, halfH, -halfL}, uvs[2], backNormal, defaultColor, backTangent};
    mesh.vertices[vertexOffset + 3] = (R3D_Vertex){{halfW, halfH, -halfL}, uvs[3], backNormal, defaultColor, backTangent};
    vertexOffset += 4;

    // Right face (+X)
    const Vector3 rightNormal = {1.0f, 0.0f, 0.0f};
    const Vector4 rightTangent = {0.0f, 0.0f, -1.0f, 1.0f};
    mesh.vertices[vertexOffset + 0] = (R3D_Vertex){{halfW, -halfH, halfL}, uvs[0], rightNormal, defaultColor, rightTangent};
    mesh.vertices[vertexOffset + 1] = (R3D_Vertex){{halfW, -halfH, -halfL}, uvs[1], rightNormal, defaultColor, rightTangent};
    mesh.vertices[vertexOffset + 2] = (R3D_Vertex){{halfW, halfH, -halfL}, uvs[2], rightNormal, defaultColor, rightTangent};
    mesh.vertices[vertexOffset + 3] = (R3D_Vertex){{halfW, halfH, halfL}, uvs[3], rightNormal, defaultColor, rightTangent};
    vertexOffset += 4;

    // Left face (-X)
    const Vector3 leftNormal = {-1.0f, 0.0f, 0.0f};
    const Vector4 leftTangent = {0.0f, 0.0f, 1.0f, 1.0f};
    mesh.vertices[vertexOffset + 0] = (R3D_Vertex){{-halfW, -halfH, -halfL}, uvs[0], leftNormal, defaultColor, leftTangent};
    mesh.vertices[vertexOffset + 1] = (R3D_Vertex){{-halfW, -halfH, halfL}, uvs[1], leftNormal, defaultColor, leftTangent};
    mesh.vertices[vertexOffset + 2] = (R3D_Vertex){{-halfW, halfH, halfL}, uvs[2], leftNormal, defaultColor, leftTangent};
    mesh.vertices[vertexOffset + 3] = (R3D_Vertex){{-halfW, halfH, -halfL}, uvs[3], leftNormal, defaultColor, leftTangent};
    vertexOffset += 4;

    // Face up (+Y)
    const Vector3 topNormal = {0.0f, 1.0f, 0.0f};
    const Vector4 topTangent = {1.0f, 0.0f, 0.0f, 1.0f};
    mesh.vertices[vertexOffset + 0] = (R3D_Vertex){{-halfW, halfH, halfL}, uvs[0], topNormal, defaultColor, topTangent};
    mesh.vertices[vertexOffset + 1] = (R3D_Vertex){{halfW, halfH, halfL}, uvs[1], topNormal, defaultColor, topTangent};
    mesh.vertices[vertexOffset + 2] = (R3D_Vertex){{halfW, halfH, -halfL}, uvs[2], topNormal, defaultColor, topTangent};
    mesh.vertices[vertexOffset + 3] = (R3D_Vertex){{-halfW, halfH, -halfL}, uvs[3], topNormal, defaultColor, topTangent};
    vertexOffset += 4;

    // Face down (-Y)
    const Vector3 bottomNormal = {0.0f, -1.0f, 0.0f};
    const Vector4 bottomTangent = {1.0f, 0.0f, 0.0f, 1.0f};
    mesh.vertices[vertexOffset + 0] = (R3D_Vertex){{-halfW, -halfH, -halfL}, uvs[0], bottomNormal, defaultColor, bottomTangent};
    mesh.vertices[vertexOffset + 1] = (R3D_Vertex){{halfW, -halfH, -halfL}, uvs[1], bottomNormal, defaultColor, bottomTangent};
    mesh.vertices[vertexOffset + 2] = (R3D_Vertex){{halfW, -halfH, halfL}, uvs[2], bottomNormal, defaultColor, bottomTangent};
    mesh.vertices[vertexOffset + 3] = (R3D_Vertex){{-halfW, -halfH, halfL}, uvs[3], bottomNormal, defaultColor, bottomTangent};

    // Generation of indices (same pattern for each face)
    for (int face = 0; face < 6; face++) {
        const unsigned int baseVertex = face * 4;
        const int baseIndex = face * 6;
    
        // First triangle (0, 1, 2)
        mesh.indices[baseIndex + 0] = baseVertex + 0;
        mesh.indices[baseIndex + 1] = baseVertex + 1;
        mesh.indices[baseIndex + 2] = baseVertex + 2;
    
        // Second triangle (2, 3, 0)
        mesh.indices[baseIndex + 3] = baseVertex + 2;
        mesh.indices[baseIndex + 4] = baseVertex + 3;
        mesh.indices[baseIndex + 5] = baseVertex + 0;
    }

    // AABB Calculation
    mesh.aabb = (BoundingBox){
        .min = {-halfW, -halfH, -halfL},
        .max = {halfW, halfH, halfL}
    };

    // Optional upload to GPU
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshSphere(float radius, int rings, int slices, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Parameter validation
    if (radius <= 0.0f || rings < 2 || slices < 3) return mesh;

    // Calculate mesh dimensions
    mesh.vertexCount = (rings + 1) * (slices + 1);
    mesh.indexCount = rings * slices * 6; // 2 triangles per quad

    // Allocate memory for vertices and indices
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Pre-calculate angular steps and default color
    const float ringStep = PI / rings;        // Vertical angle increment (phi: 0 to PI)
    const float sliceStep = 2.0f * PI / slices; // Horizontal angle increment (theta: 0 to 2PI)
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Generate vertices
    int vertexIndex = 0;
    for (int ring = 0; ring <= rings; ring++) {
        const float phi = ring * ringStep;          // Vertical angle from +Y (North Pole)
        const float sinPhi = sinf(phi);
        const float cosPhi = cosf(phi);

        const float y = radius * cosPhi;            // Y-coordinate (up/down)
        const float ringRadius = radius * sinPhi;   // Radius of the current ring

        const float v = (float)ring / rings;        // V texture coordinate (0 at North Pole, 1 at South Pole)
    
        for (int slice = 0; slice <= slices; slice++) {
            const float theta = slice * sliceStep;   // Horizontal angle (around Y-axis)
            const float sinTheta = sinf(theta);
            const float cosTheta = cosf(theta);
        
            // Calculate vertex position for right-handed, -Z forward, +Y up system
            const float x = ringRadius * cosTheta;
            const float z = ringRadius * -sinTheta; // Invert Z for -Z forward
            
            // Normals point outwards from the sphere center
            const Vector3 normal = {x / radius, y / radius, z / radius};
        
            // UV coordinates
            const float u = (float)slice / slices;
        
            // Calculate tangent vector (points in the direction of increasing U)
            // Adjusted for -Z forward system: tangent = d(position)/d(theta) normalized
            const Vector3 tangentDir = {-sinTheta, 0.0f, -cosTheta};
            const Vector4 tangent = {tangentDir.x, tangentDir.y, tangentDir.z, 1.0f}; // W for bitangent handedness
        
            mesh.vertices[vertexIndex++] = (R3D_Vertex){
                .position = {x, y, z},
                .texcoord = {u, v},
                .normal = normal,
                .color = defaultColor,
                .tangent = tangent
            };
        }
    }

    // Generate indices
    int indexOffset = 0;
    const int verticesPerRing = slices + 1;

    for (int ring = 0; ring < rings; ring++) {
        const int currentRingStartIdx = ring * verticesPerRing;
        const int nextRingStartIdx = (ring + 1) * verticesPerRing;
    
        for (int slice = 0; slice < slices; slice++) {
            // Get indices of the 4 corners of the quad
            const unsigned int current = currentRingStartIdx + slice;
            const unsigned int next = currentRingStartIdx + slice + 1;
            const unsigned int currentNext = nextRingStartIdx + slice;
            const unsigned int nextNext = nextRingStartIdx + slice + 1;
        
            // Define triangles with clockwise winding for back-face culling (right-handed system)
            // First triangle of the quad
            mesh.indices[indexOffset++] = current;
            mesh.indices[indexOffset++] = currentNext;
            mesh.indices[indexOffset++] = nextNext;

            // Second triangle of the quad
            mesh.indices[indexOffset++] = current;
            mesh.indices[indexOffset++] = nextNext;
            mesh.indices[indexOffset++] = next;
        }
    }

    // Set final index count
    mesh.indexCount = indexOffset;

    // Calculate Axis-Aligned Bounding Box (AABB)
    mesh.aabb = (BoundingBox){
        .min = {-radius, -radius, -radius},
        .max = {radius, radius, radius}
    };

    // Optional GPU upload
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshHemiSphere(float radius, int rings, int slices, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Parameter validation
    if (radius <= 0.0f || rings < 1 || slices < 3) return mesh;

    // Calculate vertex counts for hemisphere and base
    const int hemisphereVertexCount = (rings + 1) * (slices + 1);
    const int baseVertexCount = slices + 1; // Circular base includes center + points on edge
    mesh.vertexCount = hemisphereVertexCount + baseVertexCount;

    // Calculate index counts for hemisphere and base
    const int hemisphereIndexCount = rings * slices * 6; // 2 triangles per quad
    const int baseIndexCount = slices * 3;               // 1 triangle per slice for the base
    mesh.indexCount = hemisphereIndexCount + baseIndexCount;

    // Allocate memory
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Pre-compute angles and default color
    const float ringStep = (PI * 0.5f) / rings;   // Vertical angle increment (phi: 0 to PI/2 for hemisphere)
    const float sliceStep = 2.0f * PI / slices;   // Horizontal angle increment (theta: 0 to 2PI)
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Generate hemisphere vertices
    int vertexIndex = 0;
    for (int ring = 0; ring <= rings; ring++) {
        const float phi = ring * ringStep;          // Vertical angle (0 at +Y, PI/2 at Y=0)
        const float sinPhi = sinf(phi);
        const float cosPhi = cosf(phi);
        const float y = radius * cosPhi;            // Y-position (radius down to 0)
        const float ringRadius = radius * sinPhi;   // Radius of the current ring

        const float v = (float)ring / rings;        // V texture coordinate
    
        for (int slice = 0; slice <= slices; slice++) {
            const float theta = slice * sliceStep;   // Horizontal angle
            const float sinTheta = sinf(theta);
            const float cosTheta = cosf(theta);
        
            // Position for right-handed, -Z forward, +Y up
            const float x = ringRadius * cosTheta;
            const float z = ringRadius * -sinTheta; // Invert Z for -Z forward
        
            // Normal (points outwards from the sphere center)
            const Vector3 normal = {x / radius, y / radius, z / radius};
        
            // UV coordinates
            const float u = (float)slice / slices;
        
            // Tangent: adjusted for -Z forward (derivative of position wrt theta)
            const Vector3 tangentDir = {-sinTheta, 0.0f, -cosTheta};
            const Vector4 tangent = {tangentDir.x, tangentDir.y, tangentDir.z, 1.0f};
        
            mesh.vertices[vertexIndex++] = (R3D_Vertex){
                .position = {x, y, z},
                .texcoord = {u, v},
                .normal = normal,
                .color = defaultColor,
                .tangent = tangent
            };
        }
    }

    // Generate base vertices (at y = 0)
    // The base needs a central vertex and vertices around the edge.
    // Let's make the first vertex of the base ring the center.
    const int baseCenterVertexIndex = vertexIndex;
    mesh.vertices[vertexIndex++] = (R3D_Vertex){
        .position = {0.0f, 0.0f, 0.0f},
        .texcoord = {0.5f, 0.5f}, // Center of UV map
        .normal = {0.0f, -1.0f, 0.0f}, // Normal pointing downwards
        .color = defaultColor,
        .tangent = {1.0f, 0.0f, 0.0f, 1.0f} // Arbitrary tangent for a flat surface
    };

    // Then, the perimeter vertices for the base
    for (int slice = 0; slice <= slices; slice++) {
        const float theta = slice * sliceStep;
        const float sinTheta = sinf(theta);
        const float cosTheta = cosf(theta);
    
        const float x = radius * cosTheta;
        const float z = radius * -sinTheta; // Invert Z for consistency
    
        // Circular UV mapping for the base
        const float u = 0.5f + 0.5f * cosTheta;
        const float v = 0.5f + 0.5f * -sinTheta; // Invert V based on Z inversion if desired
                                                 // Or keep it standard for circular mapping: 0.5f + 0.5f * sinTheta;
                                                 // Let's assume standard circular mapping (no direct link to -Z)
    
        mesh.vertices[vertexIndex++] = (R3D_Vertex) {
            .position = {x, 0.0f, z},
            .texcoord = {u, v},
            .normal = {0.0f, -1.0f, 0.0f}, // Normal pointing downwards
            .color = defaultColor,
            .tangent = {1.0f, 0.0f, 0.0f, 1.0f} // Tangent for flat base
        };
    }

    // Generate indices for the hemisphere
    int indexOffset = 0;
    const int verticesPerRing = slices + 1;

    for (int ring = 0; ring < rings; ring++) {
        const int currentRingStartIdx = ring * verticesPerRing;
        const int nextRingStartIdx = (ring + 1) * verticesPerRing;
    
        for (int slice = 0; slice < slices; slice++) {
            const unsigned int current = currentRingStartIdx + slice;
            const unsigned int next = currentRingStartIdx + slice + 1;
            const unsigned int currentNext = nextRingStartIdx + slice;
            const unsigned int nextNext = nextRingStartIdx + slice + 1;
        
            // Triangles with clockwise winding for back-face culling (right-handed system)
            // First triangle of the quad
            mesh.indices[indexOffset++] = current;
            mesh.indices[indexOffset++] = currentNext;
            mesh.indices[indexOffset++] = nextNext;

            // Second triangle of the quad
            mesh.indices[indexOffset++] = current;
            mesh.indices[indexOffset++] = nextNext;
            mesh.indices[indexOffset++] = next;
        }
    }

    // Generate indices for the base
    // The first vertex of the base section (baseCenterVertexIndex) is the center point.
    // The subsequent vertices form the perimeter.
    for (int slice = 0; slice < slices; slice++) {
        // Base vertices start after hemisphere vertices and the center vertex
        const unsigned int currentPerimeter = hemisphereVertexCount + 1 + slice; // +1 to skip center vertex
        const unsigned int nextPerimeter = hemisphereVertexCount + 1 + slice + 1;
    
        // Triangle for the base (clockwise winding when viewed from below, i.e., normal direction)
        mesh.indices[indexOffset++] = currentPerimeter;
        mesh.indices[indexOffset++] = baseCenterVertexIndex; // Center vertex
        mesh.indices[indexOffset++] = nextPerimeter;
    }

    // Set final total index count
    mesh.indexCount = indexOffset;

    // Calculate AABB for the hemisphere
    mesh.aabb = (BoundingBox) {
        .min = {-radius, 0.0f, -radius}, // Y starts at 0 for hemisphere base
        .max = {radius, radius, radius}
    };

    // Optional GPU upload
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshCylinder(float radius, float height, int slices, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Validate parameters
    if (radius <= 0.0f || height <= 0.0f || slices < 3) return mesh;

    // Calculate vertex and index counts
    // Body vertices: 2 rows * (slices+1) vertices (top and bottom per slice)
    // Cap vertices: 2 * (1 center + slices perimeter vertices)
    const int bodyVertexCount = 2 * (slices + 1);
    const int capVertexCount = 2 * (1 + slices);
    mesh.vertexCount = bodyVertexCount + capVertexCount;

    // Indices: body + 2 caps
    const int bodyIndexCount = slices * 6;      // 2 triangles per slice
    const int capIndexCount = 2 * slices * 3;   // slices triangles per cap
    mesh.indexCount = bodyIndexCount + capIndexCount;

    // Allocate memory
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Pre-compute values
    const float halfHeight = height * 0.5f;
    // For -Z forward, +Y up: theta starts at +X and increases toward -Z (clockwise when viewed from above)
    const float sliceStep = 2.0f * PI / slices;
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // White

    // Generate body vertices
    int vertexIndex = 0;

    // Bottom row (y = -halfHeight)
    for (int slice = 0; slice <= slices; slice++) {
        const float theta = slice * sliceStep;
        // For -Z forward: x = cos(theta), z = -sin(theta)
        // This makes theta=0 point toward +X, theta=PI/2 toward -Z
        const float x = radius * cosf(theta);
        const float z = -radius * sinf(theta);

        // Normal: points radially outward
        const Vector3 normal = {cosf(theta), 0.0f, -sinf(theta)};

        // UV mapping: u = angular position, v = height
        const float u = (float)slice / slices;
        const float v = 0.0f; // Bottom of cylinder

        // Tangent: perpendicular to the normal and along the circumference
        // To be consistent with -Z forward, tangent is in the direction of increasing theta
        // If normal is (cos(θ), 0, -sin(θ)), then tangent is (-sin(θ), 0, -cos(θ))
        const Vector4 tangent = {-sinf(theta), 0.0f, -cosf(theta), 1.0f};

        mesh.vertices[vertexIndex] = (R3D_Vertex){
            .position = {x, -halfHeight, z},
            .texcoord = {u, v},
            .normal = normal,
            .color = defaultColor,
            .tangent = tangent
        };
        vertexIndex++;
    }

    // Top row (y = halfHeight)
    for (int slice = 0; slice <= slices; slice++) {
        const float theta = slice * sliceStep;
        const float x = radius * cosf(theta);
        const float z = -radius * sinf(theta);

        // Normal: points outwards
        const Vector3 normal = {cosf(theta), 0.0f, -sinf(theta)};

        const float u = (float)slice / slices;
        const float v = 1.0f; // Top of cylinder

        // Tangent: consistent with bottom row
        const Vector4 tangent = {-sinf(theta), 0.0f, -cosf(theta), 1.0f};

        mesh.vertices[vertexIndex] = (R3D_Vertex){
            .position = {x, halfHeight, z},
            .texcoord = {u, v},
            .normal = normal,
            .color = defaultColor,
            .tangent = tangent
        };
        vertexIndex++;
    }

    // Generate bottom cap vertices
    // Normal points downwards
    const Vector3 bottomNormal = {0.0f, -1.0f, 0.0f};
    // Tangent for a flat surface facing down, +X is a valid tangent
    const Vector4 bottomTangent = {1.0f, 0.0f, 0.0f, 1.0f};

    // Center of bottom cap
    mesh.vertices[vertexIndex] = (R3D_Vertex){
        .position = {0.0f, -halfHeight, 0.0f},
        .texcoord = {0.5f, 0.5f}, // Center of UV space
        .normal = bottomNormal,
        .color = defaultColor,
        .tangent = bottomTangent
    };
    const int bottomCenterIndex = vertexIndex;
    vertexIndex++;

    // Perimeter of bottom cap
    for (int slice = 0; slice < slices; slice++) {
        const float theta = slice * sliceStep;
        const float x = radius * cosf(theta);
        const float z = -radius * sinf(theta); // Consistent with -Z forward

        // Circular UV mapping
        const float u = 0.5f + 0.5f * cosf(theta);
        const float v = 0.5f - 0.5f * sinf(theta); // Flip V for -Z forward

        mesh.vertices[vertexIndex] = (R3D_Vertex){
            .position = {x, -halfHeight, z},
            .texcoord = {u, v},
            .normal = bottomNormal,
            .color = defaultColor,
            .tangent = bottomTangent
        };
        vertexIndex++;
    }

    // Generate top cap vertices
    // Normal points upwards
    const Vector3 topNormal = {0.0f, 1.0f, 0.0f};
    // Tangent for a flat surface facing up, +X is a valid tangent
    const Vector4 topTangent = {1.0f, 0.0f, 0.0f, 1.0f};

    // Center of top cap
    mesh.vertices[vertexIndex] = (R3D_Vertex){
        .position = {0.0f, halfHeight, 0.0f},
        .texcoord = {0.5f, 0.5f},
        .normal = topNormal,
        .color = defaultColor,
        .tangent = topTangent
    };
    const int topCenterIndex = vertexIndex;
    vertexIndex++;

    // Perimeter of top cap
    for (int slice = 0; slice < slices; slice++) {
        const float theta = slice * sliceStep;
        const float x = radius * cosf(theta);
        const float z = -radius * sinf(theta); // Consistent with -Z forward

        // Circular UV mapping
        const float u = 0.5f + 0.5f * cosf(theta);
        const float v = 0.5f - 0.5f * sinf(theta); // Flip V for -Z forward

        mesh.vertices[vertexIndex] = (R3D_Vertex){
            .position = {x, halfHeight, z},
            .texcoord = {u, v},
            .normal = topNormal,
            .color = defaultColor,
            .tangent = topTangent
        };
        vertexIndex++;
    }

    // Generate indices
    int indexOffset = 0;
    const int verticesPerRow = slices + 1; // Vertices in bottom and top rows of cylinder body

    // Body indices (CCW winding from outside)
    // For -Z forward, CCW order from outside remains the same
    for (int slice = 0; slice < slices; slice++) {
        const unsigned int bottomLeft = slice;
        const unsigned int bottomRight = slice + 1;
        const unsigned int topLeft = verticesPerRow + slice;
        const unsigned int topRight = verticesPerRow + slice + 1;

        // First triangle: bottomLeft -> bottomRight -> topRight (CCW from outside)
        mesh.indices[indexOffset++] = bottomLeft;
        mesh.indices[indexOffset++] = bottomRight;
        mesh.indices[indexOffset++] = topRight;

        // Second triangle: bottomLeft -> topRight -> topLeft (CCW from outside)
        mesh.indices[indexOffset++] = bottomLeft;
        mesh.indices[indexOffset++] = topRight;
        mesh.indices[indexOffset++] = topLeft;
    }

    // Bottom cap indices (CCW winding from normal's perspective: looking from -Y up)
    // With -Z forward, the order must be reversed to maintain correct winding
    const int bottomPerimeterStart = bottomCenterIndex + 1;
    for (int slice = 0; slice < slices; slice++) {
        const unsigned int current = bottomPerimeterStart + slice;
        const unsigned int next = bottomPerimeterStart + (slice + 1) % slices;

        // Reverse order for -Z forward: center -> next -> current
        mesh.indices[indexOffset++] = bottomCenterIndex;
        mesh.indices[indexOffset++] = next;
        mesh.indices[indexOffset++] = current;
    }

    // Top cap indices (CCW winding from normal's perspective: looking from +Y down)
    // With -Z forward, the order must be reversed to maintain correct winding
    const int topPerimeterStart = topCenterIndex + 1;
    for (int slice = 0; slice < slices; slice++) {
        const unsigned int current = topPerimeterStart + slice;
        const unsigned int next = topPerimeterStart + (slice + 1) % slices;

        // Reverse order for -Z forward: center -> current -> next
        mesh.indices[indexOffset++] = topCenterIndex;
        mesh.indices[indexOffset++] = current;
        mesh.indices[indexOffset++] = next;
    }

    // Calculate AABB
    mesh.aabb = (BoundingBox){
        .min = {-radius, -halfHeight, -radius},
        .max = {radius, halfHeight, radius}
    };

    // Optional upload to GPU
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshCone(float radius, float height, int slices, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Validate parameters
    if (radius <= 0.0f || height <= 0.0f || slices < 3) return mesh;

    // Vertex counts
    // Side: slices+1 base vertices + 1 tip vertex
    // Base: 1 center vertex + slices perimeter
    const int sideVertexCount = slices + 1 + 1;  // base + tip
    const int baseVertexCount = 1 + slices;      // center + perimeter
    mesh.vertexCount = sideVertexCount + baseVertexCount;

    // Index counts
    // Side: slices triangles
    // Base: slices triangles
    const int sideIndexCount = slices * 3;
    const int baseIndexCount = slices * 3;
    mesh.indexCount = sideIndexCount + baseIndexCount;

    // Memory allocation
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    const float halfHeight = height * 0.5f;
    const float sliceStep = 2.0f * PI / slices;
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // White

    int vertexIndex = 0;

    // Base ring vertices (shared between side and base)
    for (int slice = 0; slice <= slices; slice++) {
        const float theta = slice * sliceStep;
        const float x = radius * cosf(theta);
        const float z = -radius * sinf(theta); // -Z forward

        const float u = (float)slice / slices;
        const float v = 0.0f;

        const Vector3 pos = {x, -halfHeight, z};
        const Vector3 normal = {cosf(theta), radius / height, -sinf(theta)};
        const float len = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        const Vector3 norm = {normal.x / len, normal.y / len, normal.z / len};

        const Vector4 tangent = {-sinf(theta), 0.0f, -cosf(theta), 1.0f};

        mesh.vertices[vertexIndex++] = (R3D_Vertex){
            .position = pos,
            .texcoord = {u, 0.0f},
            .normal = norm,
            .color = defaultColor,
            .tangent = tangent
        };
    }

    // Tip of the cone (at y = +halfHeight)
    const int tipIndex = vertexIndex;
    mesh.vertices[vertexIndex++] = (R3D_Vertex){
        .position = {0.0f, halfHeight, 0.0f},
        .texcoord = {0.5f, 1.0f},
        .normal = {0.0f, 1.0f, 0.0f}, // Rough default normal
        .color = defaultColor,
        .tangent = {1.0f, 0.0f, 0.0f, 1.0f}
    };

    // Base center vertex
    const int baseCenterIndex = vertexIndex;
    mesh.vertices[vertexIndex++] = (R3D_Vertex){
        .position = {0.0f, -halfHeight, 0.0f},
        .texcoord = {0.5f, 0.5f},
        .normal = {0.0f, -1.0f, 0.0f},
        .color = defaultColor,
        .tangent = {1.0f, 0.0f, 0.0f, 1.0f}
    };

    // Base perimeter
    for (int slice = 0; slice < slices; slice++) {
        const float theta = slice * sliceStep;
        const float x = radius * cosf(theta);
        const float z = -radius * sinf(theta);

        const float u = 0.5f + 0.5f * cosf(theta);
        const float v = 0.5f - 0.5f * sinf(theta);

        mesh.vertices[vertexIndex++] = (R3D_Vertex){
            .position = {x, -halfHeight, z},
            .texcoord = {u, v},
            .normal = {0.0f, -1.0f, 0.0f},
            .color = defaultColor,
            .tangent = {1.0f, 0.0f, 0.0f, 1.0f}
        };
    }

    // Indices
    int index = 0;

    // Side triangles (each slice connects base to tip)
    for (int slice = 0; slice < slices; slice++) {
        const unsigned int base0 = slice;
        const unsigned int base1 = slice + 1;
        mesh.indices[index++] = base0;
        mesh.indices[index++] = base1;
        mesh.indices[index++] = tipIndex; // All triangles meet at the tip
    }

    // Base triangles (CCW order from below, so reversed here)
    const int basePerimeterStart = baseCenterIndex + 1;
    for (int slice = 0; slice < slices; slice++) {
        const unsigned int curr = basePerimeterStart + slice;
        const unsigned int next = basePerimeterStart + ((slice + 1) % slices);
        mesh.indices[index++] = baseCenterIndex;
        mesh.indices[index++] = next;
        mesh.indices[index++] = curr;
    }

    // AABB
    mesh.aabb = (BoundingBox){
        .min = {-radius, -halfHeight, -radius},
        .max = {radius, halfHeight, radius}
    };

    // Upload if requested
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshTorus(float radius, float size, int radSeg, int sides, bool upload)
{
    R3D_Mesh mesh = { 0 };

    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3) {
        return mesh;
    }

    const int rings = radSeg + 1;
    const int segments = sides + 1;

    mesh.vertexCount = rings * segments;
    mesh.indexCount = radSeg * sides * 6;

    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    const float ringStep = 2.0f * PI / radSeg;
    const float sideStep = 2.0f * PI / sides;
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    int vertexIndex = 0;

    for (int ring = 0; ring <= radSeg; ring++) {
        float theta = ring * ringStep;
        float cosTheta = cosf(theta);
        float sinTheta = sinf(theta);

        // Center of current ring
        Vector3 ringCenter = {
            radius * cosTheta,
            0.0f,
            -radius * sinTheta
        };

        for (int side = 0; side <= sides; side++) {
            float phi = side * sideStep;
            float cosPhi = cosf(phi);
            float sinPhi = sinf(phi);

            // Normal at vertex
            Vector3 normal = {
                cosTheta * cosPhi,
                sinPhi,
                -sinTheta * cosPhi
            };

            // Position = ringCenter + normal * size
            Vector3 pos = {
                ringCenter.x + size * normal.x,
                ringCenter.y + size * normal.y,
                ringCenter.z + size * normal.z
            };

            // Tangent along ring (around main circle)
            Vector4 tangent = {
                -sinTheta, 0.0f, -cosTheta, 1.0f
            };

            // UV coordinates
            float u = (float)ring / radSeg;
            float v = (float)side / sides;

            mesh.vertices[vertexIndex++] = (R3D_Vertex){
                .position = pos,
                .texcoord = {u, v},
                .normal = normal,
                .color = defaultColor,
                .tangent = tangent
            };
        }
    }

    // Indices
    int index = 0;
    for (int ring = 0; ring < radSeg; ring++) {
        for (int side = 0; side < sides; side++) {
            int current = ring * segments + side;
            int next = (ring + 1) * segments + side;

            // Triangle 1
            mesh.indices[index++] = current;
            mesh.indices[index++] = next;
            mesh.indices[index++] = next + 1;

            // Triangle 2
            mesh.indices[index++] = current;
            mesh.indices[index++] = next + 1;
            mesh.indices[index++] = current + 1;
        }
    }

    // AABB
    float outerRadius = radius + size;
    mesh.aabb = (BoundingBox){
        .min = {-outerRadius, -size, -outerRadius},
        .max = { outerRadius,  size,  outerRadius}
    };

    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshKnot(float radius, float tubeRadius, int segments, int sides, bool upload)
{
    R3D_Mesh mesh = { 0 };

    if (radius <= 0.0f || tubeRadius <= 0.0f || segments < 6 || sides < 3) {
        return mesh;
    }

    const int knotSegments = segments + 1;
    const int tubeSides = sides + 1;

    mesh.vertexCount = knotSegments * tubeSides;
    mesh.indexCount = segments * sides * 6;

    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    const float segmentStep = 2.0f * PI / segments;
    const float sideStep = 2.0f * PI / sides;
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    int vertexIndex = 0;

    for (int seg = 0; seg <= segments; seg++) {
        float t = seg * segmentStep;
        
        // Trefoil knot parametric equations
        float x = sinf(t) + 2.0f * sinf(2.0f * t);
        float y = cosf(t) - 2.0f * cosf(2.0f * t);
        float z = -sinf(3.0f * t);
        
        // Scale by radius
        Vector3 knotCenter = {
            radius * x * 0.2f,  // Scale factor to normalize the knot size
            radius * y * 0.2f,
            radius * z * 0.2f
        };

        // Calculate tangent vector (derivative of knot curve)
        float dx = cosf(t) + 4.0f * cosf(2.0f * t);
        float dy = -sinf(t) + 4.0f * sinf(2.0f * t);
        float dz = -3.0f * cosf(3.0f * t);
        
        Vector3 tangent = {dx, dy, dz};
        
        // Normalize tangent
        float tangentLength = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
        if (tangentLength > 0.0f) {
            tangent.x /= tangentLength;
            tangent.y /= tangentLength;
            tangent.z /= tangentLength;
        }

        // Calculate binormal (second derivative for better frame)
        float d2x = -sinf(t) - 8.0f * sinf(2.0f * t);
        float d2y = -cosf(t) + 8.0f * cosf(2.0f * t);
        float d2z = 9.0f * sinf(3.0f * t);
        
        Vector3 binormal = {d2x, d2y, d2z};
        
        // Normalize binormal
        float binormalLength = sqrtf(binormal.x * binormal.x + binormal.y * binormal.y + binormal.z * binormal.z);
        if (binormalLength > 0.0f) {
            binormal.x /= binormalLength;
            binormal.y /= binormalLength;
            binormal.z /= binormalLength;
        }

        // Calculate normal (cross product of tangent and binormal)
        Vector3 normal = {
            tangent.y * binormal.z - tangent.z * binormal.y,
            tangent.z * binormal.x - tangent.x * binormal.z,
            tangent.x * binormal.y - tangent.y * binormal.x
        };

        for (int side = 0; side <= sides; side++) {
            float phi = side * sideStep;
            float cosPhi = cosf(phi);
            float sinPhi = sinf(phi);

            // Create tube cross-section
            Vector3 tubeOffset = {
                tubeRadius * (normal.x * cosPhi + binormal.x * sinPhi),
                tubeRadius * (normal.y * cosPhi + binormal.y * sinPhi),
                tubeRadius * (normal.z * cosPhi + binormal.z * sinPhi)
            };

            // Final vertex position
            Vector3 pos = {
                knotCenter.x + tubeOffset.x,
                knotCenter.y + tubeOffset.y,
                knotCenter.z + tubeOffset.z
            };

            // Surface normal at this point
            Vector3 surfaceNormal = {
                normal.x * cosPhi + binormal.x * sinPhi,
                normal.y * cosPhi + binormal.y * sinPhi,
                normal.z * cosPhi + binormal.z * sinPhi
            };

            // Tangent along the knot curve
            Vector4 vertexTangent = {
                tangent.x, tangent.y, tangent.z, 1.0f
            };

            // UV coordinates
            float u = (float)seg / segments;
            float v = (float)side / sides;

            mesh.vertices[vertexIndex++] = (R3D_Vertex){
                .position = pos,
                .texcoord = {u, v},
                .normal = surfaceNormal,
                .color = defaultColor,
                .tangent = vertexTangent
            };
        }
    }

    // Generate indices
    int index = 0;
    for (int seg = 0; seg < segments; seg++) {
        for (int side = 0; side < sides; side++) {
            int current = seg * tubeSides + side;
            int next = (seg + 1) * tubeSides + side;

            // Triangle 1
            mesh.indices[index++] = current;
            mesh.indices[index++] = next;
            mesh.indices[index++] = next + 1;

            // Triangle 2
            mesh.indices[index++] = current;
            mesh.indices[index++] = next + 1;
            mesh.indices[index++] = current + 1;
        }
    }

    // Calculate AABB (approximate bounds for trefoil knot)
    float maxExtent = radius * 0.6f + tubeRadius;  // Approximate max extent
    mesh.aabb = (BoundingBox){
        .min = {-maxExtent, -maxExtent, -maxExtent},
        .max = { maxExtent,  maxExtent,  maxExtent}
    };

    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshHeightmap(Image heightmap, Vector3 size, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Parameter validation
    if (heightmap.data == NULL || heightmap.width <= 1 || heightmap.height <= 1 ||
        size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) {
        return mesh;
    }

    // Heightmap dimensions
    const int mapWidth = heightmap.width;
    const int mapHeight = heightmap.height;

    // Mesh dimensions calculation
    mesh.vertexCount = mapWidth * mapHeight;
    mesh.indexCount = (mapWidth - 1) * (mapHeight - 1) * 6; // 2 triangles per quad

    // Memory allocation
    mesh.vertices = (R3D_Vertex*)malloc(mesh.vertexCount * sizeof(R3D_Vertex));
    mesh.indices = (unsigned int*)malloc(mesh.indexCount * sizeof(unsigned int));

    if (!mesh.vertices || !mesh.indices) {
        if (mesh.vertices) free(mesh.vertices);
        if (mesh.indices) free(mesh.indices);
        return mesh;
    }

    // Precompute some values
    const float halfSizeX = size.x * 0.5f;
    const float halfSizeZ = size.z * 0.5f;
    const float stepX = size.x / (mapWidth - 1);
    const float stepZ = size.z / (mapHeight - 1);
    const float stepU = 1.0f / (mapWidth - 1);
    const float stepV = 1.0f / (mapHeight - 1);
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Macro to extract height from a pixel
    #define GET_HEIGHT_VALUE(x, y) \
        ((x) < 0 || (x) >= mapWidth || (y) < 0 || (y) >= mapHeight) \
            ? 0.0f : ((float)GetImageColor(heightmap, x, y).r / 255)

    // Generate vertices
    int vertexIndex = 0;
    float minY = FLT_MAX, maxY = -FLT_MAX;

    for (int z = 0; z < mapHeight; z++) {
        for (int x = 0; x < mapWidth; x++) {
            // Vertex position
            const float posX = -halfSizeX + x * stepX;
            const float posZ = -halfSizeZ + z * stepZ;
            const float posY = GET_HEIGHT_VALUE(x, z);

            // Update Y bounds for AABB
            if (posY < minY) minY = posY;
            if (posY > maxY) maxY = posY;

            // Calculate normal by finite differences (gradient method)
            const float heightL = GET_HEIGHT_VALUE(x - 1, z);     // Left
            const float heightR = GET_HEIGHT_VALUE(x + 1, z);     // Right
            const float heightD = GET_HEIGHT_VALUE(x, z - 1);     // Down
            const float heightU = GET_HEIGHT_VALUE(x, z + 1);     // Up

            // Gradient in X and Z
            const float gradX = (heightR - heightL) / (2.0f * stepX);
            const float gradZ = (heightU - heightD) / (2.0f * stepZ);

            // Normal (cross product of tangent vectors)
            const Vector3 normal = {
                -gradX,
                1.0f,
                -gradZ
            };

            // Normalize the normal
            const float normalLength = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            const Vector3 normalizedNormal = {
                normal.x / normalLength,
                normal.y / normalLength,
                normal.z / normalLength
            };

            // UV mapping
            const float u = x * stepU;
            const float v = z * stepV;

            // Tangent (X direction in texture space)
            const Vector3 tangentDir = {1.0f, gradX, 0.0f};
            const float tangentLength = sqrtf(tangentDir.x * tangentDir.x + tangentDir.y * tangentDir.y + tangentDir.z * tangentDir.z);
            const Vector4 tangent = {
                tangentDir.x / tangentLength,
                tangentDir.y / tangentLength,
                tangentDir.z / tangentLength,
                1.0f
            };

            // Color based on height (optional)
            const float heightRatio = (posY - minY) / (size.y > 0.0f ? size.y : 1.0f);
            const unsigned char colorIntensity = (unsigned char)(255.0f * heightRatio);
            const Vector4 heightColor = {colorIntensity, colorIntensity, colorIntensity, 255};

            mesh.vertices[vertexIndex] = (R3D_Vertex){
                .position = {posX, posY, posZ},
                .texcoord = {u, v},
                .normal = normalizedNormal,
                .color = heightColor,
                .tangent = tangent
            };
            vertexIndex++;
        }
    }

    // Generate indices
    int indexOffset = 0;
    for (int z = 0; z < mapHeight - 1; z++) {
        for (int x = 0; x < mapWidth - 1; x++) {
            // Indices of the 4 corners of the current quad
            const unsigned int topLeft = z * mapWidth + x;
            const unsigned int topRight = topLeft + 1;
            const unsigned int bottomLeft = (z + 1) * mapWidth + x;
            const unsigned int bottomRight = bottomLeft + 1;
        
            // First triangle (topLeft, bottomLeft, topRight)
            mesh.indices[indexOffset++] = topLeft;
            mesh.indices[indexOffset++] = bottomLeft;
            mesh.indices[indexOffset++] = topRight;
        
            // Second triangle (topRight, bottomLeft, bottomRight)
            mesh.indices[indexOffset++] = topRight;
            mesh.indices[indexOffset++] = bottomLeft;
            mesh.indices[indexOffset++] = bottomRight;
        }
    }

    // Calculate AABB
    mesh.aabb = (BoundingBox){
        .min = {-halfSizeX, minY, -halfSizeZ},
        .max = {halfSizeX, maxY, halfSizeZ}
    };

    // Cleanup macro
    #undef GET_HEIGHT_VALUE

    // Optional upload to GPU
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

R3D_Mesh R3D_GenMeshCubicmap(Image cubicmap, Vector3 cubeSize, bool upload)
{
    R3D_Mesh mesh = { 0 };

    // Validation of parameters
    if (cubicmap.width <= 0 || cubicmap.height <= 0 || 
        cubeSize.x <= 0.0f || cubeSize.y <= 0.0f || cubeSize.z <= 0.0f) {
        return mesh;
    }

    Color* pixels = LoadImageColors(cubicmap);
    if (!pixels) return mesh;

    // Pre-compute some values
    const float halfW = cubeSize.x * 0.5f;
    const float halfH = cubeSize.y * 0.5f;  // height
    const float halfL = cubeSize.z * 0.5f;
    const Vector4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Normals of the 6 faces of the cube
    const Vector3 normals[6] = {
        {1.0f, 0.0f, 0.0f},   // right (+X)
        {-1.0f, 0.0f, 0.0f},  // left (-X)
        {0.0f, 1.0f, 0.0f},   // up (+Y)
        {0.0f, -1.0f, 0.0f},  // down (-Y)
        {0.0f, 0.0f, -1.0f},  // forward (-Z)
        {0.0f, 0.0f, 1.0f}    // backward (+Z)
    };

    // Corresponding tangents
    const Vector4 tangents[6] = {
        {0.0f, 0.0f, -1.0f, 1.0f},  // right
        {0.0f, 0.0f, 1.0f, 1.0f},   // left
        {1.0f, 0.0f, 0.0f, 1.0f},   // up
        {1.0f, 0.0f, 0.0f, 1.0f},   // down
        {-1.0f, 0.0f, 0.0f, 1.0f},  // forward
        {1.0f, 0.0f, 0.0f, 1.0f}    // backward
    };

    // UV coordinates for the 6 faces (2x3 atlas texture)
    typedef struct { float x, y, width, height; } RectangleF;
    const RectangleF texUVs[6] = {
        {0.0f, 0.0f, 0.5f, 0.5f},    // right
        {0.5f, 0.0f, 0.5f, 0.5f},    // left
        {0.0f, 0.5f, 0.5f, 0.5f},    // up
        {0.5f, 0.5f, 0.5f, 0.5f},    // down
        {0.5f, 0.0f, 0.5f, 0.5f},    // backward
        {0.0f, 0.0f, 0.5f, 0.5f}     // forward
    };

    // Estimate the maximum number of faces needed
    int maxFaces = 0;
    for (int z = 0; z < cubicmap.height; z++) {
        for (int x = 0; x < cubicmap.width; x++) {
            Color pixel = pixels[z * cubicmap.width + x];
            if (ColorIsEqual(pixel, WHITE)) {
                maxFaces += 6; // complete cube
            } else if (ColorIsEqual(pixel, BLACK)) {
                maxFaces += 2; // floor and ceiling only
            }
        }
    }

    // Allocation of temporary tables
    R3D_Vertex* vertices = (R3D_Vertex*)malloc(maxFaces * 4 * sizeof(R3D_Vertex));
    unsigned int* indices = (unsigned int*)malloc(maxFaces * 6 * sizeof(unsigned int));

    if (!vertices || !indices) {
        if (vertices) free(vertices);
        if (indices) free(indices);
        UnloadImageColors(pixels);
        return mesh;
    }

    int vertexCount = 0;
    int indexCount = 0;

    // Variables for calculating AABB
    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
    float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

    // Mesh generation
    for (int z = 0; z < cubicmap.height; z++) {
        for (int x = 0; x < cubicmap.width; x++) {
            Color pixel = pixels[z * cubicmap.width + x];

            // Position of the center of the cube
            float posX = cubeSize.x * (x - cubicmap.width * 0.5f + 0.5f);
            float posZ = cubeSize.z * (z - cubicmap.height * 0.5f + 0.5f);

            // AABB Update
            minX = fminf(minX, posX - halfW);
            maxX = fmaxf(maxX, posX + halfW);
            minZ = fminf(minZ, posZ - halfL);
            maxZ = fmaxf(maxZ, posZ + halfL);

            if (ColorIsEqual(pixel, WHITE)) {
                // Complete cube - generate all necessary faces
                minY = fminf(minY, 0.0f);
                maxY = fmaxf(maxY, cubeSize.y);

                // Face up (always generated for white cubes)
                if (true) { // Top side still visible
                    Vector2 uvs[4] = {
                        {texUVs[2].x, texUVs[2].y},
                        {texUVs[2].x, texUVs[2].y + texUVs[2].height},
                        {texUVs[2].x + texUVs[2].width, texUVs[2].y + texUVs[2].height},
                        {texUVs[2].x + texUVs[2].width, texUVs[2].y}
                    };

                    vertices[vertexCount + 0] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ - halfL}, uvs[0], normals[2], defaultColor, tangents[2]};
                    vertices[vertexCount + 1] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ + halfL}, uvs[1], normals[2], defaultColor, tangents[2]};
                    vertices[vertexCount + 2] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ + halfL}, uvs[2], normals[2], defaultColor, tangents[2]};
                    vertices[vertexCount + 3] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ - halfL}, uvs[3], normals[2], defaultColor, tangents[2]};

                    // Clues for 2 triangles
                    indices[indexCount + 0] = vertexCount + 0;
                    indices[indexCount + 1] = vertexCount + 1;
                    indices[indexCount + 2] = vertexCount + 2;
                    indices[indexCount + 3] = vertexCount + 2;
                    indices[indexCount + 4] = vertexCount + 3;
                    indices[indexCount + 5] = vertexCount + 0;

                    vertexCount += 4;
                    indexCount += 6;
                }

                // Face down
                if (true) {
                    Vector2 uvs[4] = {
                        {texUVs[3].x + texUVs[3].width, texUVs[3].y},
                        {texUVs[3].x, texUVs[3].y + texUVs[3].height},
                        {texUVs[3].x + texUVs[3].width, texUVs[3].y + texUVs[3].height},
                        {texUVs[3].x, texUVs[3].y}
                    };

                    vertices[vertexCount + 0] = (R3D_Vertex){{posX - halfW, 0.0f, posZ - halfL}, uvs[0], normals[3], defaultColor, tangents[3]};
                    vertices[vertexCount + 1] = (R3D_Vertex){{posX + halfW, 0.0f, posZ + halfL}, uvs[1], normals[3], defaultColor, tangents[3]};
                    vertices[vertexCount + 2] = (R3D_Vertex){{posX - halfW, 0.0f, posZ + halfL}, uvs[2], normals[3], defaultColor, tangents[3]};
                    vertices[vertexCount + 3] = (R3D_Vertex){{posX + halfW, 0.0f, posZ - halfL}, uvs[3], normals[3], defaultColor, tangents[3]};

                    indices[indexCount + 0] = vertexCount + 0;
                    indices[indexCount + 1] = vertexCount + 1;
                    indices[indexCount + 2] = vertexCount + 2;
                    indices[indexCount + 3] = vertexCount + 0;
                    indices[indexCount + 4] = vertexCount + 3;
                    indices[indexCount + 5] = vertexCount + 1;

                    vertexCount += 4;
                    indexCount += 6;
                }

                // Checking the lateral faces (occlusion culling)

                // Back face (+Z)
                if ((z == cubicmap.height - 1) || !ColorIsEqual(pixels[(z + 1) * cubicmap.width + x], WHITE)) {
                    Vector2 uvs[4] = {
                        {texUVs[5].x, texUVs[5].y},
                        {texUVs[5].x, texUVs[5].y + texUVs[5].height},
                        {texUVs[5].x + texUVs[5].width, texUVs[5].y},
                        {texUVs[5].x + texUVs[5].width, texUVs[5].y + texUVs[5].height}
                    };

                    vertices[vertexCount + 0] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ + halfL}, uvs[0], normals[5], defaultColor, tangents[5]};
                    vertices[vertexCount + 1] = (R3D_Vertex){{posX - halfW, 0.0f, posZ + halfL}, uvs[1], normals[5], defaultColor, tangents[5]};
                    vertices[vertexCount + 2] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ + halfL}, uvs[2], normals[5], defaultColor, tangents[5]};
                    vertices[vertexCount + 3] = (R3D_Vertex){{posX + halfW, 0.0f, posZ + halfL}, uvs[3], normals[5], defaultColor, tangents[5]};

                    indices[indexCount + 0] = vertexCount + 0;
                    indices[indexCount + 1] = vertexCount + 1;
                    indices[indexCount + 2] = vertexCount + 2;
                    indices[indexCount + 3] = vertexCount + 2;
                    indices[indexCount + 4] = vertexCount + 1;
                    indices[indexCount + 5] = vertexCount + 3;

                    vertexCount += 4;
                    indexCount += 6;
                }

                // Front face (-Z)
                if ((z == 0) || !ColorIsEqual(pixels[(z - 1) * cubicmap.width + x], WHITE)) {
                    Vector2 uvs[4] = {
                        {texUVs[4].x + texUVs[4].width, texUVs[4].y},
                        {texUVs[4].x, texUVs[4].y + texUVs[4].height},
                        {texUVs[4].x + texUVs[4].width, texUVs[4].y + texUVs[4].height},
                        {texUVs[4].x, texUVs[4].y}
                    };

                    vertices[vertexCount + 0] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ - halfL}, uvs[0], normals[4], defaultColor, tangents[4]};
                    vertices[vertexCount + 1] = (R3D_Vertex){{posX - halfW, 0.0f, posZ - halfL}, uvs[1], normals[4], defaultColor, tangents[4]};
                    vertices[vertexCount + 2] = (R3D_Vertex){{posX + halfW, 0.0f, posZ - halfL}, uvs[2], normals[4], defaultColor, tangents[4]};
                    vertices[vertexCount + 3] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ - halfL}, uvs[3], normals[4], defaultColor, tangents[4]};

                    indices[indexCount + 0] = vertexCount + 0;
                    indices[indexCount + 1] = vertexCount + 1;
                    indices[indexCount + 2] = vertexCount + 2;
                    indices[indexCount + 3] = vertexCount + 0;
                    indices[indexCount + 4] = vertexCount + 3;
                    indices[indexCount + 5] = vertexCount + 1;

                    vertexCount += 4;
                    indexCount += 6;
                }

                // Right face (+X)
                if ((x == cubicmap.width - 1) || !ColorIsEqual(pixels[z * cubicmap.width + (x + 1)], WHITE)) {
                    Vector2 uvs[4] = {
                        {texUVs[0].x, texUVs[0].y},
                        {texUVs[0].x, texUVs[0].y + texUVs[0].height},
                        {texUVs[0].x + texUVs[0].width, texUVs[0].y},
                        {texUVs[0].x + texUVs[0].width, texUVs[0].y + texUVs[0].height}
                    };

                    vertices[vertexCount + 0] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ + halfL}, uvs[0], normals[0], defaultColor, tangents[0]};
                    vertices[vertexCount + 1] = (R3D_Vertex){{posX + halfW, 0.0f, posZ + halfL}, uvs[1], normals[0], defaultColor, tangents[0]};
                    vertices[vertexCount + 2] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ - halfL}, uvs[2], normals[0], defaultColor, tangents[0]};
                    vertices[vertexCount + 3] = (R3D_Vertex){{posX + halfW, 0.0f, posZ - halfL}, uvs[3], normals[0], defaultColor, tangents[0]};

                    indices[indexCount + 0] = vertexCount + 0;
                    indices[indexCount + 1] = vertexCount + 1;
                    indices[indexCount + 2] = vertexCount + 2;
                    indices[indexCount + 3] = vertexCount + 2;
                    indices[indexCount + 4] = vertexCount + 1;
                    indices[indexCount + 5] = vertexCount + 3;

                    vertexCount += 4;
                    indexCount += 6;
                }

                // Left face (-X)
                if ((x == 0) || !ColorIsEqual(pixels[z * cubicmap.width + (x - 1)], WHITE)) {
                    Vector2 uvs[4] = {
                        {texUVs[1].x, texUVs[1].y},
                        {texUVs[1].x + texUVs[1].width, texUVs[1].y + texUVs[1].height},
                        {texUVs[1].x + texUVs[1].width, texUVs[1].y},
                        {texUVs[1].x, texUVs[1].y + texUVs[1].height}
                    };

                    vertices[vertexCount + 0] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ - halfL}, uvs[0], normals[1], defaultColor, tangents[1]};
                    vertices[vertexCount + 1] = (R3D_Vertex){{posX - halfW, 0.0f, posZ + halfL}, uvs[1], normals[1], defaultColor, tangents[1]};
                    vertices[vertexCount + 2] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ + halfL}, uvs[2], normals[1], defaultColor, tangents[1]};
                    vertices[vertexCount + 3] = (R3D_Vertex){{posX - halfW, 0.0f, posZ - halfL}, uvs[3], normals[1], defaultColor, tangents[1]};

                    indices[indexCount + 0] = vertexCount + 0;
                    indices[indexCount + 1] = vertexCount + 1;
                    indices[indexCount + 2] = vertexCount + 2;
                    indices[indexCount + 3] = vertexCount + 0;
                    indices[indexCount + 4] = vertexCount + 3;
                    indices[indexCount + 5] = vertexCount + 1;

                    vertexCount += 4;
                    indexCount += 6;
                }
            }
            else if (ColorIsEqual(pixel, BLACK)) {
                // Black pixel - generate only the floor and ceiling
                minY = fminf(minY, 0.0f);
                maxY = fmaxf(maxY, cubeSize.y);

                // Ceiling face (inverted to be visible from below)
                Vector2 uvs_top[4] = {
                    {texUVs[2].x, texUVs[2].y},
                    {texUVs[2].x + texUVs[2].width, texUVs[2].y + texUVs[2].height},
                    {texUVs[2].x, texUVs[2].y + texUVs[2].height},
                    {texUVs[2].x + texUVs[2].width, texUVs[2].y}
                };

                vertices[vertexCount + 0] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ - halfL}, uvs_top[0], normals[3], defaultColor, tangents[3]};
                vertices[vertexCount + 1] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ + halfL}, uvs_top[1], normals[3], defaultColor, tangents[3]};
                vertices[vertexCount + 2] = (R3D_Vertex){{posX - halfW, cubeSize.y, posZ + halfL}, uvs_top[2], normals[3], defaultColor, tangents[3]};
                vertices[vertexCount + 3] = (R3D_Vertex){{posX + halfW, cubeSize.y, posZ - halfL}, uvs_top[3], normals[3], defaultColor, tangents[3]};

                indices[indexCount + 0] = vertexCount + 0;
                indices[indexCount + 1] = vertexCount + 1;
                indices[indexCount + 2] = vertexCount + 2;
                indices[indexCount + 3] = vertexCount + 0;
                indices[indexCount + 4] = vertexCount + 3;
                indices[indexCount + 5] = vertexCount + 1;

                vertexCount += 4;
                indexCount += 6;

                // Ground face
                Vector2 uvs_bottom[4] = {
                    {texUVs[3].x + texUVs[3].width, texUVs[3].y},
                    {texUVs[3].x + texUVs[3].width, texUVs[3].y + texUVs[3].height},
                    {texUVs[3].x, texUVs[3].y + texUVs[3].height},
                    {texUVs[3].x, texUVs[3].y}
                };

                vertices[vertexCount + 0] = (R3D_Vertex){{posX - halfW, 0.0f, posZ - halfL}, uvs_bottom[0], normals[2], defaultColor, tangents[2]};
                vertices[vertexCount + 1] = (R3D_Vertex){{posX - halfW, 0.0f, posZ + halfL}, uvs_bottom[1], normals[2], defaultColor, tangents[2]};
                vertices[vertexCount + 2] = (R3D_Vertex){{posX + halfW, 0.0f, posZ + halfL}, uvs_bottom[2], normals[2], defaultColor, tangents[2]};
                vertices[vertexCount + 3] = (R3D_Vertex){{posX + halfW, 0.0f, posZ - halfL}, uvs_bottom[3], normals[2], defaultColor, tangents[2]};

                indices[indexCount + 0] = vertexCount + 0;
                indices[indexCount + 1] = vertexCount + 1;
                indices[indexCount + 2] = vertexCount + 2;
                indices[indexCount + 3] = vertexCount + 2;
                indices[indexCount + 4] = vertexCount + 3;
                indices[indexCount + 5] = vertexCount + 0;

                vertexCount += 4;
                indexCount += 6;
            }
        }
    }

    // Final mesh allocation
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.vertices = vertices;
    mesh.indices = indices;

    // Copy of final data
    memcpy(mesh.vertices, vertices, vertexCount * sizeof(R3D_Vertex));
    memcpy(mesh.indices, indices, indexCount * sizeof(unsigned int));

    // AABB Configuration
    mesh.aabb = (BoundingBox){
        .min = {minX, minY, minZ},
        .max = {maxX, maxY, maxZ}
    };

    // Cleaning
    UnloadImageColors(pixels);

    // Optional upload to GPU
    if (upload) {
        R3D_UploadMesh(&mesh, false);
    }

    return mesh;
}

void R3D_UnloadMesh(const R3D_Mesh* mesh)
{
    if ((mesh)->ebo != 0) {
        glDeleteBuffers(1, &mesh->ebo);
    }

    if ((mesh)->vbo != 0) {
        glDeleteBuffers(1, &mesh->vbo);
    }

    if ((mesh)->vao != 0) {
        glDeleteVertexArrays(1, &mesh->vao);
    }

    free(mesh->indices);
    free(mesh->vertices);
    free(mesh->boneMatrices);
}

bool R3D_UploadMesh(R3D_Mesh* mesh, bool dynamic)
{
    if (!mesh || mesh->vertexCount <= 0 || !mesh->vertices) {
        TraceLog(LOG_WARNING, "R3D: Invalid mesh data passed to R3D_UploadMesh");
        return false;
    }

    // Prevent re-upload if VAO already generated
    if (mesh->vao != 0) {
        TraceLog(LOG_WARNING, "R3D: Mesh already uploaded, use R3D_UpdateMesh to update the mesh");
        return false;
    }

    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    // Creation of the VAO
    glGenVertexArrays(1, &mesh->vao);
    glBindVertexArray(mesh->vao);

    // Creation of the VBO
    glGenBuffers(1, &mesh->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh->vertexCount * sizeof(R3D_Vertex), mesh->vertices, usage);

    // position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, position));

    // texcoord (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, texcoord));

    // normal (vec3)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, normal));

    // color (vec4)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, color));

    // tangent (vec4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, tangent));

    // boneIds (ivec4)
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_INT, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, boneIds));

    // weights (vec4)
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(R3D_Vertex), (void*)offsetof(R3D_Vertex, weights));

    // EBO if indices present
    if (mesh->indexCount > 0 && mesh->indices) {
        glGenBuffers(1, &mesh->ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indexCount * sizeof(unsigned int), mesh->indices, usage);
    } else {
        mesh->ebo = 0;
    }

    // Cleaning
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (mesh->ebo != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    return true;
}

R3DAPI bool R3D_UpdateMesh(R3D_Mesh* mesh)
{
    if (!mesh || mesh->vao == 0 || mesh->vbo == 0) {
        TraceLog(LOG_WARNING, "R3D: Cannot update mesh - mesh not uploaded yet");
        return false;
    }

    if (mesh->vertexCount <= 0 || !mesh->vertices) {
        TraceLog(LOG_WARNING, "R3D: Invalid vertex data in R3D_UpdateMesh");
        return false;
    }

    glBindVertexArray(mesh->vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);

    const size_t vertexSize = sizeof(R3D_Vertex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, mesh->vertexCount * vertexSize, mesh->vertices);

    // Updates indices if provided
    if (mesh->indexCount > 0 && mesh->indices) {
        if (mesh->ebo == 0) {
            // Generate an EBO if there was none
            glGenBuffers(1, &mesh->ebo);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indexCount * sizeof(unsigned int), mesh->indices, GL_DYNAMIC_DRAW);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (mesh->ebo != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    return true;
}

void R3D_UpdateMeshBoundingBox(R3D_Mesh* mesh)
{
    if (!mesh || mesh->vertices == NULL) {
        return;
    }

    Vector3 minVertex = mesh->vertices[0].position;
    Vector3 maxVertex = mesh->vertices[0].position;

    for (size_t i = 1; i < mesh->vertexCount; i++) {
        minVertex = Vector3Min(minVertex, mesh->vertices[i].position);
        maxVertex = Vector3Max(maxVertex, mesh->vertices[i].position);
    }

    mesh->aabb.min = minVertex;
    mesh->aabb.max = maxVertex;
}

/* === Public Material Functions === */

R3D_Material R3D_GetDefaultMaterial(void)
{
    R3D_Material material = { 0 };

    // Albedo map
    material.albedo.texture = R3D_GetWhiteTexture();
    material.albedo.color = WHITE;

    // Emission map
    material.emission.texture = R3D_GetWhiteTexture();
    material.emission.color = WHITE;
    material.emission.multiplier = 0.0f;

    // Normal map
    material.normal.texture = R3D_GetNormalTexture();

    // ORM map
    material.orm.texture = R3D_GetWhiteTexture();
    material.orm.occlusion = 1.0f;
    material.orm.roughness = 1.0f;
    material.orm.metalness = 0.0f;

    // Misc
    material.blendMode = R3D_BLEND_OPAQUE;
    material.cullMode = R3D_CULL_BACK;
    material.shadowCastMode = R3D_SHADOW_CAST_FRONT_FACES;
    material.billboardMode = R3D_BILLBOARD_DISABLED;
    material.alphaCutoff = 0.01f;

    return material;
}

void R3D_UnloadMaterial(const R3D_Material* material)
{
#define UNLOAD_TEXTURE_IF_VALID(id) \
    do { \
        if ((id) != 0 && !r3d_is_default_texture(id)) { \
            rlUnloadTexture(id); \
        } \
    } while (0)

    UNLOAD_TEXTURE_IF_VALID(material->albedo.texture.id);
    UNLOAD_TEXTURE_IF_VALID(material->emission.texture.id);
    UNLOAD_TEXTURE_IF_VALID(material->normal.texture.id);
    UNLOAD_TEXTURE_IF_VALID(material->orm.texture.id);

#undef UNLOAD_TEXTURE_IF_VALID
}

/* === Common Assimp Helper Functions === */

static inline Vector3 r3d_vec3_from_ai_vec3(const struct aiVector3D* aiVec)
{
    return (Vector3) { aiVec->x, aiVec->y, aiVec->z };
}

static inline Vector2 r3d_vec2_from_ai_vec3(const struct aiVector3D* aiVec)
{
    return (Vector2) { aiVec->x, aiVec->y };
}

static inline Vector2 r3d_vec2_from_ai_vec2(const struct aiVector2D* aiVec)
{
    return (Vector2) { aiVec->x, aiVec->y };
}

static inline Color r3d_color_from_ai_color(const struct aiColor4D* aiCol)
{
    return (Color) {
        (unsigned char)(Clamp(aiCol->r, 0.0f, 1.0f) * 255),
        (unsigned char)(Clamp(aiCol->g, 0.0f, 1.0f) * 255),
        (unsigned char)(Clamp(aiCol->b, 0.0f, 1.0f) * 255),
        (unsigned char)(Clamp(aiCol->a, 0.0f, 1.0f) * 255)
    };
}

static inline Matrix r3d_matrix_from_ai_matrix(const struct aiMatrix4x4* aiMat)
{
    return (Matrix) {
        aiMat->a1, aiMat->a2, aiMat->a3, aiMat->a4,
        aiMat->b1, aiMat->b2, aiMat->b3, aiMat->b4,
        aiMat->c1, aiMat->c2, aiMat->c3, aiMat->c4,
        aiMat->d1, aiMat->d2, aiMat->d3, aiMat->d4,
    };
}

/* === Assimp Mesh Processing === */

static bool r3d_process_assimp_mesh(R3D_Model* model, int meshIndex, const struct aiMesh* aiMesh, const struct aiScene* scene, bool upload)
{
    /* --- Cleanup macro in case we failed to process mesh --- */

    #define CLEANUP(mesh) do {                      \
        if ((mesh)->ebo != 0) {                     \
            glDeleteBuffers(1, &(mesh)->ebo);       \
        }                                           \
        if ((mesh)->vbo != 0) {                     \
            glDeleteBuffers(1, &(mesh)->vbo);       \
        }                                           \
        if ((mesh)->vao != 0) {                     \
            glDeleteVertexArrays(1, &(mesh)->vao);  \
        }                                           \
        free((mesh)->indices);                      \
        free((mesh)->vertices);                     \
        (mesh)->indices = NULL;                     \
        (mesh)->vertices = NULL;                    \
        (mesh)->vertexCount = 0;                    \
        (mesh)->indexCount = 0;                     \
    } while(0)

    /* --- Validate input parameters --- */

    if (!aiMesh || !model) {
        TraceLog(LOG_ERROR, "R3D: Invalid parameters for process_assimp_mesh");
        return false;
    }

    R3D_Mesh* mesh = &model->meshes[meshIndex];
    if (!mesh) {
        TraceLog(LOG_ERROR, "R3D: Invalid mesh for process_assimp_mesh");
        return false;
    }

    /* --- Validate mesh data presence --- */

    if (aiMesh->mNumVertices == 0 || aiMesh->mNumFaces == 0) {
        TraceLog(LOG_ERROR, "R3D: Empty mesh detected");
        return false;
    }

    /* --- Initialize mesh metadata --- */

    model->meshMaterials[meshIndex] = aiMesh->mMaterialIndex;
    mesh->vertexCount = aiMesh->mNumVertices;
    mesh->indexCount = 3 * aiMesh->mNumFaces;

    /* --- Allocate vertex and index buffers --- */

    mesh->vertices = calloc(mesh->vertexCount, sizeof(R3D_Vertex));
    if (!mesh->vertices) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for vertices");
        return false;
    }

    mesh->indices = calloc(mesh->indexCount, sizeof(unsigned int));
    if (!mesh->indices) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for indices");
        free(mesh->vertices);
        return false;
    }

    /* --- Initialize bounding box --- */

    Vector3 minBounds = {+FLT_MAX, +FLT_MAX, +FLT_MAX};
    Vector3 maxBounds = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    /* --- Process vertex attributes --- */

    for (size_t i = 0; i < mesh->vertexCount; i++) {
        R3D_Vertex* vertex = &mesh->vertices[i];

        // Position
        vertex->position = r3d_vec3_from_ai_vec3(&aiMesh->mVertices[i]);
        
        // Bounds update
        if (vertex->position.x < minBounds.x) minBounds.x = vertex->position.x;
        if (vertex->position.y < minBounds.y) minBounds.y = vertex->position.y;
        if (vertex->position.z < minBounds.z) minBounds.z = vertex->position.z;
        if (vertex->position.x > maxBounds.x) maxBounds.x = vertex->position.x;
        if (vertex->position.y > maxBounds.y) maxBounds.y = vertex->position.y;
        if (vertex->position.z > maxBounds.z) maxBounds.z = vertex->position.z;

        // Texture coordinates
        if (aiMesh->mTextureCoords[0] && aiMesh->mNumUVComponents[0] >= 2) {
            vertex->texcoord = r3d_vec2_from_ai_vec3(&aiMesh->mTextureCoords[0][i]);
        } else {
            vertex->texcoord = (Vector2) { 0.0f, 0.0f };
        }

        // Normals
        if (aiMesh->mNormals) {
            vertex->normal = r3d_vec3_from_ai_vec3(&aiMesh->mNormals[i]);
        } else {
            vertex->normal = (Vector3) { 0.0f, 0.0f, 1.0f };
        }

        // Tangent
        if (aiMesh->mNormals && aiMesh->mTangents && aiMesh->mBitangents) {
            vertex->tangent.x = aiMesh->mTangents[i].x;
            vertex->tangent.y = aiMesh->mTangents[i].y;
            vertex->tangent.z = aiMesh->mTangents[i].z;

            Vector3 normal = r3d_vec3_from_ai_vec3(&aiMesh->mNormals[i]);
            Vector3 tangent = r3d_vec3_from_ai_vec3(&aiMesh->mTangents[i]);
            Vector3 bitangent = r3d_vec3_from_ai_vec3(&aiMesh->mBitangents[i]);

            // Calculation of handedness
            Vector3 reconstructedBitangent = Vector3CrossProduct(normal, tangent);
            float handedness = Vector3DotProduct(reconstructedBitangent, bitangent);
            vertex->tangent.w = (handedness < 0.0f) ? -1.0f : 1.0f;
        } else {
            vertex->tangent = (Vector4) { 1.0f, 0.0f, 0.0f, 1.0f };
        }

        // Vertex color
        if (aiMesh->mColors[0]) {
            vertex->color.x = aiMesh->mColors[0][i].r;
            vertex->color.y = aiMesh->mColors[0][i].g;
            vertex->color.z = aiMesh->mColors[0][i].b;
            vertex->color.w = aiMesh->mColors[0][i].a;
        } else {
            vertex->color = (Vector4) { 1.0f, 1.0f, 1.0f, 1.0f };
        }
    }

    /* --- Process bone data --- */

    if (aiMesh->mNumBones > 0) {
        for (size_t boneIndex = 0; boneIndex < aiMesh->mNumBones; boneIndex++) {
            const struct aiBone* bone = aiMesh->mBones[boneIndex];

            if (!bone) {
                TraceLog(LOG_WARNING, "R3D: Null bone at index %zu", boneIndex);
                continue;
            }

            // Process all vertex weights for this bone
            for (size_t weightIndex = 0; weightIndex < bone->mNumWeights; weightIndex++) {
                const struct aiVertexWeight* weight = &bone->mWeights[weightIndex];

                unsigned int vertexId = weight->mVertexId;
                float weightValue = weight->mWeight;

                // Validate vertex ID
                if (vertexId >= mesh->vertexCount) {
                    TraceLog(LOG_ERROR, "R3D: Invalid vertex ID %u in bone weights (max: %zu)", 
                             vertexId, mesh->vertexCount);
                    continue;
                }

                // Skip weights that are too small to matter
                if (weightValue < 0.001f) {
                    continue;
                }

                // Find an empty slot in the vertex bone data (max 4 bones per vertex)
                R3D_Vertex* vertex = &mesh->vertices[vertexId];
                bool slotFound = false;

                for (int slot = 0; slot < 4; slot++) {
                    if (vertex->weights[slot] == 0.0f) {
                        vertex->weights[slot] = weightValue;
                        vertex->boneIds[slot] = (int)boneIndex;
                        slotFound = true;
                        break;
                    }
                }

                if (!slotFound) {
                    // If all 4 slots are occupied, replace the smallest weight
                    int minWeightIndex = 0;
                    for (int slot = 1; slot < 4; slot++) {
                        if (vertex->weights[slot] < vertex->weights[minWeightIndex]) {
                            minWeightIndex = slot;
                        }
                    }

                    if (weightValue > vertex->weights[minWeightIndex]) {
                        vertex->weights[minWeightIndex] = weightValue;
                        vertex->boneIds[minWeightIndex] = (int)boneIndex;
                    }
                }
            }
        }

        // Normalize bone weights for each vertex
        for (size_t i = 0; i < mesh->vertexCount; i++) {
            R3D_Vertex* boneVertex = &mesh->vertices[i];
            float totalWeight = 0.0f;

            // Calculate total weight
            for (int j = 0; j < 4; j++) {
                totalWeight += boneVertex->weights[j];
            }

            // Normalize weights if total > 0
            if (totalWeight > 0.0f) {
                for (int j = 0; j < 4; j++) {
                    boneVertex->weights[j] /= totalWeight;
                }
            } else {
                // If no bone weights, assign to first bone with weight 1.0
                boneVertex->weights[0] = 1.0f;
                boneVertex->boneIds[0] = 0;
            }
        }
    } else {
        TraceLog(LOG_INFO, "R3D: No bones found for mesh %d", meshIndex);
        for (size_t i = 0; i < mesh->vertexCount; i++) {
            mesh->vertices[i].weights[0] = 1.0f;
            mesh->vertices[i].boneIds[0] = 0;
        }
    }

    /* --- Set bounding box --- */

    mesh->aabb.min = minBounds;
    mesh->aabb.max = maxBounds;

    /* --- Process indices and validate faces --- */

    size_t indexOffset = 0;
    for (size_t i = 0; i < aiMesh->mNumFaces; i++) {
        const struct aiFace* face = &aiMesh->mFaces[i];

        if (face->mNumIndices != 3) {
            TraceLog(LOG_ERROR, "R3D: Non-triangular face detected (indices: %u)", face->mNumIndices);
            CLEANUP(mesh);
            return false;
        }

        for (unsigned int j = 0; j < 3; j++) {
            if (face->mIndices[j] >= aiMesh->mNumVertices) {
                TraceLog(LOG_ERROR, "R3D: Invalid vertex index (%u >= %u)", face->mIndices[j], aiMesh->mNumVertices);
                CLEANUP(mesh);
                return false;
            }
        }

        mesh->indices[indexOffset++] = face->mIndices[0];
        mesh->indices[indexOffset++] = face->mIndices[1];
        mesh->indices[indexOffset++] = face->mIndices[2];
    }

    /* --- Final validation: index count consistency --- */

    if (indexOffset != mesh->indexCount) {
        TraceLog(LOG_ERROR, "R3D: Inconsistency in the number of indices (%zu != %zu)", indexOffset, mesh->indexCount);
        CLEANUP(mesh);
        return false;
    }

    /* --- Upload optionally mesh to GPU --- */

    if (upload) {
        if (!R3D_UploadMesh(mesh, false)) {
            CLEANUP(mesh);
            return false;
        }
    }

    return true;

#undef CLEANUP
}

/* === Assimp Material Processing === */

static Image r3d_load_assimp_image(
    const struct aiScene* scene, const struct aiMaterial* aiMat,
    enum aiTextureType textureType, unsigned int index,
    const char* basePath, bool* isAllocated)
{
    assert(isAllocated != NULL);

    Image image = { 0 };
    struct aiString texPath;

    *isAllocated = false;

    /* --- Try to retrieve the texture path --- */

    if (aiGetMaterialTexture(aiMat, textureType, index, &texPath, NULL, NULL, NULL, NULL, NULL, NULL) != AI_SUCCESS) {
        return image; // No texture of this type
    }

    /* --- Handle embedded texture (starts with '*') --- */

    if (texPath.data[0] == '*')
    {
        int textureIndex = atoi(&texPath.data[1]);

        /* --- Validate embedded texture index --- */

        if (textureIndex < 0 || textureIndex >= (int)scene->mNumTextures) {
            return image;
        }

        const struct aiTexture* aiTex = scene->mTextures[textureIndex];

        /* --- Handle compressed embedded texture --- */

        if (aiTex->mHeight == 0) {
            image = LoadImageFromMemory(
                TextFormat(".%s", aiTex->achFormatHint), (const unsigned char*)aiTex->pcData, aiTex->mWidth
            );
            *isAllocated = true;
        }

        /* --- Handle uncompressed (raw RGBA) embedded texture --- */

        else {
            image.width = aiTex->mWidth;
            image.height = aiTex->mHeight;
            image.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            // NOTE: No need to copy the data here, the image will be immediately
            //       uploaded to the GPU without being retained afterward
            image.data = (unsigned char*)aiTex->pcData;
        }
    }

    /* --- Handle external texture from file --- */

    else {
        if (basePath == NULL) {
            TraceLog(LOG_ERROR,
                "R3D: You are trying to load a model from memory that includes external textures;"
                "The model will be invalid"
            );
            return image;
        }
        image = LoadImage(TextFormat("%s%s", basePath, texPath.data));
        *isAllocated = true;
    }

    return image;
}

static Texture2D r3d_load_assimp_texture(
    const struct aiScene* scene, const struct aiMaterial* aiMat,
    enum aiTextureType textureType, unsigned int index,
    const char* basePath)
{
    Texture2D texture = { 0 };

    bool imgIsAllocted = false;
    Image image = r3d_load_assimp_image(scene, aiMat, textureType, index, basePath, &imgIsAllocted);

    if (image.data == NULL) {
        return texture;
    }

    texture = LoadTextureFromImage(image);
    if (imgIsAllocted) UnloadImage(image);

    if (R3D.state.loading.textureFilter > TEXTURE_FILTER_BILINEAR) {
        GenTextureMipmaps(&texture);
    }

    SetTextureFilter(texture, R3D.state.loading.textureFilter);

    return texture;
}

/* --- Main ORM texture loading function --- */

static Texture2D r3d_load_assimp_orm_texture(
    const struct aiScene* scene, const struct aiMaterial* aiMat, const char* basePath,
    bool* hasOcclusion, bool* hasRoughness, bool* hasMetalness)
{
#define PATHS_EQUAL(a, b) (strcmp((a).data, (b).data) == 0)
#define HAS_TEXTURE_DATA(comp) ((comp).image.data != NULL)
#define IS_SHININESS_TYPE(comp) ((comp).type == aiTextureType_SHININESS)

    Texture2D ormTexture = { 0 };

    /* --- Init output values --- */

    *hasOcclusion = false;
    *hasRoughness = false;
    *hasMetalness = false;

    /* --- Texture component structure --- */
    
    typedef struct {
        Image image;
        bool isAllocated;
        enum aiTextureType type;
        struct aiString texPath;
        bool hasTexture;
    } TextureComponent;

    TextureComponent components[3] = {
        { {0}, false, aiTextureType_AMBIENT_OCCLUSION, {0}, false },
        { {0}, false, aiTextureType_DIFFUSE_ROUGHNESS, {0}, false },
        { {0}, false, aiTextureType_METALNESS, {0}, false }
    };

    /* --- Initialize texture availability --- */
    
    *hasOcclusion = components[0].hasTexture = (aiGetMaterialTexture(aiMat, components[0].type, 0, &components[0].texPath, NULL, NULL, NULL, NULL, NULL, NULL) == AI_SUCCESS);
    *hasRoughness = components[1].hasTexture = (aiGetMaterialTexture(aiMat, components[1].type, 0, &components[1].texPath, NULL, NULL, NULL, NULL, NULL, NULL) == AI_SUCCESS);
    
    // Fallback to shininess for roughness if not available
    if (!components[1].hasTexture) {
        *hasRoughness = components[1].hasTexture = (aiGetMaterialTexture(aiMat, aiTextureType_SHININESS, 0, &components[1].texPath, NULL, NULL, NULL, NULL, NULL, NULL) == AI_SUCCESS);
        if (components[1].hasTexture) components[1].type = aiTextureType_SHININESS;
    }
    
    *hasMetalness = components[2].hasTexture = (aiGetMaterialTexture(aiMat, components[2].type, 0, &components[2].texPath, NULL, NULL, NULL, NULL, NULL, NULL) == AI_SUCCESS);

    /* --- Analyze texture sharing patterns --- */
    
    bool allTexturesSame = false, twoTexturesSame = false;
    int sharedTextureIndex = -1;

    if (components[0].hasTexture && components[1].hasTexture && components[2].hasTexture) {
        if (PATHS_EQUAL(components[0].texPath, components[1].texPath) && 
            PATHS_EQUAL(components[1].texPath, components[2].texPath)) {
            allTexturesSame = true;
            sharedTextureIndex = 0;
        }
        else if (PATHS_EQUAL(components[0].texPath, components[1].texPath) || 
                 PATHS_EQUAL(components[0].texPath, components[2].texPath)) {
            twoTexturesSame = true;
            sharedTextureIndex = 0;
        }
        else if (PATHS_EQUAL(components[1].texPath, components[2].texPath)) {
            twoTexturesSame = true;
            sharedTextureIndex = 1;
        }
    }

    /* --- Handle case where all textures are identical --- */
    
    if (allTexturesSame)
    {
        components[0].image = r3d_load_assimp_image(scene, aiMat, components[0].type, 0, basePath, &components[0].isAllocated);

        if (HAS_TEXTURE_DATA(components[0]))
        {
            // Convert to RGB format if necessary
            if (components[0].image.format != RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8) {
                ImageFormat(&components[0].image, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            }

            // In such cases, there should be no shininess as roughness.
            //if (IS_SHININESS_TYPE(components[1])) {
            //    ImageColorInvertGreen(&components[0].image);
            //}

            ormTexture = LoadTextureFromImage(components[0].image);

            // Apply texture filtering
            if (R3D.state.loading.textureFilter > TEXTURE_FILTER_BILINEAR) {
                GenTextureMipmaps(&ormTexture);
            }
            SetTextureFilter(ormTexture, R3D.state.loading.textureFilter);

            // Cleanup and return
            if (components[0].isAllocated) {
                UnloadImage(components[0].image);
            }
            return ormTexture;
        }
    }

    /* --- Load individual texture components --- */
    
    // Load occlusion texture
    if (components[0].hasTexture) {
        components[0].image = r3d_load_assimp_image(scene, aiMat, components[0].type, 0, basePath, &components[0].isAllocated);
    }
    
    // Load roughness texture with sharing
    if (components[1].hasTexture) {
        if (twoTexturesSame && sharedTextureIndex == 0 && PATHS_EQUAL(components[0].texPath, components[1].texPath)) {
            components[1].image = components[0].image;
            components[1].isAllocated = false;
        } else {
            components[1].image = r3d_load_assimp_image(scene, aiMat, components[1].type, 0, basePath, &components[1].isAllocated);
            if (HAS_TEXTURE_DATA(components[1]) && IS_SHININESS_TYPE(components[1])) {
                ImageColorInvert(&components[1].image);
            }
        }
    }
    
    // Load metalness texture with sharing
    if (components[2].hasTexture) {
        bool shouldLoadNew = true;
        
        if (twoTexturesSame) {
            if (sharedTextureIndex == 0 && PATHS_EQUAL(components[0].texPath, components[2].texPath)) {
                components[2].image = components[0].image;
                components[2].isAllocated = false;
                shouldLoadNew = false;
            } else if (sharedTextureIndex == 1 && PATHS_EQUAL(components[1].texPath, components[2].texPath)) {
                components[2].image = components[1].image;
                components[2].isAllocated = false;
                shouldLoadNew = false;
            }
        }
        
        if (shouldLoadNew) {
            components[2].image = r3d_load_assimp_image(scene, aiMat, components[2].type, 0, basePath, &components[2].isAllocated);
        }
    }

    /* --- Validate at least one component is available --- */
    
    bool hasAnyTexture = HAS_TEXTURE_DATA(components[0]) || HAS_TEXTURE_DATA(components[1]) || HAS_TEXTURE_DATA(components[2]);
    if (!hasAnyTexture) goto cleanup;

    /* --- Determine reference dimensions --- */
    
    int refWidth = 0, refHeight = 0;
    for (int i = 0; i < 3; i++) {
        if (HAS_TEXTURE_DATA(components[i])) {
            refWidth = components[i].image.width;
            refHeight = components[i].image.height;
            break;
        }
    }

    /* --- Resize components to match reference dimensions --- */
    
    for (int i = 0; i < 3; i++) {
        if (HAS_TEXTURE_DATA(components[i]) && components[i].isAllocated &&
            (components[i].image.width != refWidth || components[i].image.height != refHeight)) {
            ImageResize(&components[i].image, refWidth, refHeight);
        }
    }

    /* --- Create combined ORM texture --- */
    
    Image ormImage = {
        .data = malloc(refWidth * refHeight * 3 * sizeof(uint8_t)),
        .format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8,
        .width = refWidth,
        .height = refHeight,
        .mipmaps = 1
    };

    if (!ormImage.data) goto cleanup;

    /* --- Pack ORM channels into final image --- */
    
    uint8_t* ormData = (uint8_t*)ormImage.data;
    const size_t pixelCount = refWidth * refHeight;
    
    for (size_t i = 0; i < pixelCount; i++)
    {
        const int x = i % refWidth;
        const int y = i / refWidth;

        // Extract channels with default values
        const uint8_t O = HAS_TEXTURE_DATA(components[0]) ? GetImageColor(components[0].image, x, y).r : 255;
        const uint8_t R = HAS_TEXTURE_DATA(components[1]) ? GetImageColor(components[1].image, x, y).g : 255;
        const uint8_t M = HAS_TEXTURE_DATA(components[2]) ? GetImageColor(components[2].image, x, y).b : 255;

        // Pack into RGB: Red=Occlusion, Green=Roughness, Blue=Metalness
        ormData[i * 3 + 0] = O;
        ormData[i * 3 + 1] = R;
        ormData[i * 3 + 2] = M;
    }

    /* --- Generate final texture with filtering --- */
    
    ormTexture = LoadTextureFromImage(ormImage);
    UnloadImage(ormImage);

    if (R3D.state.loading.textureFilter > TEXTURE_FILTER_BILINEAR) {
        GenTextureMipmaps(&ormTexture);
    }
    SetTextureFilter(ormTexture, R3D.state.loading.textureFilter);

cleanup:
    /* --- Cleanup allocated texture components --- */

    for (int i = 0; i < 3; i++) {
        if (components[i].isAllocated && HAS_TEXTURE_DATA(components[i])) {
            UnloadImage(components[i].image);
        }
    }

    return ormTexture;

#undef PATHS_EQUAL
#undef HAS_TEXTURE_DATA
#undef IS_SHININESS_TYPE
}

bool process_assimp_materials(const struct aiScene* scene, R3D_Material** materials, int* materialCount, const char* modelPath)
{
    /* --- Allocate material array --- */

    *materialCount = scene->mNumMaterials;
    *materials = malloc(*materialCount * sizeof(R3D_Material));
    if (!*materials) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for materials");
        return false;
    }

    const char* basePath = NULL;

    if (modelPath != NULL) {
        basePath = GetDirectoryPath(modelPath);
    }

    /* --- Process each material --- */

    for (size_t i = 0; i < *materialCount; i++) {
        const struct aiMaterial* aiMat = scene->mMaterials[i];
        R3D_Material* mat = &(*materials)[i];

        /* --- Initialize material defaults --- */

        *mat = R3D_GetDefaultMaterial();

        /* --- Calculate the albedo --- */

        struct aiColor4D diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        struct aiColor4D baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float transparency = 0.0f;
        float opacity = 1.0f;

        (void)aiGetMaterialColor(aiMat, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor);
        (void)aiGetMaterialColor(aiMat, AI_MATKEY_BASE_COLOR, &baseColor);
        (void)aiGetMaterialFloat(aiMat, AI_MATKEY_TRANSPARENCYFACTOR, &transparency);
        (void)aiGetMaterialFloat(aiMat, AI_MATKEY_OPACITY, &opacity);

        mat->albedo.color.r = 255 * (diffuseColor.r * baseColor.r);
        mat->albedo.color.g = 255 * (diffuseColor.g * baseColor.g);
        mat->albedo.color.b = 255 * (diffuseColor.b * baseColor.b);
        mat->albedo.color.a = 255 * (diffuseColor.a * baseColor.a);
        mat->albedo.color.a *= opacity * (1.0f - transparency);

        /* --- Load albedo texture --- */

        mat->albedo.texture = r3d_load_assimp_texture(scene, aiMat, aiTextureType_DIFFUSE, 0, basePath);

        if (mat->albedo.texture.id == 0) {
            mat->albedo.texture = r3d_load_assimp_texture(scene, aiMat, aiTextureType_BASE_COLOR, 0, basePath);
        }

        if (mat->albedo.texture.id == 0) {
            mat->albedo.texture = R3D_GetWhiteTexture();
        }

        /* --- Load normal map --- */

        mat->normal.texture = r3d_load_assimp_texture(scene, aiMat, aiTextureType_NORMALS, 0, basePath);

        if (mat->normal.texture.id == 0) {
            mat->normal.texture = R3D_GetNormalTexture();
        }

        //mat->normal.scale = 1.0f;

        /* --- Load emission map --- */

        mat->emission.texture = r3d_load_assimp_texture(scene, aiMat, aiTextureType_EMISSIVE, 0, basePath);
        if (mat->emission.texture.id == 0) {
            mat->emission.texture = R3D_GetWhiteTexture();
        }
        else {
            mat->emission.multiplier = 1.0f;
        }
        
        /* --- Load ORM map --- */

        bool hasOcclusion = false;
        bool hasRoughness = false;
        bool hasMetalness = false;

        mat->orm.texture = r3d_load_assimp_orm_texture(
            scene, aiMat, basePath,
            &hasOcclusion,
            &hasRoughness,
            &hasMetalness
        );

        if (mat->orm.texture.id == 0) {
            mat->orm.texture = R3D_GetWhiteTexture();
        }

        if (aiGetMaterialFloat(aiMat, AI_MATKEY_ROUGHNESS_FACTOR, &mat->orm.roughness) != AI_SUCCESS) {
            mat->orm.roughness = 1.0f;
        }

        if (aiGetMaterialFloat(aiMat, AI_MATKEY_METALLIC_FACTOR, &mat->orm.metalness) != AI_SUCCESS) {
            mat->orm.metalness = hasMetalness ? 1.0f : 0.0f;
        }

        /* --- Handle cull mode from two-sided property --- */

        int twoSided = 0;
        if (aiGetMaterialInteger(aiMat, AI_MATKEY_TWOSIDED, &twoSided) == AI_SUCCESS) {
            if (twoSided) mat->cullMode = R3D_CULL_NONE;
        }

        /* --- Handle the alpha mode for glTF models --- */

        bool glTFTransparency = false;

        struct aiString alphaMode;
        if (aiGetMaterialString(aiMat, "$mat.gltf.alphaMode", 0, 0, &alphaMode) == AI_SUCCESS) {
            // NOTE: Do not pay attention to 'BLEND' as it can sometimes be misleading...
            if (strcmp(alphaMode.data, "MASK") == 0) {
                float alphaCutoff = mat->alphaCutoff;
                if (aiGetMaterialFloat(aiMat, "$mat.gltf.alphaCutoff", 0, 0, &alphaCutoff) == AI_SUCCESS) {
                    mat->alphaCutoff = alphaCutoff;
                }
                glTFTransparency = true;
            }
        }

        /* --- Adjust blend mode based on alpha --- */

        // Check also the transmission factor
        // Indicates light passes through material (glass, transparent plastics)
        float transmission = 0.0f;
        (void)aiGetMaterialFloat(aiMat, AI_MATKEY_TRANSMISSION_FACTOR, &transmission);

        if (glTFTransparency || mat->albedo.color.a < 1.0f || transmission > 0.01f) {
            mat->blendMode = R3D_BLEND_ALPHA;
            mat->cullMode = R3D_CULL_NONE;
        }

        /* --- Handle blend function override --- */

        int blendFunc = aiBlendMode_Default;
        if (aiGetMaterialInteger(aiMat, AI_MATKEY_BLEND_FUNC, &blendFunc) == AI_SUCCESS) {
            switch (blendFunc) {
            case aiBlendMode_Additive:
                mat->blendMode = R3D_BLEND_ADDITIVE;
                mat->cullMode = R3D_CULL_NONE;
                break;
            case aiBlendMode_Default:
            default:
                mat->blendMode = R3D_BLEND_ALPHA;
                mat->cullMode = R3D_CULL_NONE;
                break;
            }
        }
    }

    return true;
}

/* === Assimp Bones / Bind Poses Processing === */

// Finds a bone's index by name. Returns -1 if not found.
static int find_bone_index(const char* name, BoneInfo* bones, int count)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(name, bones[i].name) == 0) return i;
    }
    return -1;
}

// Recursively builds the bone hierarchy using the final bone array.
static void build_hierarchy_recursive(const struct aiNode* node, BoneInfo* bones, int boneCount, int parentIndex)
{
    int currentIndex = find_bone_index(node->mName.data, bones, boneCount);

    if (currentIndex != -1) {
        bones[currentIndex].parent = parentIndex;
        parentIndex = currentIndex; // This node becomes the parent for its children
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        build_hierarchy_recursive(node->mChildren[i], bones, boneCount, parentIndex);
    }
}

// Processes bones and offsets for an R3D_Model.
bool r3d_process_bones_and_offsets(R3D_Model* model, const struct aiScene* scene)
{
    if (!model || !scene || scene->mNumMeshes <= 0) {
        return false;
    }

    // Determine maximum possible bones to pre-allocate, avoiding temporary heap buffers.
    int maxPossibleBones = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        maxPossibleBones += scene->mMeshes[m]->mNumBones;
    }

    // Early exit if no bones are found across all meshes.
    if (maxPossibleBones == 0) {
        model->boneCount = 0;
        model->bones = NULL;
        model->boneOffsets = NULL;
        return true;
    }

    // Allocate final bone and offset arrays directly in the model.
    model->bones = (BoneInfo*)calloc(maxPossibleBones, sizeof(BoneInfo));
    model->boneOffsets = (Matrix*)calloc(maxPossibleBones, sizeof(Matrix));
    if (!model->bones || !model->boneOffsets) {
        free(model->bones);
        free(model->boneOffsets);
        model->bones = NULL;
        model->boneOffsets = NULL;
        model->boneCount = 0;
        return false;
    }

    // Collect unique bones and their offset matrices directly into the model's arrays.
    int uniqueBoneCount = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        const struct aiMesh* mesh = scene->mMeshes[m];
        for (unsigned int b = 0; b < mesh->mNumBones; b++) {
            const struct aiBone* bone = mesh->mBones[b];

            // Add bone if it's unique.
            if (find_bone_index(bone->mName.data, model->bones, uniqueBoneCount) == -1) {
                strncpy(model->bones[uniqueBoneCount].name, bone->mName.data, 31);
                model->bones[uniqueBoneCount].name[31] = '\0';
                model->bones[uniqueBoneCount].parent = -1; // Initialize parent

                model->boneOffsets[uniqueBoneCount] = r3d_matrix_from_ai_matrix(&bone->mOffsetMatrix);

                uniqueBoneCount++;
            }
        }
    }
    model->boneCount = uniqueBoneCount; // Update the actual number of unique bones.

    // Build the bone hierarchy by traversing the Assimp scene graph.
    build_hierarchy_recursive(scene->mRootNode, model->bones, model->boneCount, -1);

    // Allocate a boneMatrices cache array for all meshes
    // Which will be used to calculate only one of the matrices and be reused between passes
    for (int i = 0; i < model->meshCount; i++) {
        model->meshes[i].boneMatrices = malloc(R3D_SHADER_MAX_BONES * sizeof(Matrix));
    }
    
    return true;
}

/* === Assimp Animation Processing === */

Vector3 r3d_interpolate_animation_keys_vec3(const struct aiVectorKey* keys, unsigned int numKeys, float time)
{
    /* --- Case where there is only one key --- */

    if (numKeys == 1) {
        return (Vector3){
            keys[0].mValue.x,
            keys[0].mValue.y,
            keys[0].mValue.z
        };
    }
    
    /* --- Find surrounding keys --- */

    unsigned int index = 0;
    for (unsigned int i = 0; i < numKeys - 1; i++) {
        if (time < keys[i + 1].mTime) {
            index = i;
            break;
        }
    }
    
    /* --- Clamp to last key --- */

    if (index >= numKeys - 1) {
        return (Vector3){
            keys[numKeys - 1].mValue.x,
            keys[numKeys - 1].mValue.y,
            keys[numKeys - 1].mValue.z
        };
    }
    
    /* --- Linear interpolation between the two surrounding keyframes --- */

    float deltaTime = keys[index + 1].mTime - keys[index].mTime;
    float factor = (time - keys[index].mTime) / deltaTime;
    
    Vector3 pos1 = {keys[index].mValue.x, keys[index].mValue.y, keys[index].mValue.z};
    Vector3 pos2 = {keys[index + 1].mValue.x, keys[index + 1].mValue.y, keys[index + 1].mValue.z};
    
    return (Vector3){
        pos1.x + factor * (pos2.x - pos1.x),
        pos1.y + factor * (pos2.y - pos1.y),
        pos1.z + factor * (pos2.z - pos1.z)
    };
}

Quaternion r3d_interpolate_animation_keys_quat(const struct aiQuatKey* keys, unsigned int numKeys, float time)
{
    /* --- Case where there is only one key --- */

    if (numKeys == 1) {
        return (Quaternion){
            keys[0].mValue.x, keys[0].mValue.y,
            keys[0].mValue.z, keys[0].mValue.w
        };
    }
    
    /* --- Find surrounding keys --- */

    unsigned int index = 0;
    for (unsigned int i = 0; i < numKeys - 1; i++) {
        if (time < keys[i + 1].mTime) {
            index = i;
            break;
        }
    }
    
    /* --- Clamp to last key --- */

    if (index >= numKeys - 1) {
        return (Quaternion){
            keys[numKeys - 1].mValue.x, keys[numKeys - 1].mValue.y, 
            keys[numKeys - 1].mValue.z, keys[numKeys - 1].mValue.w
        };
    }
    
    /* --- Spherical interpolation (SLERP) between the two surrounding keyframes --- */

    float deltaTime = keys[index + 1].mTime - keys[index].mTime;
    float factor = (time - keys[index].mTime) / deltaTime;
    
    Quaternion q1 = {
        keys[index].mValue.x,
        keys[index].mValue.y,
        keys[index].mValue.z,
        keys[index].mValue.w
    };
    
    Quaternion q2 = {
        keys[index + 1].mValue.x,
        keys[index + 1].mValue.y,
        keys[index + 1].mValue.z,
        keys[index + 1].mValue.w
    };
    
    return QuaternionSlerp(q1, q2, factor);
}

Matrix r3d_get_node_transform_at_time(const struct aiAnimation* aiAnim, const char* nodeName, float time)
{
    /* --- Search for the animation channel corresponding to the given node --- */

    for (unsigned int i = 0; i < aiAnim->mNumChannels; i++) {
        const struct aiNodeAnim* nodeAnim = aiAnim->mChannels[i];
        if (strcmp(nodeAnim->mNodeName.data, nodeName) == 0) {

            /* --- Interpolate position, rotation, and scale at the specified time --- */

            Vector3 position = r3d_interpolate_animation_keys_vec3(
                nodeAnim->mPositionKeys,
                nodeAnim->mNumPositionKeys,
                time
            );

            Quaternion rotation = r3d_interpolate_animation_keys_quat(
                nodeAnim->mRotationKeys,
                nodeAnim->mNumRotationKeys,
                time
            );

            Vector3 scale = r3d_interpolate_animation_keys_vec3(
                nodeAnim->mScalingKeys,
                nodeAnim->mNumScalingKeys,
                time
            );

            /* --- Combine transformations: Scale * Rotation * Translation --- */

            return MatrixMultiply(
                MatrixMultiply(
                    MatrixScale(scale.x, scale.y, scale.z),
                    QuaternionToMatrix(rotation)
                ),
                MatrixTranslate(position.x, position.y, position.z)
            );
        }
    }

    /* --- No animation channel found for the node: return identity matrix --- */

    return MatrixIdentity();
}

void r3d_calculate_animation_global_transforms(
    const struct aiNode* node, const struct aiAnimation* aiAnim, float time,
    Matrix parentTransform, Matrix* globalTransforms,
    const BoneInfo* bones, int totalBones)
{
    /* --- Get the node's local transform at the specified time --- */

    Matrix localTransform = r3d_get_node_transform_at_time(aiAnim, node->mName.data, time);

    /* --- Fallback: use default transformation if no animation is found for this node --- */

    if (r3d_simd_is_matrix_identity((float*)&localTransform)) {
        localTransform = r3d_matrix_from_ai_matrix(&node->mTransformation);
    }

    /* --- Compute the global transformation by combining with the parent's transform --- */

    Matrix globalTransform = MatrixMultiply(localTransform, parentTransform);

    /* --- Check if this node corresponds to a bone and store its global transform --- */

    for (int i = 0; i < totalBones; i++) {
        if (strcmp(node->mName.data, bones[i].name) == 0) {
            globalTransforms[i] = globalTransform;
            break;
        }
    }

    /* --- Recursively compute global transforms for all child nodes --- */

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        r3d_calculate_animation_global_transforms(
            node->mChildren[i], aiAnim, time, 
            globalTransform, globalTransforms, 
            bones, totalBones);
    }
}

bool r3d_process_animation(R3D_ModelAnimation* animation, const struct aiScene* scene, const struct aiAnimation* aiAnim, int targetFrameRate)
{
    /* --- Validate input --- */

    if (!animation || !scene || !aiAnim) {
        return false;
    }

    /* --- Initialize animation name --- */

    strncpy(animation->name, aiAnim->mName.data, 31);
    animation->name[31] = '\0';

    /* --- Compute duration in frames based on ticks per second --- */

    float ticksPerSecond = aiAnim->mTicksPerSecond ? aiAnim->mTicksPerSecond : 25.0f;
    float durationInSeconds = (float)aiAnim->mDuration / ticksPerSecond;
    animation->frameCount = (int)(durationInSeconds * targetFrameRate + 0.5f);

    TraceLog(LOG_INFO, "R3D: Animation '%s' - Duration: %.2fs, Frames: %d", 
             animation->name, durationInSeconds, animation->frameCount);

    /* --- Count unique bones used across all meshes --- */

    int boneCounter = 0;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++) {
        const struct aiMesh* mesh = scene->mMeshes[meshIndex];

        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; boneIndex++) {
            const struct aiBone* bone = mesh->mBones[boneIndex];
            bool boneExists = false;

            // Check in previous meshes
            for (unsigned int prevMeshIndex = 0; prevMeshIndex < meshIndex && !boneExists; prevMeshIndex++) {
                const struct aiMesh* prevMesh = scene->mMeshes[prevMeshIndex];
                for (unsigned int prevBoneIndex = 0; prevBoneIndex < prevMesh->mNumBones && !boneExists; prevBoneIndex++) {
                    boneExists = (strcmp(bone->mName.data, prevMesh->mBones[prevBoneIndex]->mName.data) == 0);
                }
            }

            // Check in previous bones of the same mesh
            if (!boneExists) {
                for (unsigned int prevBoneIndex = 0; prevBoneIndex < boneIndex && !boneExists; prevBoneIndex++) {
                    boneExists = (strcmp(bone->mName.data, mesh->mBones[prevBoneIndex]->mName.data) == 0);
                }
            }

            if (!boneExists) boneCounter++;
        }
    }

    /* --- Abort if no bones found --- */

    if (boneCounter == 0) {
        TraceLog(LOG_WARNING, "R3D: No bones found for animation '%s'", animation->name);
        return false;
    }

    animation->boneCount = boneCounter;

    /* --- Allocate memory for bones and frame poses --- */

    animation->bones = calloc(animation->boneCount, sizeof(BoneInfo));
    animation->framePoses = calloc(animation->frameCount, sizeof(Matrix*));

    if (!animation->bones || !animation->framePoses) {
        TraceLog(LOG_ERROR, "R3D: Failed to allocate memory for animation data");
        free(animation->framePoses);
        free(animation->bones);
        return false;
    }

    /* --- Collect unique bone names --- */

    boneCounter = 0;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++) {
        const struct aiMesh* mesh = scene->mMeshes[meshIndex];

        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; boneIndex++) {
            const struct aiBone* bone = mesh->mBones[boneIndex];
            bool boneExists = false;

            for (int i = 0; i < boneCounter && !boneExists; i++) {
                boneExists = (strcmp(bone->mName.data, animation->bones[i].name) == 0);
            }

            if (!boneExists) {
                strncpy(animation->bones[boneCounter].name, bone->mName.data, 31);
                animation->bones[boneCounter].name[31] = '\0';
                animation->bones[boneCounter].parent = -1;
                boneCounter++;
            }
        }
    }

    /* --- Allocate matrices for each animation frame --- */

    for (int frame = 0; frame < animation->frameCount; frame++) {
        animation->framePoses[frame] = calloc(animation->boneCount, sizeof(Matrix));
        if (!animation->framePoses[frame]) {
            TraceLog(LOG_ERROR, "R3D: Failed to allocate memory for frame %d", frame);
            for (int i = 0; i < frame; i++) free(animation->framePoses[i]);
            free(animation->framePoses);
            free(animation->bones);
            return false;
        }
    }

    /* --- Compute global bone transforms for each frame --- */

    for (int frame = 0; frame < animation->frameCount; frame++) {
        float timeInTicks = fminf(
            ((float)frame / targetFrameRate) * ticksPerSecond,
            (float)aiAnim->mDuration);

        // Initialize all bones to identity before calculating
        for (int i = 0; i < animation->boneCount; i++) {
            animation->framePoses[frame][i] = MatrixIdentity();
        }

        r3d_calculate_animation_global_transforms(
            scene->mRootNode, aiAnim, timeInTicks,
            MatrixIdentity(), animation->framePoses[frame],
            animation->bones, animation->boneCount
        );
    }

    /* --- Final success log --- */

    TraceLog(LOG_INFO, "R3D: Successfully processed animation '%s' with %d bones and %d frames", 
             animation->name, animation->boneCount, animation->frameCount);

    return true;
}

/* === Public Model Functions === */

#define R3D_ASSIMP_FLAGS                \
    aiProcess_Triangulate           |   \
    aiProcess_FlipUVs               |   \
    aiProcess_CalcTangentSpace      |   \
    aiProcess_GenNormals            |   \
    aiProcess_JoinIdenticalVertices |   \
    aiProcess_SortByPType           |   \
    aiProcess_GlobalScale

R3D_Model R3D_LoadModel(const char* filePath)
{
    R3D_Model model = { 0 };

    /* --- Configure Assimp properties --- */

    // Set the global scale from 'cm' to 'm'
    struct aiPropertyStore* props = aiCreatePropertyStore();
    aiSetImportPropertyFloat(props, AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, 0.01f);

    /* --- Import scene using Assimp --- */

    const struct aiScene* scene = aiImportFileExWithProperties(filePath, R3D_ASSIMP_FLAGS, NULL, props);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        TraceLog(LOG_ERROR, "R3D: Assimp error; %s", aiGetErrorString());
        return model;
    }

    aiReleasePropertyStore(props);

    /* --- Process materials --- */

    if (!process_assimp_materials(scene, &model.materials, &model.materialCount, filePath)) {
        TraceLog(LOG_ERROR, "R3D: Unable to load materials; The model will be invalid");
        return model;
    }

    /* --- Initialize model and allocate meshes --- */

    model.meshCount = scene->mNumMeshes;

    model.meshes = calloc(model.meshCount, sizeof(R3D_Mesh));
    if (model.meshes == NULL) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for meshes; The model will be invalid");
        R3D_UnloadModel(&model, true);
        aiReleaseImport(scene);
        return model;
    }

    model.meshMaterials = calloc(model.meshCount, sizeof(int));
    if (model.meshMaterials == NULL) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for mesh materials array; The model will be invalid");
        R3D_UnloadModel(&model, true);
        aiReleaseImport(scene);
        free(model.meshes);
        return model;
    }

    /* --- Process all meshes --- */

    for (uint32_t i = 0; i < model.meshCount; i++) {
        if (!r3d_process_assimp_mesh(&model, i, scene->mMeshes[i], scene, true)) {
            TraceLog(LOG_ERROR, "R3D: Unable to load mesh [%d]; The model will be invalid", i);
            R3D_UnloadModel(&model, true);
            aiReleaseImport(scene);
            return model;
        }
    }

    /* --- Process bones and bind poses --- */

    if (!r3d_process_bones_and_offsets(&model, scene)) {
        TraceLog(LOG_WARNING, "R3D: Failed to process bones, model will not be animated");
    }

    /* --- Calculate model bounding box --- */

    R3D_UpdateModelBoundingBox(&model, false);

    /* --- Clean up and return the model --- */

    aiReleaseImport(scene);

    return model;
}

R3D_Model R3D_LoadModelFromMemory(const char* fileType, const void* data, unsigned int size)
{
    R3D_Model model = { 0 };

    /* --- Configure Assimp properties --- */

    // Set the global scale from 'cm' to 'm'
    struct aiPropertyStore* props = aiCreatePropertyStore();
    aiSetImportPropertyFloat(props, AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, 0.01f);

    /* --- Import scene using Assimp --- */

    if (fileType != NULL && fileType[0] == '.') {
        // pHint takes the format without the point, unlike raylib
        fileType++;
    }

    const struct aiScene* scene = aiImportFileFromMemoryWithProperties(
        data, size, R3D_ASSIMP_FLAGS, fileType, props);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        TraceLog(LOG_ERROR, "R3D: Assimp error; %s", aiGetErrorString());
        return model;
    }

    aiReleasePropertyStore(props);

    /* --- Process materials --- */

    if (!process_assimp_materials(scene, &model.materials, &model.materialCount, NULL)) {
        TraceLog(LOG_ERROR, "R3D: Unable to load materials; The model will be invalid");
        return model;
    }

    /* --- Initialize model and allocate meshes --- */

    model.meshCount = scene->mNumMeshes;
    model.meshes = malloc(model.meshCount * sizeof(R3D_Mesh));

    if (model.meshes == NULL) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for meshes; The model will be invalid");
        R3D_UnloadModel(&model, true);
        aiReleaseImport(scene);
        return model;
    }

    /* --- Process all meshes --- */

    for (uint32_t i = 0; i < model.meshCount; i++) {
        if (!r3d_process_assimp_mesh(&model, i, scene->mMeshes[i], scene, true)) {
            TraceLog(LOG_ERROR, "R3D: Unable to load mesh [%d]; The model will be invalid", i);
            R3D_UnloadModel(&model, true);
            aiReleaseImport(scene);
            return model;
        }
    }

    /* --- Process bones and bind poses --- */

    if (!r3d_process_bones_and_offsets(&model, scene)) {
        TraceLog(LOG_WARNING, "R3D: Failed to process bones, model will not be animated");
    }

    /* --- Calculate model bounding box --- */

    R3D_UpdateModelBoundingBox(&model, false);

    /* --- Clean up and return the model --- */

    aiReleaseImport(scene);

    return model;
}

R3D_Model R3D_LoadModelFromMesh(const R3D_Mesh* mesh)
{
    R3D_Model model = { 0 };

    if (!mesh) {
        return model;
    }

    model.meshes = malloc(sizeof(R3D_Mesh));
    model.meshes[0] = *mesh;
    model.meshCount = 1;

    model.materials = malloc(sizeof(R3D_Material));
    model.materials[0] = R3D_GetDefaultMaterial();
    model.materialCount = 1;

    model.meshMaterials = malloc(sizeof(int));
    model.meshMaterials[0] = 0;

    R3D_UpdateModelBoundingBox(&model, false);

    return model;
}

void R3D_UnloadModel(const R3D_Model* model, bool unloadMaterials)
{
    for (int i = 0; i < model->meshCount; i++) {
        R3D_UnloadMesh(&model->meshes[i]);
    }

    if (unloadMaterials) {
        for (int i = 0; i < model->materialCount; i++) {
            R3D_UnloadMaterial(&model->materials[i]);
        }
    }

    free(model->meshMaterials);
    free(model->materials);
    free(model->meshes);

    free(model->boneOffsets);
    free(model->bones);
}

void R3D_UpdateModelBoundingBox(R3D_Model* model, bool updateMeshBoundingBoxes)
{
    if (!model || !model->meshes) {
        return;
    }

    Vector3 minVertex = { +FLT_MAX, +FLT_MAX, +FLT_MAX };
    Vector3 maxVertex = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    for (uint32_t i = 0; i < model->meshCount; i++) {
        R3D_Mesh* mesh = &model->meshes[i];
        if (updateMeshBoundingBoxes) {
            R3D_UpdateMeshBoundingBox(mesh);
        }
        minVertex = Vector3Min(minVertex, mesh->aabb.min);
        maxVertex = Vector3Max(maxVertex, mesh->aabb.max);
    }

    model->aabb.min = minVertex;
    model->aabb.max = maxVertex;
}

R3D_ModelAnimation* R3D_LoadModelAnimations(const char* fileName, int* animCount, int targetFrameRate)
{
    *animCount = 0;

    /* --- Configure Assimp properties --- */

    struct aiPropertyStore* props = aiCreatePropertyStore();
    aiSetImportPropertyFloat(props, AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, 0.01f);

    /* --- Import scene using Assimp --- */

    const struct aiScene* scene = aiImportFileExWithProperties(fileName, R3D_ASSIMP_FLAGS, NULL, props);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        TraceLog(LOG_ERROR, "R3D: Assimp error loading animations: %s", aiGetErrorString());
        aiReleasePropertyStore(props);
        return NULL;
    }

    aiReleasePropertyStore(props);

    /* --- Check if there are animations --- */

    if (scene->mNumAnimations == 0) {
        TraceLog(LOG_INFO, "R3D: No animations found in model '%s'", fileName);
        aiReleaseImport(scene);
        return NULL;
    }

    TraceLog(LOG_INFO, "R3D: Found %d animations in model '%s'", scene->mNumAnimations, fileName);

    /* --- Allocate animations array --- */

    R3D_ModelAnimation* animations = calloc(scene->mNumAnimations, sizeof(R3D_ModelAnimation));
    if (!animations) {
        TraceLog(LOG_ERROR, "R3D: Unable to allocate memory for animations");
        aiReleaseImport(scene);
        return NULL;
    }

    /* --- Process each animation --- */

    int successCount = 0;
    for (unsigned int i = 0; i < scene->mNumAnimations; i++) {
        const struct aiAnimation* aiAnim = scene->mAnimations[i];

        TraceLog(LOG_INFO, "R3D: Processing animation %d: '%s'", i, aiAnim->mName.data);

        if (r3d_process_animation(&animations[successCount], scene, aiAnim, targetFrameRate)) {
            successCount++;
        } else {
            TraceLog(LOG_ERROR, "R3D: Failed to process animation %d", i);
        }
    }

    /* --- Clean up and return --- */

    aiReleaseImport(scene);

    if (successCount == 0) {
        TraceLog(LOG_ERROR, "R3D: No animations were successfully loaded");
        free(animations);
        return NULL;
    }

    if (successCount < (int)scene->mNumAnimations) {
        TraceLog(LOG_WARNING, "R3D: Only %d out of %d animations were successfully loaded", successCount, scene->mNumAnimations);
        R3D_ModelAnimation* resizedAnims = realloc(animations, successCount * sizeof(R3D_ModelAnimation));
        if (resizedAnims) animations = resizedAnims;
    }

    *animCount = successCount;
    TraceLog(LOG_INFO, "R3D: Successfully loaded %d animations", successCount);

    return animations;
}

void R3D_UnloadModelAnimations(R3D_ModelAnimation* animations, int animCount)
{
    if (!animations) return;
    
    for (int i = 0; i < animCount; i++) {
        R3D_ModelAnimation* anim = &animations[i];
        
        // Free frame poses
        if (anim->framePoses) {
            for (int frame = 0; frame < anim->frameCount; frame++) {
                free(anim->framePoses[frame]);
            }
            free(anim->framePoses);
        }
        
        // Free bones
        free(anim->bones);
    }
    
    free(animations);
}

R3D_ModelAnimation* R3D_GetModelAnimation(R3D_ModelAnimation* animations, int animCount, const char* name)
{
    if (!animations || !name) return NULL;
    
    for (int i = 0; i < animCount; i++) {
        if (strcmp(animations[i].name, name) == 0) {
            return &animations[i];
        }
    }
    
    return NULL;
}

void R3D_ListModelAnimations(R3D_ModelAnimation* animations, int animCount)
{
    if (!animations) {
        TraceLog(LOG_INFO, "R3D: No animations available");
        return;
    }
    
    TraceLog(LOG_INFO, "R3D: Available animations (%d):", animCount);
    for (int i = 0; i < animCount; i++) {
        TraceLog(LOG_INFO, "  [%d] '%s' - %d frames, %d bones", 
                 i, animations[i].name, animations[i].frameCount, animations[i].boneCount);
    }
}
