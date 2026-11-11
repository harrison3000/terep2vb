// The renderer's internal types and functions (WebGL 2). Not part of the
// public interface.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "../../common/pcx.hpp"
#include "renderizeitor.h"
#include "rz_gl.h"

namespace rz {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr int32_t kGridSize      = 256;                         // vertices per side
constexpr int32_t kQuadsPerSide  = kGridSize - 1;               // 255
constexpr int32_t kVertexCount   = kGridSize * kGridSize;       // 65,536
constexpr int32_t kTriangleCount = kQuadsPerSide * kQuadsPerSide * 2; // 130,050
constexpr int32_t kPaletteSize   = 766;                         // sum of 3 heights: 0..765
// Terrain chunks (culling): kChunkQuads x kChunkQuads quads, contiguous in the VBO
constexpr int32_t kChunkQuads    = 16;
constexpr int32_t kChunksPerSide = (kQuadsPerSide + kChunkQuads - 1) / kChunkQuads;   // 16 (the last one with 15)
constexpr int32_t kChunkCount    = kChunksPerSide * kChunksPerSide;

// Default background colour (before rzSetBackgroundColor + rzLoadTileAtlas), already in float
constexpr float kBackgroundR = 32.0f / 255.0f;
constexpr float kBackgroundG = 40.0f / 255.0f;
constexpr float kBackgroundB = 48.0f / 255.0f;

constexpr float kPi              = 3.14159265358979f;
constexpr float kFovYDegrees     = 50.0f;   // rzSetCameraFov's default
constexpr float kFovYMin         = 10.0f;
constexpr float kFovYMax         = 120.0f;
constexpr float kAmbient         = 0.3f;
constexpr float kTexturedShading = 0.35f;   // strength of the light on the textured ground (0 = plain texture)

// Overview (no target): the whole terrain, seen from the +z side, fixed pitch
constexpr float kOverviewPitchDegrees = 50.0f;

// The legacy's scale: byte 0 = height 0, byte 255 = 16 tiles (world 255 x 255 x 16)
constexpr float kDefaultCellSize    = 1.0f;
constexpr float kDefaultHeightScale = 16.0f / 255.0f;

// Chase camera, on the scale of a car of ~0.85 tile
constexpr float kFollowDistance  = 3.0f;    // rope (tiles)
constexpr float kFollowHeight    = 1.0f;    // height over the target (tiles)
constexpr float kFollowStiffness = 0.08f;
constexpr float kFollowClearance    = 0.5f;   // least height wanted over the ground (tiles)
constexpr float kFollowMinClearance = 0.1f;   // hard limit over the ground (tiles)
constexpr float kTerrainBubble      = 0.8f;   // chase: radius (tiles) of the sphere around the eye that the
                                              // ground does not enter (the near plane and its corners do not cut the hill)
constexpr float kFollowClimb        = 0.2f;   // damping going up
// Point looked at (smoothed target): the target's shaking on bumpy ground
// does not reach the camera. Strong vertically, light horizontally (little lag).
constexpr float kFollowLookXZ       = 0.5f;
constexpr float kFollowLookY        = 0.1f;
// Dead zone of the camera's angle: a bubble (sphere) around the target,
// which the target pushes from inside. The camera looks at the bubble's
// centre; while the target moves inside it, the look does not change. The
// position (rope) follows as usual.
constexpr float kCameraBubbleRadius  = 0.3f;    // tiles
constexpr float kInternalNear        = 0.02f;   // internal camera: near plane (tiles)
constexpr float kFreeNear            = 0.05f;   // free camera: near plane at most (tiles; down to 0.1 over the ground)
constexpr float kInternalEnterFrames = 30.0f;   // chase -> internal: smooth flight (frames)

// The camera's change of target (rzSetCameraTarget with another object):
// instead of teleporting, the camera goes to the new target. Speeds and
// durations are in frames of 60 Hz; the camera moves by each frame's time
// (frameStep).
//   - target up to kRetargetTurnDistance tiles away: it only turns (the
//     point looked at slides to the new target in kRetargetTurnFrames) and
//     the rope brings the camera;
//   - further: a flight in an arc to behind the new target, rising up to
//     kRetargetArcHeight tiles in the middle (full height from
//     kRetargetArcFull tiles on), no faster than kRetargetMaxSpeed.
constexpr float kRetargetTurnDistance = 10.0f;   // tiles
constexpr float kRetargetTurnFrames   = 30.0f;
constexpr float kRetargetArcFull      = 60.0f;   // tiles
constexpr float kRetargetArcHeight    = 25.0f;   // tiles
constexpr float kRetargetMaxSpeed     = 100.0f / 60.0f;   // tiles per frame
constexpr float kRetargetMinFrames    = 30.0f;

// Into and out of the overview: a flight of fixed duration (the distance varies a lot).
// The fog moves from the eye (following) to the map's centre (overview, from
// terrainRadius up to + kOverviewFogDepth tiles: only the continuation fades
// in it); the shadows change mode halfway.
constexpr float kOverviewFrames   = 120.0f;
constexpr float kOverviewFogDepth = 60.0f;   // tiles

constexpr int32_t kMaxWindowSize = 8192;

// A vertex/sprite coordinate from the host (tiles): finite and |v| < 1e6
// (the comparison fails with NaN; infinity is past the limit)
inline bool validCoord(float v) { return v > -1.0e6f && v < 1.0e6f; }

// Fog by distance (only following a target; the overview has none): clear
// up to the start, gone into the background colour at the end (tiles, 3D
// distance to the eye, smoothstep curve). Past the end nothing is drawn.
// Default 30..65; changes at runtime with rzSetFog (RzContext::fogStart/fogEnd),
// the end limited to kFogMaxEnd.
constexpr float kFogStartDefault = 30.0f;
constexpr float kFogEndDefault   = 65.0f;
constexpr float kFogCullMargin = 10.0f;   // tiles: objects past the end + radius + margin
constexpr float kViewCullMargin = 1.0f;   // tiles past the object's sphere: wheels and lines (frustum culling)
                                          // get neither into the screen nor into the shadows
                                          // (the margin covers long shadows inwards)

// The world's edge (rz_border.cpp): the terrain going on past the map, up to
// the fog's reach + slack, and the boundary wall with red Xs.
// Bands of the continuation's mesh, from inside out: up to `limit` tiles
// from the border, cells of `step` tiles. The last one goes to kSkirtExtent.
struct SkirtBand { int32_t limit, step; };
constexpr SkirtBand kSkirtBands[] = { { 8, 1 }, { 16, 2 }, { 32, 4 }, { 128, 16 } };
constexpr int32_t kSkirtBandCount = int32_t(sizeof(kSkirtBands) / sizeof(kSkirtBands[0]));
constexpr int32_t kSkirtExtent   = kSkirtBands[kSkirtBandCount - 1].limit;   // 128: aligns the cells of 16
constexpr float   kSkirtBlend    = 24.0f;  // tiles from the border's height to the generated one
constexpr float   kSkirtNoiseLow  = 60.0f; // noise amplitude (unit of the byte), period 32
constexpr float   kSkirtNoiseHigh = 18.0f; // period 12
constexpr int32_t kSkirtTileBand  = 8;     // drawing lots: quads up to 8 tiles from the nearest border point
constexpr int32_t kSkirtJitterPercent = 12;   // % of the cells that draw lots instead of continuing the border
constexpr float   kSkirtMeanderGrowth = 0.35f; // shift along the border: up to 0.35 x distance...
constexpr float   kSkirtMeanderMax    = 12.0f; // ...limited to 12 tiles
constexpr int32_t kSkirtMeanderPeriod = 24;    // period of the meander's noise (tiles)
constexpr float   kBorderWallHeight = 6.0f;   // tiles over the ground
constexpr float   kBorderTopFade    = 2.5f;   // fades out over the top tiles
constexpr float   kBorderFadeFar    = 25.0f;  // target 25 tiles away: starts to show
constexpr float   kBorderFadeNear   = 6.0f;   // at 6 tiles: fully visible
constexpr float   kBorderXSize      = 2.125f; // cell of each circle (tiles), fixed to the world
constexpr float   kFogMaxEnd = 150.0f;   // rzSetFog: past kFogMaxGround the continuation ends before the fog
constexpr float   kFogMaxGround = 120.0f; // (its end shows: for an editor, not for the game)
static_assert(kFogEndDefault <= kFogMaxGround && kFogMaxGround <= float(kSkirtExtent) && kFogMaxGround <= kFogMaxEnd,
              "the continuation has to go up to the end of the fog");

// Transparency of the PCX textures (atlas and objects): the palette's index
// 255 becomes alpha 0 (the rest, alpha 255); the shader discards the pixel
// with alpha < 0.5. Mipmaps with an alpha-weighted average (the colour of
// 255 does not bleed at the edges); the objects', by texel class (rz_texture.cpp).
// The shadow ignores it (the holes still cast a shadow).
constexpr int32_t  kTransparentIndex = 255;
// Glass texels in the objects' textures: indices 240..254 (as in the legacy:
// what is behind goes through the translucency table of tone index − 240)
// become a grey filter like the glass's (rz_glass.cpp), drawn in a pass of
// their own (drawTexturedGlass). In the texel: rgb = the grey, alpha =
// kGlassAlpha; opaque 255, transparent 0. Tone 0 (black) becomes
// kGlassTexelTone0, as gl_renderer does with the glass polygons.
constexpr int32_t  kFirstGlassIndex  = 240;
constexpr uint32_t kGlassAlpha       = 128;
constexpr int32_t  kGlassTexelTone0  = 6;   // a matter of taste (black darkened too much); change it if it looks bad

// Sprites (rz_sprites.cpp): smoke/debris/water, quads facing the camera
constexpr int32_t kMaxSprites          = 256;    // the maximum checked in the legacy
constexpr int32_t kSpriteFrames        = 8;      // noise variations (array texture)
constexpr int32_t kSpriteFrameTicks    = 4;      // frames of 60 Hz per picture
constexpr int32_t kSpriteNoiseSize     = 64;     // side of the noise texture
constexpr int32_t kSpriteClicks        = 2;      // "spray clicks" per picture
constexpr int32_t kSpriteDotsPerClick  = 260;    // dots per click
constexpr float   kSpriteClickRadius   = 0.36f;  // radius of the click (fraction of the side)
constexpr float   kSpriteClickJitter   = 0.10f;  // shift of the click's centre
// Debris: alpha inside the circle by the clicks that hit the pixel (0, 1, 2+);
// outside the circle, 0 (tuned by eye)
constexpr uint8_t kSpriteAlphaByHits[3] = { 40, 200, 255 };
constexpr float   kSmokeNoiseSpeed     = 0.4f;   // smoke: the noise's advance per second (60 frames)

// Shadows: cascades of the directional light (details in rz_shadow.cpp), by
// distance to the eye: 0 from 0 to kCascadeSplit0 tiles, 1 up to
// kCascadeSplit1 (both with terrain + objects, redone every frame) and 2 up
// to the end of the fog (terrain only; a box around the eye, redone when it
// moves kCascadeFarRecenter tiles or the terrain changes). A transition of
// kCascadeBlend tiles between them. In the overview, only 2, with the whole
// terrain and the objects.
constexpr int32_t kShadowMaps           = 3;
// The cascades in a depth array texture (one layer per cascade, all of the
// same side): rzCreate's default, limited to GL_MAX_TEXTURE_SIZE at creation
// (ctx->shadowSize). With ctx->cascades == 1 (the lighter qualities), a
// single cascade, up to ctx->shadowReach, in layer 0; the others' matrices
// are copies.
constexpr int32_t kShadowMapSize        = 2048;
constexpr float   kCascadeSplit0        = 8.0f;   // tiles from the eye
constexpr float   kCascadeSplit1        = 30.0f;
constexpr float   kCascadeBlend         = 2.0f;   // tiles of transition before each split
constexpr float   kCascadeFarRecenter   = 10.0f;  // tiles moved before cascade 2 is redone
constexpr float   kCascadeFarMargin     = 4.0f;   // tiles past the fog + recentring
constexpr int32_t kUnitShadow           = 1;      // texture unit of the shadows (0: atlas/object)
// Each object's positions and indices in a uniform block (ObjectData, in the
// object.vert and object_depth.vert shaders, std140 layout):
// kMaxObjectVertices vec4 of position and then kMaxObjectIndices int, four
// per ivec4. 16 KB, the minimum every WebGL 2 takes in a block.
constexpr int32_t kMaxObjectVertices    = 256;
constexpr int32_t kMaxObjectIndices     = 3072;
constexpr int32_t kObjectIndicesOffset  = kMaxObjectVertices * 16;              // bytes
constexpr int32_t kObjectDataSize       = kObjectIndicesOffset + kMaxObjectIndices * 4;
constexpr GLuint  kObjectDataBinding    = 0;      // the block's binding point
constexpr float   kShadowTexturedDim  = 0.48f;  // textured ground in the shadow (was 0.6; 20 % darker)
constexpr float   kShadowLight        = kAmbient * 0.8f;   // light in the shadow (flat colours and objects):
                                                         // 20 % below the ambient
constexpr float   kShadowOffsetFactor = 2.0f;   // glPolygonOffset in the depth pass
constexpr float   kShadowOffsetUnits  = 4.0f;
constexpr float   kShadowNormalOffset = 1.5f;   // normal offset when reading, in texels of the map
constexpr float   kShadowTerminator   = 0.15f;  // n · l below this: the shadow comes in (smoothstep)
constexpr float   kCascadeSizeStep    = 1.1f;   // boxes 0 and 1: size in steps (does not change every frame)

// Textures: 16x16 blocks in an array texture (one layer per block), with
// mipmaps 16, 8, 4, 2, 1 made at loading (2x2 average, as in the software one).
constexpr int32_t kTileSize      = 16;
constexpr int32_t kMipLevels     = 5;
constexpr int32_t kMaxTiles      = 256;                         // the index is a byte
constexpr int32_t kAtlasSize     = 256;                         // atlas: 16 x 16 blocks of 16x16

// ---------------------------------------------------------------------------
// Maths
// ---------------------------------------------------------------------------

struct Vec3 {
    float x, y, z;
};

inline Vec3  operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3  operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline float dot(Vec3 a, Vec3 b)       { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// 4x4 matrix, column vectors: v' = M · v. Access m[row, column].
// Sent to the shader by setMatrices (which transposes it: WebGL does not).
struct Mat4 {
    float e[16];

    float&       operator[](int row, int col)       { return e[row * 4 + col]; }
    const float& operator[](int row, int col) const { return e[row * 4 + col]; }

    static Mat4 identity();
};

Mat4 operator*(const Mat4& a, const Mat4& b);

// The 6 planes (a, b, c, d: inside with a·x + b·y + c·z + d >= 0, (a, b, c)
// unit) of a projection · view matrix (perspective or orthographic)
struct Frustum {
    float plane[6][4];
};

Frustum frustumOf(const Mat4& m);
bool    boxInFrustum(const Frustum& f, Vec3 lo, Vec3 hi);   // the box may show (conservative)
bool    sphereInFrustum(const Frustum& f, Vec3 center, float radius);

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

// An object's triangle: vertex indices and the polygon it comes from (for the colour).
// The triangle's corners as positions in Object::indices (the vertex and the UV come from there).
struct ObjectTriangle {
    int32_t  a, b, c;
    uint16_t polygon;
};

// An object's vertex sent to the GPU: position in the world, the polygon's
// flat light (grey 0x00LLLLLL; in memory B, G, R, 0, read as a normalised
// vec4), which multiplies the texture, and UV.
// An object's line (rzAddObjectLine): a prism of square section between two
// vertices, redone at each upload of the VBO (6 faces, 2 triangles each)
constexpr int32_t kLineVertices = 36;
struct ObjectLine {
    uint16_t a, b;
    int16_t  palette;
    float    half;          // half the thickness, world
};

struct GpuVertex {
    float    x, y, z;
    uint32_t normal;           // the face's normal (packNormal); the light is done in the shader
    float    u, v;
};
static_assert(sizeof(GpuVertex) == 24);

// A corner of the objects' triangles (static VBO): in the polygons, only the
// object's vertex, the polygon (start and length in `indices`) and the UV;
// position and normal come from the positions and indices textures in the
// vertex shader. In the lines (vertex = -1), position and normal directly,
// redone at each update.
struct ObjectVertex {
    float    x, y, z;          // lines only
    uint32_t normal;           // lines only (packNormal)
    float    u, v;             // textured polygons only
    int32_t  vertex;           // index of the object's vertex, or -1 (line)
    int32_t  polygonStart, polygonLength;
    uint32_t color;            // R, G, B in bytes and the corner's kind in the alpha (kCorner*)
};
static_assert(sizeof(ObjectVertex) == 40);

// The corner's kind, in the alpha of ObjectVertex::color (object.frag):
// textured (samples the texture at the UV), solid colour (the vertex's
// colour: rzAddObjectPolygon and lines) or a hole (the solid colour 255,
// transparent: not drawn, but casts a shadow)
constexpr uint32_t kCornerTextured = 0, kCornerHole = 128, kCornerSolid = 255;
// Object::polygonPalette of a shaded polygon (rzAddObjectShadedPolygon)
constexpr int16_t  kPolygonShaded = -2;

// A terrain vertex: one per triangle corner (unindexed mesh, because the
// flat colour and the block belong to the triangle/quad, not to the grid point).
struct TerrainVertex {
    float    x, y, z;
    uint32_t color;            // the triangle's flat colour (no light), 0x00RRGGBB
    uint8_t  u, v, layer;      // the quad's corner (0/1) and the atlas block
    uint8_t  pad;
    uint32_t normal;           // the face's normal (packNormal); the light is done in the shader
};
static_assert(sizeof(TerrainVertex) == 24);

// A glass vertex (rz_glass.cpp): position, the polygon's outward normal, grey
struct GlassVertex {
    float x, y, z;
    float nx, ny, nz;
    float tint;                // 0 (black) .. 1 (transparent) = tone / 15
};
static_assert(sizeof(GlassVertex) == 28);

// Wheels (rz_wheels.cpp): tyres, a solid of revolution with rounded shoulders
constexpr int32_t  kWheelSegments      = 24;        // sides of the ring
constexpr int32_t  kWheelShoulderSteps = 3;         // steps that round each shoulder of the tyre
constexpr float    kWheelWidth    = 0.4f;           // width = 0.4 x diameter (12:30)
constexpr uint32_t kWheelColor    = 0x001C1C1Cu;    // nearly black (so the light still shows)
// Shell of fixed colour: shoulder (x2, with steps) + tread (x1) + the wall of
// the rim's recess (x2). Each band = kWheelSegments quads (2 triangles).
constexpr int32_t  kWheelSkinBands = 2 * kWheelShoulderSteps + 3;
constexpr int32_t  kWheelSideVerts = kWheelSkinBands * kWheelSegments * 6;     // the tyre's shell (fixed colour)
// Textured faces: the sidewall (flat ring, x2) + the recessed rim (fan to
// the centre, x2).
constexpr int32_t  kWheelCapVerts  = 2 * kWheelSegments * 6                    // sidewalls (rings)
                                   + 2 * kWheelSegments * 3;                   // recessed rims (fans)

// Glass: a "blurred" specular highlight (Blinn-Phong of low exponent)
constexpr float kGlassSpecular  = 0.45f;
constexpr float kGlassShininess = 12.0f;

struct Object {
    bool      alive   = false;
    bool      positioned = false;   // has had rzUpdateObjectVertices
    bool      gpuDirty   = false;   // corners/VBO out of date (polygons, lines): upload at the next frame
    bool      buffersDirty = false; // new polygons/lines: reallocate the VBO and upload the indices again
                                    // (in the frame, once, and not at each polygon of the loading)
    bool      positionsDirty = false;   // new positions (rzUpdateObjectVertices): upload to the block again
    Vec3      center = { 0.0f, 0.0f, 0.0f };   // bounding sphere (redone at each update)
    float     radius = 0.0f;

    std::vector<Vec3>     world;          // positions in the world, updated by the host
    std::vector<int32_t>  polygonStart;   // in `indices`
    std::vector<int32_t>  polygonLength;  // without the closing index
    std::vector<uint16_t> indices;        // a copy of the polygons' indices (without the closings)
    std::vector<float>    uvs;            // (u, v) of each entry of `indices`; 0 in the solid colour ones
    std::vector<int16_t>  polygonPalette; // colour index (rzAddObjectPolygon), -1 (own UVs) or
                                          // kPolygonShaded (each corner's colour in cornerShade)
    std::vector<uint8_t>  cornerShade;    // colour index of each entry of `indices` (shaded; 0 in the others)
    std::vector<ObjectTriangle> triangles; // each polygon's fan, built at loading
    std::vector<ObjectLine> lines;        // rzAddObjectLine: a thin prism made at upload
    std::vector<ObjectVertex> staging;    // triangles.size() * 3 + lines.size() * kLineVertices
    std::vector<float>    positionStaging;   // world in vec4, for the block
    GLuint    dataBuffer = 0;       // the object's uniform block: positions and indices

    GLuint    vao = 0;
    GLuint    vbo = 0;
    // Glass (rz_glass.cpp): translucent polygons, a VBO of its own
    std::vector<int32_t>  glassStart, glassLength;
    std::vector<uint8_t>  glassTone;      // 0..15
    std::vector<uint16_t> glassIndices;
    std::vector<GlassVertex> glassStaging; // the fan's triangles, filled at upload
    GLuint    glassVao = 0, glassVbo = 0;
    bool      glassDirty = false;
    bool      glassResized = false;   // new polygons: reallocate the glass VBO at the next upload

    // Wheels (rz_wheels.cpp): empty staging = no wheels
    uint16_t  wheelVertex[4] = {};
    uint8_t   wheelFront[4] = {};
    float     wheelDiameter[4] = {};    // world
    float     wheelSteer = 0.0f;        // front ones; radians, positive = left
    float     wheelRoll[4] = {};        // each wheel's accumulated turn; radians
    float     wheelSpeed[4] = {};       // each wheel's speed (tiles/frame); turns the wheels
    std::vector<TerrainVertex> wheelStaging;    // the flat part: tyre (+ faces with no texture)
    GLuint    wheelVao = 0, wheelVbo = 0;
    int32_t   wheelFlatVerts = 0;       // vertices in use in the flat VBO
    bool      wheelsDirty = false;      // local mesh to be redone (new wheels/faces)
    // Each wheel's mesh in its own frame (x forward, y up, z axle, centre at
    // the hub), made at loading only; each frame only wheelModel[i] changes
    // (position, steering and turn), applied in the vertex shader (uModel)
    int32_t   wheelFlatFirst[4] = {}, wheelFlatCount[4] = {};
    int32_t   wheelCapFirst[4] = {},  wheelCapCount[4] = {};
    Mat4      wheelModel[4] = {};
    // Textured faces (rzSetObjectWheelFaces): the discs, with the object's
    // texture; a VBO of its own in the objects' format (GpuVertex).
    bool      wheelFacesSet = false;
    float     wheelFaceOuter[4] = {};   // u0, v0, u1, v1 of the outer face's square
    float     wheelFaceInner[4] = {};   // the same, inner face
    uint8_t   wheelPlusOuter[4] = {};   // 1 = the cap on the +axle side is the outer one (fixed at the 1st build)
    float     wheelUpSign = 1.0f;       // the wheels' "up" direction, fixed at the 1st build (car upright)
    bool      wheelOuterResolved = false;   // wheelUpSign and each wheel's inner/outer already fixed
    std::vector<GpuVertex> wheelCapStaging;
    GLuint    wheelCapVao = 0, wheelCapVbo = 0;
    int32_t   wheelCapVerts = 0;        // vertices in use in the faces' VBO

    GLuint    texture = 0;          // 0: uses the context's fallback texture
    bool      textureGlass = false; // the texture has glass texels
    uint8_t   palette[768];         // RGB of the palette of the texture's PCX (the solid colours)

    int32_t vertexCount() const   { return int32_t(world.size()); }
    int32_t triangleCount() const { return int32_t(triangles.size()); }
    int32_t drawVertexCount() const { return int32_t(staging.size()); }   // polygons + lines
};

// The programs' uniform locations
struct FogUniforms {
    GLint on = -1, start = -1, end = -1, color = -1, eye = -1, origin = -1;
};

struct ShadowUniforms {
    GLint matrices = -1, cascade = -1, onlyFar = -1, offset = -1;
    GLint map = -1;            // uShadow: the array texture
};

struct TerrainProgram {
    GLuint program = 0;
    GLint  viewProj = -1, atlas = -1, textured = -1, shading = -1, ambient = -1;
    GLint  smooth = -1;    // per-pixel light (interpolated normal) instead of flat: wheels
    GLint  model = -1;     // uModel: identity, except in the wheels
    ShadowUniforms shadow;
    FogUniforms    fog;
};

struct ObjectProgram {
    GLuint program = 0;
    GLint  viewProj = -1, texture = -1, ambient = -1;
    GLint  model = -1;     // uModel: identity, except in the wheels
    GLint  fromBuffer = -1; // 1: objects (positions/normals from the block); 0: the wheels' faces
    GLint  texelMode = -1;  // kTexels*: what the shader draws of the texture
    GLint  decalCount = -1, decal = -1, decalColor = -1;   // rzSetObjectDecals
    ShadowUniforms shadow;
    FogUniforms    fog;
};

struct GlassProgram {
    GLuint program = 0;
    GLint  viewProj = -1, light = -1, specular = -1, shininess = -1;
    ShadowUniforms shadow;
    FogUniforms    fog;
};

struct SkyProgram {
    GLuint program = 0;
    GLint  right = -1, up = -1, forward = -1, focal = -1, light = -1, color = -1, time = -1;
};

struct WallProgram {
    GLuint program = 0;
    GLint  viewProj = -1, target = -1, fadeNear = -1, fadeFar = -1, xSize = -1, cellSize = -1;
    GLint  top = -1, topFade = -1;     // height of the top and the fade band (v, tiles)
    FogUniforms fog;
};

struct SpriteProgram {
    GLuint program = 0;
    GLint  viewProj = -1, right = -1, up = -1, noise = -1, time = -1;
    FogUniforms fog;
};

// A sprite in the world (rz_sprites.cpp), converted in rzSetSprites
struct Sprite {
    Vec3     center;
    float    radius;       // world
    float    opacity;      // smoke: multiplier of the noise
    uint32_t color;        // debris: 0x00RRGGBB, with the light already
    uint8_t  type;         // RZ_SPRITE_*
};
struct SpriteKey { float depth; int32_t index; };    // back to front sorting
struct SpriteInstance {    // 28 bytes; one per sprite (the shader opens the quad)
    float    x, y, z, radius;
    float    opacity;         // smoke
    int8_t   frame, type;
    uint8_t  pad[2];
    uint32_t color;           // B, G, R, 0 in bytes
};
static_assert(sizeof(SpriteInstance) == 28);

struct DepthProgram {
    GLuint program = 0;
    GLint  lightViewProj = -1;
    GLint  model = -1;     // uModel: identity, except in the wheels
};


} // namespace rz

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

struct RzContext {
    int32_t   width  = 0;             // the canvas's drawing buffer
    int32_t   height = 0;

