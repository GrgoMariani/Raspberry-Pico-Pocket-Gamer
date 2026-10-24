#include "gpu3d.h"
#include "gpu3d_tables.h"

#include <string.h>

/* ------------------------------------------------------------------ helpers */

fixed GPU3D_Sin(int32_t angle)
{
    /* *2 instead of <<1: shifting a negative signed value is undefined */
    return (fixed)_sinLutQ15[angle & (GPU3D_SIN_ENTRIES-1)] * 2;
}

fixed GPU3D_Cos(int32_t angle)
{
    return GPU3D_Sin(angle + GPU3D_SIN_ENTRIES/4);
}

static uint32_t _isqrt32(uint32_t value)
{
    uint32_t result = 0;
    uint32_t bit = 1u << 30;
    while (bit > value)
        bit >>= 2;
    while (bit)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
            result >>= 1;
        bit >>= 2;
    }
    return result;
}

static uint64_t _isqrt64(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > value)
        bit >>= 2;
    while (bit)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
            result >>= 1;
        bit >>= 2;
    }
    return result;
}

fixed GPU3D_Sqrt(fixed value)
{
    if (value <= 0)
        return 0;
    /* sqrt of a Q16 value: the integer sqrt delivers Q8, shift it up to Q16 */
    fixed result = (fixed)(_isqrt32((uint32_t)value) << 8);
    if (result == 0)
        return 0;
    /* one Newton step to regain the precision lost by the shift */
    result = (result + FX_DIV(value, result)) / 2;
    return result;
}

Vec3 GPU3D_Vec(fixed x, fixed y, fixed z)
{
    Vec3 result;
    result.x = x; result.y = y; result.z = z;
    return result;
}

fixed GPU3D_Dot(const Vec3 * a, const Vec3 * b)
{
    return FX_MUL(a->x, b->x) + FX_MUL(a->y, b->y) + FX_MUL(a->z, b->z);
}

void GPU3D_Cross(const Vec3 * a, const Vec3 * b, Vec3 * out)
{
    out->x = FX_MUL(a->y, b->z) - FX_MUL(a->z, b->y);
    out->y = FX_MUL(a->z, b->x) - FX_MUL(a->x, b->z);
    out->z = FX_MUL(a->x, b->y) - FX_MUL(a->y, b->x);
}

void GPU3D_Normalize(Vec3 * v)
{
    uint64_t len2 = (uint64_t)((int64_t)v->x*v->x + (int64_t)v->y*v->y + (int64_t)v->z*v->z);
    if (len2 == 0)
        return;
    fixed len = (fixed)_isqrt64(len2);
    if (len == 0)
        len = 1;
    v->x = (fixed)(((int64_t)v->x * 65536) / len);
    v->y = (fixed)(((int64_t)v->y * 65536) / len);
    v->z = (fixed)(((int64_t)v->z * 65536) / len);
}

void GPU3D_Rotate(Vec3 * v, int32_t angleX, int32_t angleY, int32_t angleZ)
{
    fixed cx = GPU3D_Cos(angleX), sx = GPU3D_Sin(angleX);
    fixed cy = GPU3D_Cos(angleY), sy = GPU3D_Sin(angleY);
    fixed cz = GPU3D_Cos(angleZ), sz = GPU3D_Sin(angleZ);
    fixed x = v->x, y = v->y, z = v->z;
    /* around x */
    fixed y1 = FX_MUL(y, cx) - FX_MUL(z, sx);
    fixed z1 = FX_MUL(y, sx) + FX_MUL(z, cx);
    /* around y */
    fixed x2 = FX_MUL(x, cy) + FX_MUL(z1, sy);
    fixed z2 = -FX_MUL(x, sy) + FX_MUL(z1, cy);
    /* around z */
    v->x = FX_MUL(x2, cz) - FX_MUL(y1, sz);
    v->y = FX_MUL(x2, sz) + FX_MUL(y1, cz);
    v->z = z2;
}

/* ---------------------------------------------------------------- matrices */

typedef struct {
    fixed m[3][3];
} Mat3;

static Mat3 _camMatrixCache;
static uint8_t _camMatrixValid = 0;

