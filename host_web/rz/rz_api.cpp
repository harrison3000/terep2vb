// Implementação da interface C (WebGL 2, no canvas da página). As alocações e
// uploads para a GPU acontecem na criação e nas funções de carga; rzRender não aloca.

#include <cmath>
#include <cstring>

#include "rz_internal.h"

using namespace rz;

namespace {

bool isPositiveFinite(float v) {
    return v > 0.0f && v < 3.0e38f;
}

void setSize(RzContext* ctx, int32_t width, int32_t height) {
    constexpr float degToRad = kPi / 180.0f;
    const float focal = 1.0f / tanf(ctx->fovY * 0.5f * degToRad);
    ctx->width  = width;
    ctx->height = height;
    ctx->focalY = focal;
    ctx->focalX = focal * float(height) / float(width);
}

// Cor de fundo em float a partir do índice e da paleta do atlas; sem índice
// ou sem atlas, a cor padrão.
void updateBackgroundColor(RzContext* ctx) {
    if (ctx->backgroundIndex < 0 || !ctx->hasAtlasPalette) {
        ctx->backgroundR = kBackgroundR;
        ctx->backgroundG = kBackgroundG;
        ctx->backgroundB = kBackgroundB;
        return;
    }
    constexpr float inv255 = 1.0f / 255.0f;
    const uint8_t* rgb = ctx->atlasPalette + ctx->backgroundIndex * 3;
    ctx->backgroundR = float(rgb[0]) * inv255;
    ctx->backgroundG = float(rgb[1]) * inv255;
    ctx->backgroundB = float(rgb[2]) * inv255;
}

// Um tamanho de canvas que serve
bool validSize(int32_t width, int32_t height) {
    return width >= 1 && height >= 1 && width <= kMaxWindowSize && height <= kMaxWindowSize;
}

// Erro da última rzCreate que falhou (não há contexto onde guardar);
// devolvido por rzGetError(NULL).
int32_t     g_createError = RZ_OK;
const char* g_createErrorFunction = nullptr;

int32_t recordCreateError(const char* function, int32_t err) {
    if (err != RZ_OK) {
        g_createError = err;
        g_createErrorFunction = function;
    }
    return err;
}

} // namespace

extern "C" {

int32_t rzGetError(RzContext* ctx, const char** outFunction) {
    int32_t err;
    const char* function;
    if (ctx) {
        err = ctx->firstError;
        function = ctx->firstErrorFunction;
        ctx->firstError = RZ_OK;
        ctx->firstErrorFunction = nullptr;
        ctx->errorCount = 0;
    } else if (g_createError != RZ_OK) {
        err = g_createError;
        function = g_createErrorFunction;
        g_createError = RZ_OK;
        g_createErrorFunction = nullptr;
    } else {
        err = RZ_ERR_INVALID_ARG;      // contexto nulo sem falha de criação registrada
        function = nullptr;
    }
    if (outFunction) *outFunction = function;
    return err;
}

int32_t rzCreate(int32_t width, int32_t height, int32_t shadowSize, int32_t cascades, float reach,
                 RzContext** outCtx) {
    if (!outCtx) return recordCreateError("rzCreate", RZ_ERR_INVALID_ARG);
    *outCtx = nullptr;
    if (!validSize(width, height)) return recordCreateError("rzCreate", RZ_ERR_SIZE);
    if (shadowSize == 0) shadowSize = kShadowMapSize;
    if (shadowSize < 256 || shadowSize > 4096 || (shadowSize & (shadowSize - 1)) || (cascades != 1 && cascades != 3) ||
        (cascades == 1 && !(reach >= 1.0f && reach <= 60.0f)))               // pega NaN
        return recordCreateError("rzCreate", RZ_ERR_INVALID_ARG);
    if (!canvasContext()) return recordCreateError("rzCreate", RZ_ERR_GL);

    RzContext* ctx = new RzContext;     // padrões nos inicializadores dos membros
    ctx->shadowSize = shadowSize;       // pedido; createShadowMaps limita ao driver
    ctx->cascades   = cascades;
    ctx->shadowReach = cascades == 1 ? reach : kCascadeSplit1;
    setSize(ctx, width, height);
    updateTerrainBounds(ctx);
    if (!createRenderer(ctx)) {
        rzDestroy(ctx);
        return recordCreateError("rzCreate", RZ_ERR_GL);
    }
    *outCtx = ctx;
    return RZ_OK;
}

void rzDestroy(RzContext* ctx) {
    if (!ctx) return;
    destroyAllObjects(ctx);
    destroyRenderer(ctx);
    delete ctx;
}

int32_t rzSetHeightmap(RzContext* ctx, const uint8_t* data,
                       int32_t width, int32_t height) {
    if (!ctx || !data) return recordError(ctx, "rzSetHeightmap", RZ_ERR_INVALID_ARG);
    if (width != kGridSize || height != kGridSize) return recordError(ctx, "rzSetHeightmap", RZ_ERR_SIZE);

    ctx->heights.assign(data, data + kVertexCount);
    ctx->heightSource = data;                  // relido por rzUpdateTerrain
    updateTerrainBounds(ctx);
    return recordError(ctx, "rzSetHeightmap", buildTerrainMesh(ctx) ? RZ_OK : RZ_ERR_GL);
}

int32_t rzSetTerrainScale(RzContext* ctx,
                          float cellSize, float heightScale) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!isPositiveFinite(cellSize) || !isPositiveFinite(heightScale)) return recordError(ctx, "rzSetTerrainScale", RZ_ERR_INVALID_ARG);
    ctx->cellSize    = cellSize;
    ctx->heightScale = heightScale;
    updateTerrainBounds(ctx);
    if (ctx->hasTerrain()) {          // posições e iluminação dependem da escala
        if (!buildTerrainMesh(ctx)) return recordError(ctx, "rzSetTerrainScale", RZ_ERR_GL);
    }
    return RZ_OK;
}