    float     fogStart = rz::kFogStartDefault;   // tiles (rzSetFog)
    float     fogEnd   = rz::kFogEndDefault;
    float     backgroundR = rz::kBackgroundR;
    float     backgroundG = rz::kBackgroundG;
    float     backgroundB = rz::kBackgroundB;
    // Background: index in the atlas's palette (rzSetBackgroundColor; -1 = the default colour).
    // The float colour above is redone when the index or the palette changes.
    int32_t   backgroundIndex = -1;
    bool      hasAtlasPalette = false;
    uint8_t   atlasPalette[768];      // RGB of the atlas's PCX (rzLoadTileAtlas)

    rz::TerrainProgram terrainProgram;
    rz::ObjectProgram  objectProgram;
    rz::DepthProgram   depthProgram;
    rz::DepthProgram   objectDepthProgram;   // the objects' shadow (position from the block)
    rz::WallProgram    wallProgram;
    rz::SkyProgram     skyProgram;
    rz::GLuint         skyVao = 0;
    rz::GlassProgram   glassProgram;
    rz::SpriteProgram  spriteProgram;

    // Sprites (rz_sprites.cpp): the array of the last rzSetSprites; vectors
    // reserved at creation (kMaxSprites), with no allocation afterwards
    std::vector<rz::Sprite>       sprites;
    std::vector<rz::SpriteKey>    spriteOrder;
    std::vector<rz::SpriteInstance> spriteStaging;
    uint32_t   spritePalette[256] = {};              // 0x00RRGGBB with the light (atlas or grey)
    rz::GLuint spriteVao = 0, spriteVbo = 0, spriteNoiseTex = 0;
    float      frameStep = 1.0f;                     // this frame: the time since the previous one, in frames of 60 Hz (rzRender)
    float      frameTime = 0.0f;                     // their sum: the time in frames of 60 Hz (noise and smoke)
    rz::Vec3   followLastRaw = { 0.0f, 0.0f, 0.0f }; // the target vertex at the previous frame (the chase's steps)