static void _matIdentity(Mat3 * r)
{
    for (uint8_t i = 0; i < 3; i++)
        for (uint8_t j = 0; j < 3; j++)
            r->m[i][j] = (i == j) ? FX_ONE : 0;
}

static void _matMul(const Mat3 * a, const Mat3 * b, Mat3 * out)
{
    Mat3 tmp;
    for (uint8_t i = 0; i < 3; i++)
        for (uint8_t j = 0; j < 3; j++)
            tmp.m[i][j] = FX_MUL(a->m[i][0], b->m[0][j])
                        + FX_MUL(a->m[i][1], b->m[1][j])
                        + FX_MUL(a->m[i][2], b->m[2][j]);
    *out = tmp;
}

static void _matRotX(int32_t angle, Mat3 * out)
{
    fixed c = GPU3D_Cos(angle), s = GPU3D_Sin(angle);
    _matIdentity(out);
    out->m[1][1] = c;  out->m[1][2] = -s;
    out->m[2][1] = s;  out->m[2][2] = c;
}

static void _matRotY(int32_t angle, Mat3 * out)
{
    fixed c = GPU3D_Cos(angle), s = GPU3D_Sin(angle);
    _matIdentity(out);
    out->m[0][0] = c;  out->m[0][2] = s;
    out->m[2][0] = -s; out->m[2][2] = c;
}

static void _matRotZ(int32_t angle, Mat3 * out)
{
    fixed c = GPU3D_Cos(angle), s = GPU3D_Sin(angle);
    _matIdentity(out);
    out->m[0][0] = c;  out->m[0][1] = -s;
    out->m[1][0] = s;  out->m[1][1] = c;
}

static void _matApply(const Mat3 * m, const Vec3 * in, Vec3 * out)
{
    Vec3 r;
    r.x = FX_MUL(m->m[0][0], in->x) + FX_MUL(m->m[0][1], in->y) + FX_MUL(m->m[0][2], in->z);
    r.y = FX_MUL(m->m[1][0], in->x) + FX_MUL(m->m[1][1], in->y) + FX_MUL(m->m[1][2], in->z);
    r.z = FX_MUL(m->m[2][0], in->x) + FX_MUL(m->m[2][1], in->y) + FX_MUL(m->m[2][2], in->z);
    *out = r;
}

/* ------------------------------------------------------------------- camera */

static GPU3D_Projection _projection = GPU3D_PERSPECTIVE;
static int16_t _centerX = GPU_X/2;
static int16_t _centerY = GPU_Y/2;
static fixed _focal = FX(220.0);   /* pixels per world unit at distance 1 */
static fixed _zNear = FX(0.1);
static fixed _orthoScale = FX(40.0);
static Vec3 _camPos = { 0, 0, 0 };
static int32_t _camPitch = 0;
static int32_t _camYaw = 0;
static int32_t _camRoll = 0;
/* direction from the surface towards the light, in camera space */
static Vec3 _light = { FX(-0.40), FX(0.80), FX(-0.45) };
static int16_t _ambient = 96;

/* camera matrix: inverse of "yaw, then pitch, then roll", cached because it is
   needed once per object and once per 3d line */
static const Mat3 * _cameraMatrix(void)
{
    if (!_camMatrixValid)
    {
        Mat3 ry, rx, rz, tmp;
        _matRotY(-_camYaw, &ry);
        _matRotX(-_camPitch, &rx);
        _matRotZ(-_camRoll, &rz);
        _matMul(&rx, &ry, &tmp);
        _matMul(&rz, &tmp, &_camMatrixCache);
        _camMatrixValid = 1;
    }
    return &_camMatrixCache;
}

static void _invalidateCameraMatrix(void)
{
    _camMatrixValid = 0;
}

void GPU3D_WorldToCamera(const Vec3 * world, Vec3 * camera)
{
    Vec3 relative;
    relative.x = world->x - _camPos.x;
    relative.y = world->y - _camPos.y;
    relative.z = world->z - _camPos.z;
    _matApply(_cameraMatrix(), &relative, camera);
}

