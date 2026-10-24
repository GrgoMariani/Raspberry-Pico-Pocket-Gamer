#ifndef GPU3D_H_
#define GPU3D_H_

#include <stdint.h>
#include "gpu.h"

/*
 * ---------------------------------------------------------------------------
 * Small software 3d renderer for the pocket gamer
 * ---------------------------------------------------------------------------
 * Everything is fixed point (16.16) - the RP2040 has no FPU and the library is
 * called from the draw loop of every 3d game, so a single float multiply per
 * pixel would already be too expensive.
 *
 * Pipeline:
 *   model space --(scale, rotate, translate)--> world space
 *   world space --(camera position/orientation)--> camera space (+z is away)
 *   camera space --(perspective or orthographic)--> screen space
 * Faces are depth sorted (painter's algorithm), back faces are culled and the
 * remaining ones are flat shaded with a single directional light. Convex faces
 * may additionally be filled with the 64x64 texture that is currently loaded
 * into the GPU (that is how the cube of the main menu gets its sprite).
 */

typedef int32_t fixed;

#define FX_ONE          65536
#define FX(x)           ((fixed)((x)*FX_ONE))
#define FX_INT(x)       ((fixed)((int32_t)(x) * 65536))
#define FX_TO_INT(a)    ((int32_t)((a) >> 16))
#define FX_MUL(a, b)    ((fixed)(((int64_t)(a) * (int64_t)(b)) >> 16))
#define FX_DIV(a, b)    ((fixed)(((int64_t)(a) * 65536) / (b)))

typedef struct {
    fixed x;
    fixed y;
    fixed z;
} Vec3;

/* angles are counted in 1024ths of a full turn */
#define ANG_FULL    1024
#define ANG(deg)    ((int32_t)(((deg)*ANG_FULL)/360))

fixed GPU3D_Sin(int32_t angle);
fixed GPU3D_Cos(int32_t angle);
/* square root of a Q16 value, result is Q16 */
fixed GPU3D_Sqrt(fixed value);

void GPU3D_Rotate(Vec3 * v, int32_t angleX, int32_t angleY, int32_t angleZ);
void GPU3D_Normalize(Vec3 * v);
fixed GPU3D_Dot(const Vec3 * a, const Vec3 * b);
void GPU3D_Cross(const Vec3 * a, const Vec3 * b, Vec3 * out);
Vec3 GPU3D_Vec(fixed x, fixed y, fixed z);

/* ------------------------------------------------------------------ camera */

typedef enum {
    GPU3D_PERSPECTIVE = 0,
    GPU3D_ORTHO = 1
} GPU3D_Projection;

/* focalLength: distance of the projection plane from the eye (Q16) */
void GPU3D_Init(void);
void GPU3D_SetPerspective(int16_t centerX, int16_t centerY, fixed focalLength, fixed zNear);
/* pixelsPerUnit: how many pixels one world unit occupies (Q16) */
void GPU3D_SetOrtho(int16_t centerX, int16_t centerY, fixed pixelsPerUnit);
void GPU3D_SetNearPlane(fixed zNear);
void GPU3D_SetCameraOrientation(int32_t pitch, int32_t yaw, int32_t roll);
void GPU3D_SetCameraPosition(fixed x, fixed y, fixed z);
void GPU3D_SetCameraPositionVec(const Vec3 * position);
/* direction from a surface towards the light, in camera space; normalized */
void GPU3D_SetLightDirection(fixed x, fixed y, fixed z);
/* shade level (0..256) of a face that points away from the light */
void GPU3D_SetAmbient(int16_t ambient);

void GPU3D_WorldToCamera(const Vec3 * world, Vec3 * camera);
/* returns 0 when the point is in front of the near plane (not drawable) */
uint8_t GPU3D_Project(const Vec3 * camera, int16_t * screenX, int16_t * screenY);

/* ------------------------------------------------------------------- meshes */

#define GPU3D_MAX_FACE_VERTS 6
#define GPU3D_MAX_VERTS 48
#define GPU3D_MAX_FACES 64

typedef struct {
    uint8_t nVerts;
    uint8_t verts[GPU3D_MAX_FACE_VERTS];
    /* texture coordinates in 0..TEXTURE_SIZE, only used when textured != 0 */
    uint8_t u[GPU3D_MAX_FACE_VERTS];
    uint8_t v[GPU3D_MAX_FACE_VERTS];
    GPU_Color color;
    uint8_t textured;   /* fill the face with the texture loaded in the GPU */
    uint8_t shaded;     /* apply the directional light to color */
    uint8_t twoSided;   /* also draw the face when it points away from the eye */
} GPU3D_Face;

typedef struct {
    const Vec3 * verts;
    uint8_t nVerts;
    const GPU3D_Face * faces;
    uint8_t nFaces;
} GPU3D_Mesh;

/* scale may be NULL for a uniform scale of 1.0 */
void GPU3D_DrawMesh(const GPU3D_Mesh * mesh, const Vec3 * position, const Vec3 * scale,
                    int32_t rotX, int32_t rotY, int32_t rotZ, uint8_t wireframe);
/* same, but every face is drawn in one colour (the colour of the mesh is only
   known at compile time, games often need it at run time) */
void GPU3D_DrawMeshTinted(const GPU3D_Mesh * mesh, const Vec3 * position, const Vec3 * scale,
                          int32_t rotX, int32_t rotY, int32_t rotZ,
                          GPU_Color color, uint8_t wireframe);
/* one 3d line in world coordinates */
void GPU3D_DrawLineWorld(GPU_Color color, const Vec3 * a, const Vec3 * b);

/* convenience wrapper: axis aligned (or rotated around y) box */
void GPU3D_DrawBox(GPU_Color color, const Vec3 * center, fixed sizeX, fixed sizeY, fixed sizeZ,
                   int32_t rotX, int32_t rotY, int32_t rotZ, uint8_t wireframe);

/* ready made unit meshes (1x1x1, centered at the origin) */
extern const GPU3D_Mesh GPU3D_CubeMesh;            /* flat shaded cube */
extern const GPU3D_Mesh GPU3D_TexturedCubeMesh;    /* cube with the GPU texture on every face */
extern const GPU3D_Mesh GPU3D_BallMesh;            /* low poly ball (octahedron) */
extern const GPU3D_Mesh GPU3D_PyramidMesh;         /* four sided pyramid */
extern const GPU3D_Mesh GPU3D_QuadMesh;            /* 1x1 horizontal quad, facing up */

/* polygon filling itself lives in gpu.c (GPU_DrawConvexPolygon and
   GPU_DrawTexturedPolygon) because that is where the framebuffer is */

#endif//GPU3D_H_