    // The world's edge (rz_border.cpp), redone with the terrain's mesh
    std::vector<float> extHeights;      // (255 + 2 x 128 + 2)^2, unit of the byte
    rz::GLuint skirtVao = 0, skirtVbo = 0;
    std::vector<rz::TerrainVertex> skirtStaging;    // the continuation's mesh on the CPU (reused)
    int32_t    skirtVertexCount = 0;
    int32_t    skirtFirst[4] = {}, skirtCount[4] = {};   // regions N, E, S, W (drawSkirt)
    rz::GLuint wallVao = 0, wallVbo = 0;
    int32_t    wallVertexCount = 0;

    // Shadows (rz_shadow.cpp): three maps, a depth-only FBO each
    rz::GLuint shadowFbo[rz::kShadowMaps] = {};
    rz::GLuint shadowTex = 0;                         // array texture, one layer per cascade
    rz::Mat4   shadowMatrix[rz::kShadowMaps] = {};     // the frame's; contiguous (glUniformMatrix4fv)
    float      shadowTexel[rz::kShadowMaps] = {};      // size of each map's texel (world)
    int32_t    shadowSize = 0;                       // side of the maps (already limited by the driver)
    int32_t    cascades = rz::kShadowMaps;           // 3, or 1 (lighter): layers and FBOs
    float      shadowReach = rz::kCascadeSplit1;     // tiles: how far cascades 0 and 1 go (with 1, the only one)
    bool       terrainShadowDirty = true;            // new terrain (loading): cascade 2 redone
    bool       cascadeFarValid = false;              // cascade 2 drawn for the current mode
    bool       cascadeFarOverview = false;           // ...in the overview (whole terrain + objects)
    rz::Vec3   cascadeFarCenter = { 0.0f, 0.0f, 0.0f };   // the eye when cascade 2 was redone
    float      cascadeFarHalf = 0.0f;                // its half width (changes with the fog)