uint8_t GPU3D_Project(const Vec3 * camera, int16_t * screenX, int16_t * screenY)
{
    if (camera->z < _zNear || (_projection == GPU3D_PERSPECTIVE && camera->z <= 0))
        return 0;
    int32_t x, y;
    if (_projection == GPU3D_PERSPECTIVE)
    {
        x = (int32_t)(((int64_t)camera->x * _focal) / camera->z);
        y = (int32_t)(((int64_t)camera->y * _focal) / camera->z);
    }
    else
    {
        x = FX_MUL(camera->x, _orthoScale);
        y = FX_MUL(camera->y, _orthoScale);
    }
    x = FX_TO_INT(x);
    y = FX_TO_INT(y);
    if (x > 4000) x = 4000;
    if (x < -4000) x = -4000;
    if (y > 4000) y = 4000;
    if (y < -4000) y = -4000;
    *screenX = (int16_t)(_centerX + x);
    *screenY = (int16_t)(_centerY - y);   // the screen y axis points down
    return 1;
}

void GPU3D_SetPerspective(int16_t centerX, int16_t centerY, fixed focalLength, fixed zNear)
{
    _projection = GPU3D_PERSPECTIVE;
    _centerX = centerX;
    _centerY = centerY;
    _focal = focalLength;
    _zNear = zNear;
}

void GPU3D_SetOrtho(int16_t centerX, int16_t centerY, fixed pixelsPerUnit)
{
    _projection = GPU3D_ORTHO;
    _centerX = centerX;
    _centerY = centerY;
    _orthoScale = pixelsPerUnit;
    /* a parallel projection has no singularity at z == 0 */
    _zNear = -FX_INT(10000);
}

void GPU3D_SetNearPlane(fixed zNear)
{
    _zNear = zNear;
}

void GPU3D_SetCameraOrientation(int32_t pitch, int32_t yaw, int32_t roll)
{
    _camPitch = pitch;
    _camYaw = yaw;
    _camRoll = roll;
    _invalidateCameraMatrix();
}

void GPU3D_SetCameraPosition(fixed x, fixed y, fixed z)
{
    _camPos.x = x; _camPos.y = y; _camPos.z = z;
}

void GPU3D_SetCameraPositionVec(const Vec3 * position)
{
    _camPos = *position;
}

void GPU3D_SetLightDirection(fixed x, fixed y, fixed z)
{
    _light.x = x; _light.y = y; _light.z = z;
    GPU3D_Normalize(&_light);
}

void GPU3D_SetAmbient(int16_t ambient)
{
    if (ambient < 0) ambient = 0;
    if (ambient > 256) ambient = 256;
    _ambient = ambient;
}

void GPU3D_Init(void)
{
    _projection = GPU3D_PERSPECTIVE;
    _centerX = GPU_X/2;
    _centerY = GPU_Y/2;
    _focal = FX(220.0);
    _zNear = FX(0.1);
    _orthoScale = FX(40.0);
    _camPos.x = _camPos.y = _camPos.z = 0;
    _camPitch = _camYaw = _camRoll = 0;
    _invalidateCameraMatrix();
    GPU3D_SetLightDirection(FX(-0.40), FX(0.80), FX(-0.45));
    _ambient = 96;
}

/* ------------------------------------------------------------- mesh drawing */

typedef struct {
    Vec3 p;         /* camera space */
    int32_t u, v;   /* texture coordinate, Q8 */
} ClipVertex;

static Vec3 _cameraVerts[GPU3D_MAX_VERTS];
static int32_t _faceDepth[GPU3D_MAX_FACES];
static int16_t _faceLevel[GPU3D_MAX_FACES];
static uint8_t _faceOrder[GPU3D_MAX_FACES];
static uint8_t _faceCount;