int32_t rzLoadTileAtlas(RzContext* ctx, const uint8_t* pcx, int32_t size) {
    if (!ctx || !pcx) return recordError(ctx, "rzLoadTileAtlas", RZ_ERR_INVALID_ARG);

    // Decodifica tudo antes de mexer na textura: se o arquivo falhar, o atlas anterior fica.
    // Só o canto superior esquerdo, 256x256
    PcxImage img;
    int32_t err = decodePcx(pcx, size, img);
    if (err == RZ_OK && (img.width < kAtlasSize || img.height < kAtlasSize)) err = RZ_ERR_SIZE;
    if (err != RZ_OK) return recordError(ctx, "rzLoadTileAtlas", err);
    std::vector<uint8_t> indices(size_t(kAtlasSize) * kAtlasSize);
    for (int32_t y = 0; y < kAtlasSize; ++y)
        std::memcpy(indices.data() + y * kAtlasSize, img.pixels.data() + size_t(y) * size_t(img.width), size_t(kAtlasSize));
    const uint8_t* paletteRGB = img.palette.data();

    // 256 blocos x (256 + 64 + 16 + 4 + 1) texels, por nível; temporário da carga
    constexpr int32_t kTexels = kMaxTiles * 341;
    std::vector<uint32_t> tiles(kTexels);
    buildAtlasLevels(tiles.data(), indices.data(), paletteRGB);

    if (!ctx->atlasTex) glGenTextures(1, &ctx->atlasTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ctx->atlasTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    constexpr int32_t kLevelOffset[kMipLevels] = { 0, 256, 320, 336, 340 };
    for (int32_t level = 0; level < kMipLevels; ++level) {
        const int32_t side = kTileSize >> level;
        glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_RGBA8, side, side, kMaxTiles, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, tiles.data() + kMaxTiles * kLevelOffset[level]);
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, kMipLevels - 1);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // filtro fixo: ampliação nearest; redução nearest no nível, linear entre níveis
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (glGetError() != GL_NO_ERROR) return recordError(ctx, "rzLoadTileAtlas", RZ_ERR_GL);
    ctx->hasAtlas = true;
    setSpritePalette(ctx, paletteRGB);                // sprites: paleta do jogo
    std::memcpy(ctx->atlasPalette, paletteRGB, sizeof(ctx->atlasPalette));
    ctx->hasAtlasPalette = true;
    markObjectsDirty(ctx);                            // as cores sólidas sem paleta própria
    updateBackgroundColor(ctx);                       // fundo: índice nesta paleta
    return RZ_OK;
}

int32_t rzSetTileMap(RzContext* ctx, const uint8_t* data,
                     int32_t width, int32_t height) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!data) {
        ctx->hasTileMap = false;     // a malha mantém os blocos; só deixam de ser usados
        ctx->tileSource = nullptr;
        return RZ_OK;
    }
    if (width != kGridSize || height != kGridSize) return recordError(ctx, "rzSetTileMap", RZ_ERR_SIZE);

    ctx->tileMap.assign(data, data + kVertexCount);
    ctx->tileSource = data;          // relido por rzUpdateTerrain
    ctx->hasTileMap = true;
    if (ctx->hasTerrain()) {          // o bloco vai no vértice (relevo e sombra ficam)
        if (!rebuildTerrainTiles(ctx)) return recordError(ctx, "rzSetTileMap", RZ_ERR_GL);
    }
    return RZ_OK;
}

int32_t rzUpdateTerrain(RzContext* ctx) {
    if (!ctx || !ctx->hasTerrain() || !ctx->heightSource) return recordError(ctx, "rzUpdateTerrain", RZ_ERR_INVALID_ARG);
    return recordError(ctx, "rzUpdateTerrain", updateTerrain(ctx) ? RZ_OK : RZ_ERR_GL);
}