    // The frame's (updateCamera): eye and fog
    rz::Vec3   eyePos = { 0.0f, 0.0f, 0.0f };
    rz::Vec3   targetPos = { 0.0f, 0.0f, 0.0f };     // target vertex (boundary wall)
    rz::Vec3   camRight   = { 1.0f, 0.0f, 0.0f };    // the camera's axes (sprites)
    rz::Vec3   camUp      = { 0.0f, 1.0f, 0.0f };
    rz::Vec3   camForward = { 0.0f, 0.0f, -1.0f };
    bool       fogOn = false;                        // there is fog (always, with a camera set up)
    rz::Vec3   fogOrigin = { 0.0f, 0.0f, 0.0f };     // the fog's centre: the eye, or the map's centre
    float      fogNear = 0.0f, fogFar = 0.0f;        // start/end of the fog (world)
    bool       wallOn = false;                       // boundary wall (near the target)
    bool       shadowWholeTerrain = true;           // overview: only cascade 2, whole terrain

    // Projection
    float focalX = 1.0f;     // f / aspect
    float focalY = 1.0f;     // f = 1 / tan(fov/2)
    float fovY   = rz::kFovYDegrees;   // degrees, vertical (rzSetCameraFov)

    // Terrain
    std::vector<uint8_t> heights;      // 256x256, a copy on the CPU (mesh and chase camera)
    float     cellSize    = rz::kDefaultCellSize;
    float     heightScale = rz::kDefaultHeightScale;
    rz::GLuint terrainVao = 0;
    rz::GLuint terrainVbo = 0;         // 130,050 x 3 TerrainVertex in chunks, rebuilt at loading
    std::vector<rz::TerrainVertex> terrainStaging;  // the same mesh on the CPU (rzUpdateTerrain)
    struct TerrainChunk {
        int32_t first = 0, count = 0;  // vertices in the VBO
        float   yMin = 0.0f, yMax = 0.0f;
    };
    TerrainChunk terrainChunks[rz::kChunkCount];    // row-major, chunk (x, z) at z · kChunksPerSide + x
    const uint8_t* heightSource = nullptr;  // the host's buffers (rzSetHeightmap/rzSetTileMap),
    const uint8_t* tileSource   = nullptr;  // read again by rzUpdateTerrain
    bool hasTerrain() const { return !heights.empty(); }