static void _drawMeshInternal(const GPU3D_Mesh * mesh, const Vec3 * position, const Vec3 * scale,
                              int32_t rotX, int32_t rotY, int32_t rotZ,
                              GPU_Color colorOverride, uint8_t useOverride, uint8_t wireframe)
{
    if (!mesh || mesh->nVerts == 0 || mesh->nFaces == 0)
        return;
    if (mesh->nVerts > GPU3D_MAX_VERTS || mesh->nFaces > GPU3D_MAX_FACES)
        return;

    Vec3 objectScale = { FX_ONE, FX_ONE, FX_ONE };
    if (scale)
    {
        objectScale = *scale;
        if (objectScale.x == 0) objectScale.x = FX_ONE;
        if (objectScale.y == 0) objectScale.y = FX_ONE;
        if (objectScale.z == 0) objectScale.z = FX_ONE;
    }
    Vec3 objectPos = { 0, 0, 0 };
    if (position)
        objectPos = *position;

    /* one matrix for the whole object: camera rotation * object rotation.
       Axis aligned objects (the common case: boxes on a grid) skip the object
       rotation completely, which saves ~100 fixed point multiplies per object */
    Mat3 objRot, xformStorage;
    const Mat3 * camRot = _cameraMatrix();
    const Mat3 * xform = camRot;
    if (rotX || rotY || rotZ)
    {
        Mat3 rx, ry, rz, tmp;
        _matRotX(rotX, &rx);
        _matRotY(rotY, &ry);
        _matRotZ(rotZ, &rz);
        _matMul(&ry, &rx, &tmp);
        _matMul(&rz, &tmp, &objRot);
        _matMul(camRot, &objRot, &xformStorage);
        xform = &xformStorage;
    }

    /* translation: cameraRotation * (objectPos - cameraPos) */
    Vec3 rel;
    rel.x = objectPos.x - _camPos.x;
    rel.y = objectPos.y - _camPos.y;
    rel.z = objectPos.z - _camPos.z;
    Vec3 translation;
    _matApply(camRot, &rel, &translation);

    /* transform every vertex into camera space */
    for (uint8_t i = 0; i < mesh->nVerts; i++)
    {
        Vec3 scaled;
        scaled.x = FX_MUL(mesh->verts[i].x, objectScale.x);
        scaled.y = FX_MUL(mesh->verts[i].y, objectScale.y);
        scaled.z = FX_MUL(mesh->verts[i].z, objectScale.z);
        Vec3 rotated;
        _matApply(xform, &scaled, &rotated);
        _cameraVerts[i].x = rotated.x + translation.x;
        _cameraVerts[i].y = rotated.y + translation.y;
        _cameraVerts[i].z = rotated.z + translation.z;
    }

    /* per face: visibility, depth and lighting */
    _faceCount = 0;
    for (uint8_t f = 0; f < mesh->nFaces; f++)
    {
        const GPU3D_Face * face = &mesh->faces[f];
        if (face->nVerts < 3 || face->nVerts > GPU3D_MAX_FACE_VERTS)
            continue;
        uint8_t inFront = 0;
        int64_t depthSum = 0;
        for (uint8_t i = 0; i < face->nVerts; i++)
        {
            const Vec3 * p = &_cameraVerts[face->verts[i]];
            depthSum += p->z;
            if (p->z >= _zNear)
                inFront = 1;
        }
        if (!inFront)
            continue;
        if (_projection == GPU3D_PERSPECTIVE)
        {
            uint8_t anyPositive = 0;
            for (uint8_t i = 0; i < face->nVerts; i++)
                if (_cameraVerts[face->verts[i]].z > 0)
                    anyPositive = 1;
            if (!anyPositive)
                continue;
        }

        const Vec3 * v0 = &_cameraVerts[face->verts[0]];
        const Vec3 * v1 = &_cameraVerts[face->verts[1]];
        const Vec3 * v2 = &_cameraVerts[face->verts[2]];
        int64_t d1x = v1->x - v0->x, d1y = v1->y - v0->y, d1z = v1->z - v0->z;
        int64_t d2x = v2->x - v0->x, d2y = v2->y - v0->y, d2z = v2->z - v0->z;
        int64_t nx = d1y*d2z - d1z*d2y;
        int64_t ny = d1z*d2x - d1x*d2z;
        int64_t nz = d1x*d2y - d1y*d2x;

        /* the normal points away from the eye -> back face */
        if (nz > 0 && !face->twoSided)
            continue;

        int16_t level = 256;
        if (face->shaded)
        {
            /* scale the normal down so that its square still fits in 64 bits */
            int64_t biggest = nx < 0 ? -nx : nx;
            int64_t t = ny < 0 ? -ny : ny; if (t > biggest) biggest = t;
            t = nz < 0 ? -nz : nz; if (t > biggest) biggest = t;
            uint8_t shift = 0;
            while (biggest > ((int64_t)1 << 20) && shift < 62)
            {
                biggest >>= 1;
                shift++;
            }
            nx >>= shift; ny >>= shift; nz >>= shift;
            int64_t len = (int64_t)_isqrt64((uint64_t)(nx*nx + ny*ny + nz*nz));
            if (len > 0)
            {
                int64_t dot = nx*_light.x + ny*_light.y + nz*_light.z;
                int32_t intensity = (int32_t)(dot / len);   /* Q16, -1.0 .. 1.0 */
                if (intensity < 0) intensity = 0;
                if (intensity > FX_ONE) intensity = FX_ONE;
                int32_t computed = _ambient + (int32_t)(((int64_t)(256 - _ambient) * intensity) >> 16);
                level = (int16_t)(computed > 256 ? 256 : computed);
            }
        }

        _faceDepth[_faceCount] = (int32_t)(depthSum / face->nVerts);
        _faceLevel[_faceCount] = level;
        _faceOrder[_faceCount] = f;
        _faceCount++;
    }

    /* painter's algorithm: draw the far faces first */
    for (uint8_t i = 1; i < _faceCount; i++)
    {
        uint8_t face = _faceOrder[i];
        int32_t depth = _faceDepth[i];
        int16_t level = _faceLevel[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && _faceDepth[j] < depth)
        {
            _faceOrder[j+1] = _faceOrder[j];
            _faceDepth[j+1] = _faceDepth[j];
            _faceLevel[j+1] = _faceLevel[j];
            j--;
        }
        _faceOrder[j+1] = face;
        _faceDepth[j+1] = depth;
        _faceLevel[j+1] = level;
    }

    /* rasterize */
    for (uint8_t i = 0; i < _faceCount; i++)
    {
        const GPU3D_Face * face = &mesh->faces[_faceOrder[i]];
        ClipVertex input[GPU3D_MAX_FACE_VERTS];
        ClipVertex output[GPU3D_MAX_FACE_VERTS + 2];
        uint8_t count = 0;
        for (uint8_t k = 0; k < face->nVerts; k++)
        {
            input[k].p = _cameraVerts[face->verts[k]];
            input[k].u = ((int32_t)face->u[k]) << 8;
            input[k].v = ((int32_t)face->v[k]) << 8;
        }
        /* Sutherland-Hodgman clip against the near plane */
        for (uint8_t k = 0; k < face->nVerts; k++)
        {
            uint8_t l = (uint8_t)((k + 1) % face->nVerts);
            ClipVertex * a = &input[k];
            ClipVertex * b = &input[l];
            uint8_t aIn = (a->p.z >= _zNear);
            uint8_t bIn = (b->p.z >= _zNear);
            if (aIn)
                output[count++] = *a;
            if (aIn != bIn)
            {
                fixed dz = b->p.z - a->p.z;
                fixed t = (dz != 0) ? FX_DIV(_zNear - a->p.z, dz) : 0;
                if (count < GPU3D_MAX_FACE_VERTS + 2)
                {
                    output[count].p.x = a->p.x + FX_MUL(t, b->p.x - a->p.x);
                    output[count].p.y = a->p.y + FX_MUL(t, b->p.y - a->p.y);
                    output[count].p.z = _zNear;
                    output[count].u = a->u + (int32_t)FX_MUL(t, b->u - a->u);
                    output[count].v = a->v + (int32_t)FX_MUL(t, b->v - a->v);
                    count++;
                }
            }
            if (count > GPU3D_MAX_FACE_VERTS + 1)
                break;
        }
        if (count < 3)
            continue;

        int16_t sx[GPU3D_MAX_FACE_VERTS + 2];
        int16_t sy[GPU3D_MAX_FACE_VERTS + 2];
        int32_t tu[GPU3D_MAX_FACE_VERTS + 2];
        int32_t tv[GPU3D_MAX_FACE_VERTS + 2];
        uint8_t allVisible = 1;
        for (uint8_t k = 0; k < count; k++)
        {
            if (!GPU3D_Project(&output[k].p, &sx[k], &sy[k]))
            {
                allVisible = 0;
                break;
            }
            tu[k] = output[k].u;
            tv[k] = output[k].v;
        }
        if (!allVisible)
            continue;

        GPU_Color color = useOverride ? colorOverride : face->color;
        if (face->shaded)
            color = GPU_ShadeColor(color, _faceLevel[i]);

        if (wireframe)
        {
            for (uint8_t k = 0; k < count; k++)
            {
                uint8_t l = (uint8_t)((k + 1) % count);
                GPU_DrawLine(color, sx[k], sy[k], sx[l], sy[l]);
            }
            continue;
        }
        if (face->textured)
        {
            /* the texture coordinates keep their Q8 precision while filling */
            GPU_DrawTexturedPolygon(sx, sy, tu, tv, count, face->shaded ? _faceLevel[i] : 256);
            continue;
        }
        GPU_DrawConvexPolygon(color, sx, sy, count);
    }
}