int32_t rzSetBackgroundColor(RzContext* ctx, uint8_t paletteIndex) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    ctx->backgroundIndex = paletteIndex;
    updateBackgroundColor(ctx);
    return RZ_OK;
}

int32_t rzSetFog(RzContext* ctx, float start, float end) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!(start >= 0.0f && start < end && end <= kFogMaxEnd)) return recordError(ctx, "rzSetFog", RZ_ERR_INVALID_ARG);   // pega NaN
    ctx->fogStart = start;     // só valores: uniforms, far plane e cortes leem no rzRender
    ctx->fogEnd   = end;
    return RZ_OK;
}

int32_t rzSetCameraTarget(RzContext* ctx, int32_t id, int32_t vertex) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (id < 0) {
        ctx->cameraTargetObject = -1;
        return RZ_OK;
    }
    if (id >= int32_t(ctx->objects.size()) || !ctx->objects[id].alive ||
        vertex < 0 || vertex >= ctx->objects[id].vertexCount()) {
        return recordError(ctx, "rzSetCameraTarget", RZ_ERR_INVALID_ARG);
    }
    // Outro alvo: seguindo um, a câmera vai até o novo (rz_camera.cpp); vindo
    // da visão geral, começa direto atrás dele
    if (id != ctx->cameraTargetObject) ctx->retargetPending = ctx->followInitialized;
    ctx->cameraTargetObject = id;
    ctx->cameraTargetVertex = vertex;
    return RZ_OK;
}

int32_t rzSetCamera(RzContext* ctx, int32_t mode) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (mode < RZ_CAMERA_FOLLOW || mode > RZ_CAMERA_OVERVIEW) return recordError(ctx, "rzSetCamera", RZ_ERR_INVALID_ARG);
    ctx->cameraMode = mode;
    return RZ_OK;
}

int32_t rzSetCameraInternalAxes(RzContext* ctx, const RzVertex* forward,
                                const RzVertex* up) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!forward || !up) return recordError(ctx, "rzSetCameraInternalAxes", RZ_ERR_INVALID_ARG);
    // Eixos do legado (coluna, linha, cima) -> mundo (x, y para cima, z)
    Vec3 f = { forward->x, forward->z, forward->y };
    Vec3 u = { up->x, up->z, up->y };
    const float lf = sqrtf(dot(f, f));
    const float lu0 = sqrtf(dot(u, u));
    if (!(lf > 1.0e-12f && lf < 1.0e30f) || !(lu0 > 1.0e-12f && lu0 < 1.0e30f)) {
        return recordError(ctx, "rzSetCameraInternalAxes", RZ_ERR_INVALID_ARG);   // nulo ou NaN
    }
    f = { f.x / lf, f.y / lf, f.z / lf };
    const float along = dot(u, f);                     // up perpendicular a forward
    u = { u.x - f.x * along, u.y - f.y * along, u.z - f.z * along };
    const float lu = sqrtf(dot(u, u));
    if (!(lu > 1.0e-6f * lu0)) return recordError(ctx, "rzSetCameraInternalAxes", RZ_ERR_INVALID_ARG);   // paralelos
    u = { u.x / lu, u.y / lu, u.z / lu };
    ctx->internalForward = f;
    ctx->internalUp      = u;
    ctx->internalValid   = true;
    return RZ_OK;
}

int32_t rzSetCameraFov(RzContext* ctx, float degrees) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!(degrees >= kFovYMin && degrees <= kFovYMax)) return recordError(ctx, "rzSetCameraFov", RZ_ERR_INVALID_ARG);   // pega NaN
    ctx->fovY = degrees;
    setSize(ctx, ctx->width, ctx->height);        // só a focal muda; vale no próximo rzRender
    return RZ_OK;
}

int32_t rzSetCameraFollow(RzContext* ctx, float distance, float height,
                          float stiffness) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!(distance > 0.0f) || !(height == height) || !(stiffness > 0.0f)) return recordError(ctx, "rzSetCameraFollow", RZ_ERR_INVALID_ARG);
    if (stiffness > 1.0f) stiffness = 1.0f;
    ctx->followDistance  = distance;
    ctx->followHeight    = height;
    ctx->followStiffness = stiffness;
    return RZ_OK;
}

int32_t rzSetSize(RzContext* ctx, int32_t width, int32_t height) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    if (!validSize(width, height)) return recordError(ctx, "rzSetSize", RZ_ERR_SIZE);
    setSize(ctx, width, height);                  // vale no próximo rzRender
    return RZ_OK;
}

int32_t rzRender(RzContext* ctx, float frames) {
    if (!ctx) return RZ_ERR_INVALID_ARG;
    ctx->frameStep = frames > 0.0f ? (frames < 15.0f ? frames : 15.0f) : 0.0f;   // NaN também: 0
    renderFrame(ctx);           // sem glGetError: no WebGL ele espera a GPU (só na carga)
    return RZ_OK;
}

} // extern "C"