    // Textures (optional; without both, it draws with the flat colours)
    rz::GLuint atlasTex = 0;           // array 16x16 x 256 layers, 5 levels
    std::vector<uint8_t> tileMap;      // 256x256, a copy on the CPU (each quad's block)
    bool      hasAtlas   = false;
    bool      hasTileMap = false;

    // Texture of the objects with no texture of their own (or whose loading
    // failed): the generated chequerboard or rzLoadFallbackTexture's PCX
    rz::GLuint fallbackTex = 0;
    bool       hasFallbackPalette = false;   // false: the generated chequerboard (solid colours: the atlas's palette)
    uint8_t    fallbackPalette[768];         // RGB of the fallback's PCX palette
    bool       fallbackGlass = false;        // the fallback has glass texels

    // Camera
    float terrainRadius = 0.0f;   // R: radius of the terrain's bounding sphere
    float nearPlane = 0.0f;       // worked out each frame from the camera's position
    float farPlane  = 0.0f;

    // The camera's target: a vertex of an object (chase camera), or none
    // (fixed overview of the terrain)
    int32_t cameraTargetObject = -1;
    int32_t cameraTargetVertex = 0;
    int32_t cameraMode = RZ_CAMERA_FOLLOW;          // rzSetCamera

    // Free camera (rzSetCameraFree): in the world, forward unit
    bool     freeValid = false;
    rz::Vec3 freeEye     = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 freeForward = { 0.0f, 0.0f, -1.0f };
    // Discs projected on the objects (rzSetObjectDecals): x, z (world), radius,
    // alpha and each one's colour
    int32_t  decalCount = 0;
    float    decals[RZ_MAX_DECALS * 4] = {};
    float    decalColors[RZ_MAX_DECALS * 3] = {};
    bool     skirtOn = true;                         // rzSetSkirt
    // The view-projection of the last frame (rzGetView)
    rz::Mat4 lastViewProj = {};