void GPU3D_DrawMesh(const GPU3D_Mesh * mesh, const Vec3 * position, const Vec3 * scale,
                    int32_t rotX, int32_t rotY, int32_t rotZ, uint8_t wireframe)
{
    _drawMeshInternal(mesh, position, scale, rotX, rotY, rotZ, 0, 0, wireframe);
}

void GPU3D_DrawMeshTinted(const GPU3D_Mesh * mesh, const Vec3 * position, const Vec3 * scale,
                          int32_t rotX, int32_t rotY, int32_t rotZ,
                          GPU_Color color, uint8_t wireframe)
{
    _drawMeshInternal(mesh, position, scale, rotX, rotY, rotZ, color, 1, wireframe);
}

void GPU3D_DrawBox(GPU_Color color, const Vec3 * center, fixed sizeX, fixed sizeY, fixed sizeZ,
                   int32_t rotX, int32_t rotY, int32_t rotZ, uint8_t wireframe)
{
    Vec3 scale = GPU3D_Vec(sizeX, sizeY, sizeZ);
    _drawMeshInternal(&GPU3D_CubeMesh, center, &scale, rotX, rotY, rotZ, color, 1, wireframe);
}

void GPU3D_DrawLineWorld(GPU_Color color, const Vec3 * a, const Vec3 * b)
{
    Vec3 ca, cb;
    Vec3 rel;
    const Mat3 * cam = _cameraMatrix();
    rel.x = a->x - _camPos.x; rel.y = a->y - _camPos.y; rel.z = a->z - _camPos.z;
    _matApply(cam, &rel, &ca);
    rel.x = b->x - _camPos.x; rel.y = b->y - _camPos.y; rel.z = b->z - _camPos.z;
    _matApply(cam, &rel, &cb);

    uint8_t aIn = (ca.z >= _zNear);
    uint8_t bIn = (cb.z >= _zNear);
    if (!aIn && !bIn)
        return;
    if (aIn != bIn)
    {
        fixed dz = cb.z - ca.z;
        fixed t = (dz != 0) ? FX_DIV(_zNear - ca.z, dz) : 0;
        Vec3 clipped;
        clipped.x = ca.x + FX_MUL(t, cb.x - ca.x);
        clipped.y = ca.y + FX_MUL(t, cb.y - ca.y);
        clipped.z = _zNear;
        if (aIn) cb = clipped; else ca = clipped;
    }
    int16_t sx0, sy0, sx1, sy1;
    if (!GPU3D_Project(&ca, &sx0, &sy0))
        return;
    if (!GPU3D_Project(&cb, &sx1, &sy1))
        return;
    GPU_DrawLine(color, sx0, sy0, sx1, sy1);
}