    // Internal camera (rzSetCameraInternalAxes): orthonormal basis in the world
    bool     internalValid = false;
    rz::Vec3 internalForward = { 0.0f, 0.0f, -1.0f };
    rz::Vec3 internalUp      = { 0.0f, 1.0f, 0.0f };
    // Smooth entry into the internal one coming from the chase (or the frozen one)
    bool     internalEnterActive = false;
    float    internalEnterT = 0.0f;
    rz::Vec3 internalEye0 = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 internalF0   = { 0.0f, 0.0f, -1.0f };    // the camera's front and up at the start
    rz::Vec3 internalU0   = { 0.0f, 1.0f, 0.0f };

    // Chase camera ("on the rope"); the state moves on at each rzRender
    float    followDistance  = rz::kFollowDistance;
    float    followHeight    = rz::kFollowHeight;
    float    followStiffness = rz::kFollowStiffness;
    bool     followInitialized = false;
    float    followAppliedDistance = 0.0f;   // the rope followEye was worked out with
    rz::Vec3 followEye = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 followLook = { 0.0f, 0.0f, 0.0f };   // smoothed target: pulls the rope (the camera's position)
    rz::Vec3 aimPoint = { 0.0f, 0.0f, 0.0f };     // the bubble's centre: where the camera looks (normal chase)
    bool     aimValid = false;                    // false: the next normal chase starts with the bubble on the target

    // Smooth change of target (rz_camera.cpp): asked for by rzSetCameraTarget,
    // set up at the next frame (when the new target's position is known)
    bool     retargetPending = false;
    bool     retargetActive  = false;
    bool     retargetArc     = false;    // flight in an arc; otherwise it only turns and the rope acts
    float    retargetT       = 0.0f;     // progress 0..1
    float    retargetStep    = 0.0f;     // progress per frame of 60 Hz
    float    retargetLift    = 0.0f;     // extra height in the middle of the arc (world)
    rz::Vec3 retargetEye0  = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 retargetLook0 = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 retargetDir   = { 0.0f, 0.0f, 0.0f };   // horizontal direction of the flight (unit)

    // Into/out of the overview (rz_camera.cpp)
    bool     camHasLast      = false;    // there has been a frame (camLast* count)
    bool     camLastFollowing = false;
    int32_t  camLastMode = RZ_CAMERA_OVERVIEW;   // the previous frame's actual mode
    rz::Vec3 camLastEye  = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 camLastLook = { 0.0f, 0.0f, 0.0f };
    bool     overviewActive  = false;    // transition under way
    bool     overviewToward  = false;    // going to the overview (otherwise leaving it)
    float    overviewT       = 0.0f;
    float    overviewBlend   = 1.0f;     // 0 = the chase's fog/shadows, 1 = the overview's
    float    overviewBlend0  = 1.0f;
    rz::Vec3 overviewEye0  = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 overviewLook0 = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 overviewDir   = { 0.0f, 0.0f, 0.0f };

    // Objects (id = index in the vector; free slots are reused)
    std::vector<rz::Object> objects;