/* ----------------------------------------------------------- built in meshes */

#define H FX(0.5)

static const Vec3 _cubeVerts[8] = {
    { -H, -H, -H },   /* 0 */
    {  H, -H, -H },   /* 1 */
    {  H,  H, -H },   /* 2 */
    { -H,  H, -H },   /* 3 */
    { -H, -H,  H },   /* 4 */
    {  H, -H,  H },   /* 5 */
    {  H,  H,  H },   /* 6 */
    { -H,  H,  H },   /* 7 */
};

/* every face is wound so that (v1-v0)x(v2-v0) points out of the cube */
static const GPU3D_Face _cubeFaces[6] = {
    { 4, {0,3,2,1}, {0}, {0}, C_WHITE, 0, 1, 0 },    /* front  (-z) */
    { 4, {4,5,6,7}, {0}, {0}, C_WHITE, 0, 1, 0 },    /* back   (+z) */
    { 4, {0,4,7,3}, {0}, {0}, C_WHITE, 0, 1, 0 },    /* left   (-x) */
    { 4, {1,2,6,5}, {0}, {0}, C_WHITE, 0, 1, 0 },    /* right  (+x) */
    { 4, {0,1,5,4}, {0}, {0}, C_WHITE, 0, 1, 0 },    /* bottom (-y) */
    { 4, {3,7,6,2}, {0}, {0}, C_WHITE, 0, 1, 0 },    /* top    (+y) */
};