    // Errors gathered since the last rzGetError: the first one (code and
    // function) and how many there were
    int32_t     firstError = RZ_OK;
    const char* firstErrorFunction = nullptr;
    int32_t     errorCount = 0;
};

namespace rz {

// rz_terrain.cpp
void buildPalette(uint32_t* palette);
void updateTerrainBounds(RzContext* ctx);
bool buildTerrainMesh(RzContext* ctx);      // the whole mesh + border + shadow (loading)
bool rebuildTerrainTiles(RzContext* ctx);   // blocks only (rzSetTileMap)
bool updateTerrain(RzContext* ctx);         // rzUpdateTerrain: only what changed
// The terrain's chunks that fall within matrix (projection · view), joining
// the neighbours in the VBO; withFog: also only those that start before the
// end of the fog. Program and uniforms already set.
void drawTerrain(const RzContext* ctx, const Mat4& matrix, bool withFog);
void buildAtlasLevels(uint32_t* tiles, const uint8_t* indices, const uint8_t* paletteRGB);
void downsample(const uint32_t* src, uint32_t* dst, int32_t dstSide);
void fillTransparent(uint32_t* img, int32_t side);   // colour of the alpha 0 texels = opaque neighbours
Vec3 lightDirection();
// A face's normal for the vertex (GL_INT_2_10_10_10_REV normalised: x, y, z
// in 10 signed bits). It need not come normalised; the null one (degenerate
// triangle) becomes "down", which only gets the ambient light, as before.
uint32_t packNormal(Vec3 n);
// TerrainVertex's layout in the bound VAO (terrain, continuation and wheels):
// 0 position, 1 colour, 2 (u, v, block, -), 3 normal
void setTerrainVertexLayout();
// GpuVertex's layout in the bound VAO (objects and the wheels' faces):
// 0 position, 1 normal, 2 (u, v)
void setObjectVertexLayout();

// A whole 8-bit PCX, in memory (common/pcx), with the palette (the textures
// need it). Returns RZ_OK or RZ_ERR_FORMAT.
inline int32_t decodePcx(const uint8_t* file, int32_t size, PcxImage& img) {
    if (size < 0 || !img.decode(file, size_t(size)) || img.palette.size() != 768) return RZ_ERR_FORMAT;
    return RZ_OK;
}

// rz_camera.cpp: moves the camera on a frame and returns view-projection (GL convention);
// also keeps the shadow's focus.
Mat4 updateCamera(RzContext* ctx);
Mat4 lookAt(Vec3 eye, Vec3 at, Vec3 up = { 0.0f, 1.0f, 0.0f });

// rz_shadow.cpp
bool createShadowMaps(RzContext* ctx);
void destroyShadowMaps(RzContext* ctx);
void renderShadowMaps(RzContext* ctx);
void initShadowUniforms(GLuint program, ShadowUniforms& u);
void markTerrainShadowAll(RzContext* ctx);                     // cascade 2 redone at the next frame
void bindShadowMaps(const RzContext* ctx, const ShadowUniforms& u);

// rz_border.cpp
bool  createBorder(RzContext* ctx);
void  destroyBorder(RzContext* ctx);
void  buildBorder(RzContext* ctx);                 // loading, at the end of buildTerrainMesh
void  rebuildSkirtMesh(RzContext* ctx);            // only the continuation's mesh (blocks)
void  moveSkirtEdgeVertex(RzContext* ctx, int32_t gc, int32_t gr);   // a border point changed: only the y
void  setExtendedHeight(RzContext* ctx, int32_t gc, int32_t gr, float height);   // a point of the map
uint8_t skirtTile(const RzContext* ctx, int32_t c, int32_t r);   // block of a cell of the continuation
float extendedGroundHeight(const RzContext* ctx, float x, float z);
void  drawSkirt(const RzContext* ctx, const Mat4& matrix, bool withFog);   // as drawTerrain
void  drawWall(const RzContext* ctx, const Mat4& viewProj);

// rz_wheels.cpp
void freeWheels(Object& o);
void prepareWheels(RzContext* ctx, Object& o);
void drawWheelsDepth(const RzContext* ctx, const Object& o);
void drawWheels(RzContext* ctx, const Mat4& viewProj);
void drawWheelFaces(RzContext* ctx, const Mat4& viewProj);   // discs, with the object's texture

// rz_glass.cpp
bool createGlassProgram(RzContext* ctx);
void freeGlass(Object& o);
void drawGlass(RzContext* ctx, const Mat4& viewProj);

// rz_sprites.cpp
// rz_sky.cpp
bool createSky(RzContext* ctx);
void destroySky(RzContext* ctx);
void drawSky(const RzContext* ctx);

bool createSprites(RzContext* ctx);
void destroySprites(RzContext* ctx);
void setSpritePalette(RzContext* ctx, const uint8_t* paletteRGB);   // nullptr: grey ramp
void drawSprites(RzContext* ctx, const Mat4& viewProj);

// Errors (rzGetError): keeps the first error of an exported function. Used
// in every error return of theirs: return recordError(ctx, "rzX", RZ_ERR_...);
// A filter that goes the fraction k of the way per frame of 60 Hz, in a step
// of `frames` of them
inline float stepFraction(float k, float frames) {
    return 1.0f - powf(1.0f - k, frames);
}

inline int32_t recordError(RzContext* ctx, const char* function, int32_t err) {
    if (err != RZ_OK && ctx) {
        if (ctx->errorCount == 0) {
            ctx->firstError = err;
            ctx->firstErrorFunction = function;
        }
        ++ctx->errorCount;
    }
    return err;
}

// rz_render.cpp
// A program by the names of its shaders in web/shaders.js ("terrain.vert", ...)
GLuint linkProgram(const char* vertexName, const char* fragmentName);

// rz_render.cpp: fog (program already in use)
void initFogUniforms(GLuint program, FogUniforms& u);
void bindFog(const RzContext* ctx, const FogUniforms& u);

// rz_render.cpp
bool createRenderer(RzContext* ctx);
void destroyRenderer(RzContext* ctx);
void renderFrame(RzContext* ctx);
// Mat4 (row by row) to a uniform mat4 (column by column, without the
// transpose WebGL does not have); count up to kShadowMaps
inline void setMatrices(GLint loc, const Mat4* m, int32_t count) {
    float columns[16 * kShadowMaps];
    for (int32_t k = 0; k < count; ++k)
        for (int32_t r = 0; r < 4; ++r)
            for (int32_t c = 0; c < 4; ++c) columns[16 * k + 4 * c + r] = m[k].e[4 * r + c];
    glUniformMatrix4fv(loc, count, GL_FALSE, columns);
}
inline void setMatrix(GLint loc, const Mat4& m) { setMatrices(loc, &m, 1); }

// The textures' RGBA8 texel (in memory R, G, B, A), from 0xRRGGBB and the alpha
inline uint32_t rgbaTexel(uint32_t rgb, uint32_t alpha) {
    return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16) | (alpha << 24);
}

// uTexelMode of the objects' shader: only the opaque ones (a texture with no
// glass: cut at alpha 0.5, as always; with glass: 0.75, the glass is left
// out) or only the glass
constexpr int32_t kTexelsOpaque = 0, kTexelsOpaqueGlass = 1, kTexelsGlass = 2;

// rz_texture.cpp: the objects' square 2D textures (mipmaps on the CPU)
GLuint uploadSquareTexture(const uint32_t* rgba, int32_t side);
void   applyFilter2D(GLuint texture, int32_t side);
bool   createFallbackTexture(RzContext* ctx);
int32_t mipLevels(int32_t side);   // log2(side) + 1

// rz_object.cpp
bool objectInRange(const RzContext* ctx, const Object& o);   // not all of it past the fog
bool objectInView(const RzContext* ctx, const Object& o, const Frustum& f);   // in range and in the frustum
void prepareObjects(RzContext* ctx);          // uploads the changed VBOs again
// shadow pass (program already bound): onlyId >= 0 draws only it; skipId >= 0 skips it
void drawObjectsDepth(RzContext* ctx, const Mat4& lightViewProj, int32_t onlyId, int32_t skipId);
void drawObjects(RzContext* ctx, const Mat4& viewProj);
void drawTexturedGlass(RzContext* ctx, const Mat4& viewProj);   // glass texels, after the opaque
bool objectTextureGlass(const RzContext* ctx, const Object& o);  // the texture it uses has glass
void destroyAllObjects(RzContext* ctx);
void markObjectsDirty(RzContext* ctx);        // the solid colours' palette changed: corners to upload again

} // namespace rz