const GPU3D_Mesh GPU3D_CubeMesh = { _cubeVerts, 8, _cubeFaces, 6 };

/* same cube, but every face is mapped with the texture currently in the GPU */
static const GPU3D_Face _texturedCubeFaces[6] = {
    { 4, {0,3,2,1}, {0,0,64,64}, {0,64,64,0}, C_WHITE, 1, 1, 0 },
    { 4, {4,5,6,7}, {0,0,64,64}, {0,64,64,0}, C_WHITE, 1, 1, 0 },
    { 4, {0,4,7,3}, {0,0,64,64}, {0,64,64,0}, C_WHITE, 1, 1, 0 },
    { 4, {1,2,6,5}, {0,0,64,64}, {0,64,64,0}, C_WHITE, 1, 1, 0 },
    { 4, {0,1,5,4}, {0,0,64,64}, {0,64,64,0}, C_WHITE, 1, 1, 0 },
    { 4, {3,7,6,2}, {0,0,64,64}, {0,64,64,0}, C_WHITE, 1, 1, 0 },
};

const GPU3D_Mesh GPU3D_TexturedCubeMesh = { _cubeVerts, 8, _texturedCubeFaces, 6 };

/* octahedron: a cheap "ball" */
static const Vec3 _ballVerts[6] = {
    { 0,  FX(0.7), 0 },        /* 0 top    */
    { 0, -FX(0.7), 0 },        /* 1 bottom */
    { -H, 0, -H },             /* 2 front  */
    {  H, 0, -H },             /* 3 right  */
    {  H, 0,  H },             /* 4 back   */
    { -H, 0,  H },             /* 5 left   */
};

static const GPU3D_Face _ballFaces[8] = {
    { 3, {3,2,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {4,3,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {5,4,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {2,5,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {2,3,1}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {3,4,1}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {4,5,1}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {5,2,1}, {0}, {0}, C_WHITE, 0, 1, 0 },
};

const GPU3D_Mesh GPU3D_BallMesh = { _ballVerts, 6, _ballFaces, 8 };

/* four sided pyramid (base included) */
static const Vec3 _pyramidVerts[5] = {
    { 0,  H, 0 },              /* 0 apex   */
    { -H, -H, -H },            /* 1 */
    {  H, -H, -H },            /* 2 */
    {  H, -H,  H },            /* 3 */
    { -H, -H,  H },            /* 4 */
};

static const GPU3D_Face _pyramidFaces[5] = {
    { 3, {2,1,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {3,2,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {4,3,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 3, {1,4,0}, {0}, {0}, C_WHITE, 0, 1, 0 },
    { 4, {1,2,3,4}, {0}, {0}, C_WHITE, 0, 1, 0 },
};

const GPU3D_Mesh GPU3D_PyramidMesh = { _pyramidVerts, 5, _pyramidFaces, 5 };

/* a horizontal 1x1 quad, used for roads, water and landing pads */
static const Vec3 _quadVerts[4] = {
    { FX(-0.5), 0, FX(-0.5) },
    { FX(0.5),  0, FX(-0.5) },
    { FX(0.5),  0, FX(0.5) },
    { FX(-0.5), 0, FX(0.5) },
};

static const GPU3D_Face _quadFaces[1] = {
    { 4, {0,3,2,1}, {0}, {0}, C_WHITE, 0, 0, 1 },
};

const GPU3D_Mesh GPU3D_QuadMesh = { _quadVerts, 4, _quadFaces, 1 };
