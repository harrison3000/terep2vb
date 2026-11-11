/* The game's renderer on the page: WebGL 2 on the canvas (made from
   renderizeitor, github.com/harrison3000/renderizeitor, for the web only). Built together with
   the game into terep2.wasm; the WebGL calls are imports provided by web/webgl.js. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RzContext RzContext;

/* The API's structs: passed by pointer only (never by value), fixed layout,
   fields from the largest to the smallest, explicit padding (zero it). */

/* A vertex's position in tiles (1.0 = 1 tile), the legacy axes: x = column,
   y = row, z = height (up). From the legacy's 8.24: value / 16777216.0f.
   12 bytes. */
typedef struct RzVertex {
    float x, y, z;
} RzVertex;

/* A corner of a textured polygon: the vertex's index and the UV. 12 bytes. */
typedef struct RzTexVertex {
    float    u, v;
    uint16_t index;
    uint16_t pad;
} RzTexVertex;

/* A wheel (see rzSetObjectWheels). 8 bytes. */
typedef struct RzWheel {
    float    diameter;     /* tiles */
    uint16_t hubVertex;    /* the object's vertex at the wheel's centre */
    uint8_t  front;        /* != 0: a front wheel */
    uint8_t  pad;
} RzWheel;

/* A square in the object's texture atlas, by two opposite corners in UV
   (0..1); the picture of a wheel's face (see rzSetObjectWheelFaces). 16 bytes. */
typedef struct RzWheelFace {
    float u0, v0;          /* a corner */
    float u1, v1;          /* the opposite corner */
} RzWheelFace;

/* Sprite types (RzSprite.type) */
#define RZ_SPRITE_DEBRIS  0   /* debris: spray noise in the colour `color` */
#define RZ_SPRITE_SMOKE   1   /* smoke: round, white, soft animated noise */

/* A sprite (see rzSetSprites). 24 bytes. */
typedef struct RzSprite {
    float    x, y, z;      /* centre, in tiles as RzVertex (z up) */
    float    size;         /* diameter in tiles */
    float    opacity;      /* multiplies the alpha (>= 0; 1 = normal) */
    uint8_t  color;        /* index in the game's palette (RZ_SPRITE_DEBRIS only) */
    uint8_t  type;         /* RZ_SPRITE_* */
    uint8_t  pad[2];
} RzSprite;

/* Sizes checked when compiling (C89: an array of negative size) */
typedef char RzAssertVertexSize[sizeof(RzVertex) == 12 ? 1 : -1];
typedef char RzAssertTexVertexSize[sizeof(RzTexVertex) == 12 ? 1 : -1];
typedef char RzAssertWheelSize[sizeof(RzWheel) == 8 ? 1 : -1];
typedef char RzAssertWheelFaceSize[sizeof(RzWheelFace) == 16 ? 1 : -1];
typedef char RzAssertSpriteSize[sizeof(RzSprite) == 24 ? 1 : -1];

/* A disc projected from above on the objects (see rzSetObjectDecals). 32 bytes. */
typedef struct RzDecal {
    float x, y;            /* centre, in tiles as RzVertex (column, row) */
    float radius;          /* tiles */
    float alpha;           /* multiplies the picture (0..1) */
    float r, g, b;         /* the colour, 0..1 */
    float pad;
} RzDecal;
typedef char RzAssertDecalSize[sizeof(RzDecal) == 32 ? 1 : -1];
#define RZ_MAX_DECALS 4

#define RZ_OK               0
#define RZ_ERR_INVALID_ARG  1   /* null pointer or parameter out of range */
#define RZ_ERR_SIZE         2   /* dimension over the limit or not supported */
#define RZ_ERR_NO_MEMORY    3   /* reserved (allocation failure; today it aborts) */
#define RZ_ERR_GL           4   /* WebGL 2 unavailable, or a shader failed */
#define RZ_ERR_FORMAT       6   /* file in an unsupported format, or corrupt */

/* Creates the context on the page's canvas (webgl.js: its WebGL 2); width and
   height: the size of its drawing buffer. The shadows' quality, the GPU's
   heaviest work: shadowSize, the maps' side (a power of 2, 256 to 4096; 0:
   2048), cascades, 3 (up to 8 tiles, up to 30 and up to the end of the fog)
   or 1 (lighter: up to reach tiles, 1 to 60; with 3, reach does not count). rzRender draws to the
   canvas's framebuffer, which the browser shows at the end of the animation
   frame. One context at a time. */
int32_t rzCreate(int32_t width, int32_t height, int32_t shadowSize, int32_t cascades, float reach,
                 RzContext** outCtx);

/* The canvas changed size (the host says so only when it changes): from the
   next rzRender. */
int32_t rzSetSize(RzContext* ctx, int32_t width, int32_t height);

void rzDestroy(RzContext* ctx);

/* Heights: 256x256 bytes, data[row*256 + col]. This version only accepts
   width == height == 256. Copies the data and builds everything (mesh,
   border, the relief's shadow): a loading step, it costs tens of ms. The
   pointer is also kept for rzUpdateTerrain: the buffer has to stay valid
   until the next rzSetHeightmap or rzDestroy. */
int32_t rzSetHeightmap(RzContext* ctx, const uint8_t* data,
                       int32_t width, int32_t height);

/* cellSize: distance between the grid's points; heightScale: height per unit of the byte.
   Default: cellSize = 1, heightScale = 16/255 (byte 255 = 16 tiles; world 255 x 255 x 16). */
int32_t rzSetTerrainScale(RzContext* ctx,
                          float cellSize, float heightScale);

/* Texture atlas: reads an 8-bit PCX file (1 plane, a VGA palette of 256
   colours at the end of the file). The image must be at least 256x256; if it
   is larger, only the top left corner (256x256) is used and the rest ignored.
   The atlas has 16 x 16 blocks of 16x16, numbered left to right and top to
   bottom (block 0 at the top left corner).
   Pixels with the palette's index 255 are fully transparent (the ground has
   a hole there).
   pcx, size: the whole file, in memory (only read during the call).
   Returns: RZ_ERR_FORMAT (not an 8-bit PCX with a palette, or truncated),
   RZ_ERR_SIZE (smaller than 256x256).
   On an error, the previous atlas (if any) stays. */
int32_t rzLoadTileAtlas(RzContext* ctx, const uint8_t* pcx, int32_t size);

/* Block map: 256x256 bytes, data[row*256 + col] = the block of quad (col, row).
   The last row and the last column (255) have no quad and are ignored.
   data == NULL turns the textures off (back to the flat colours). Copies the
   data and redoes the blocks of all the quads (cheaper than rzSetHeightmap:
   it touches neither the relief nor the shadow). The pointer is kept for
   rzUpdateTerrain (the same validity rule as rzSetHeightmap). */
int32_t rzSetTileMap(RzContext* ctx, const uint8_t* data,
                     int32_t width, int32_t height);

/* Quick terrain update while playing: reads again the buffers given to
   rzSetHeightmap and rzSetTileMap (the host changes its own arrays and calls
   this) and redoes only what changed, point by point (made for few changes
   at a time): the 4 quads around each height changed (or the quad of each
   block changed) and, in the relief's shadow, only their area. A height
   change on the map's border (row/column 0 or 255) only moves along the
   vertex of the outside terrain that is on that point (the rest outside is
   not recalculated: for small changes; rzSetHeightmap redoes everything); a
   block less than 8 quads from the border redoes the continuation's mesh.
   Cost: proportional to the number of points changed (comparing the 2 x 64 KB
   is negligible);
   without changes, nothing. Does not allocate. Can be called every frame.
   Errors: RZ_ERR_INVALID_ARG without rzSetHeightmap before. */
int32_t rzUpdateTerrain(RzContext* ctx);

/* Texture filtering (ground and objects): fixed. Magnification (near) nearest;
   minification (far) nearest within the mipmap level and linear blending
   between levels (avoids the "shimmering" of distant polygons). */

/* Background colour (where there is neither terrain nor object; also the
   fog's colour): an index in the atlas's palette (that of rzLoadTileAtlas's
   PCX). Can be called before the atlas: the colour counts as soon as it is
   loaded, and a new atlas changes the colour with it. From the next
   rzRender. Without an index or an atlas: (32, 40, 48). */
int32_t rzSetBackgroundColor(RzContext* ctx, uint8_t paletteIndex);

/* Fog (draw distance), in tiles of 3D distance to the eye: clear up to
   start, gone into the background colour at end; past end nothing is
   drawn. Only when following a target (rzSetCameraTarget). Default 30 and 65.
   Cost: negligible (only keeps the two values; nothing is redone on the CPU
   nor on the GPU), from the next rzRender. Can be called at any time, even
   every frame along the way (e.g. closing the fog on a stretch).
   Errors: RZ_ERR_INVALID_ARG if not 0 <= start < end <= 150. The terrain
   only goes on 128 tiles past the map's border: with the end above 120, its
   end may show (an editor, not the game). */
int32_t rzSetFog(RzContext* ctx, float start, float end);

/* The terrain outside the map (the continuation past the border): drawn or
   not (on == 0: only the map, the rest in the background colour; an editor).
   Default: on. From the next rzRender. */
int32_t rzSetSkirt(RzContext* ctx, int32_t on);

/* ------------------------------------------------------------------------ */
/* Objects                                                                  */
/* ------------------------------------------------------------------------ */

/* Vertices: an array of vertexCount RzVertex, in float, absolute coordinates
   in the world: 1.0 = 1 tile. NaN, infinity or |value| >= 1e6 in any vertex:
   RZ_ERR_INVALID_ARG and nothing changes.
   The legacy axes: (x, y, z) = (column, row, height), z up.

   Building an object (loading step):
     1. rzCreateObject(ctx, vertexCount, &id)       only the number of vertices
     2. rzAddObjectPolygon(ctx, id, indices, n, colour)  once per polygon, or
        rzAddObjectTexturedPolygon(ctx, id, corners, n)  (with UVs)
     3. rzUpdateObjectVertices(ctx, id, vertices)   positions (and every frame)
   Steps 2 and 3 can come in any order and repeat; the object is only drawn
   after the first rzUpdateObjectVertices. */

/* Creates an empty object with vertexCount vertices (1 to 256), with no
   polygons and no positions yet (loading step: allocates). Gives the id in outId. */
int32_t rzCreateObject(RzContext* ctx, int32_t vertexCount, int32_t* outId);

/* Adds a convex polygon of a solid colour to the object (loading step:
   allocates). Can be called as many times as needed; the indices are copied.
   indices: count indices of the object's vertices. If the last one repeats
   the first (closing, as in the legacy: 0 1 2 3 0), it is dropped.
   paletteIndex (0..255): the colour is that entry of the palette of the
   object's texture's PCX (see rzLoadObjectTexture), with the polygon's flat light.
   Triangulated as a fan. With fewer than 3 vertices, it is ignored (RZ_OK).
   Errors: RZ_ERR_INVALID_ARG (index outside the object or paletteIndex out
   of 0..255; nothing is added), RZ_ERR_SIZE (more than 65535 polygons, or
   more than 3072 indices in the whole object). */
int32_t rzAddObjectPolygon(RzContext* ctx, int32_t id,
                           const uint16_t* indices, int32_t count,
                           int32_t paletteIndex);

/* As rzAddObjectPolygon, but with a colour per corner (Gouraud):
   paletteIndices has count indices of the same palette, one for each vertex
   index (the closing one too, dropped with it). The colour is interpolated
   in RGB between the corners, with the same flat light. Errors: those of rzAddObjectPolygon. */
int32_t rzAddObjectShadedPolygon(RzContext* ctx, int32_t id,
                                 const uint16_t* indices, const uint8_t* paletteIndices, int32_t count);

/* As rzAddObjectPolygon, but with its own UVs instead of the colour: corners
   has count corners (the vertex's index + UV). If the last corner repeats
   the first one's index (closing), it is dropped. u and v go from 0 to 1,
   both on the scale of the texture's WIDTH (see rzLoadObjectTexture): v = 1
   is row W, not the image's height. (0, 0) is the top left corner. Outside
   [0, 1], the edge repeats. A UV not finite: RZ_ERR_INVALID_ARG. The
   polygon's colour is the texture times the flat light. */
int32_t rzAddObjectTexturedPolygon(RzContext* ctx, int32_t id,
                                   const RzTexVertex* corners, int32_t count);

/* A translucent polygon (glass) in a shade of grey. tone 0..15: what is
   behind is multiplied by tone/15 (0 = black, 15 = transparent), plus a
   blurred specular highlight of the light (gone in the shadow). Does not
   depend on the drawing order; casts no shadow; only the outside face shows
   (as the other polygons). The same index/closing rules as
   rzAddObjectPolygon; no texture.
   Errors: RZ_ERR_INVALID_ARG (index outside the object or tone out of 0..15). */
int32_t rzAddObjectTranslucentPolygon(RzContext* ctx, int32_t id,
                                      const uint16_t* indices, int32_t count,
                                      int32_t tone);

/* A line between the object's vertices a and b (loading step: allocates),
   shown as a bar of square section of side `thickness` (tiles), in the
   colour paletteIndex (0..255) of the palette of the object's texture, as in
   rzAddObjectPolygon, with light and shadow. Follows rzUpdateObjectVertices.
   a == b is ignored (RZ_OK). Errors: RZ_ERR_INVALID_ARG (vertex outside the
   object, thickness <= 0 or not finite, paletteIndex out of 0..255),
   RZ_ERR_SIZE (more than 65535 lines). */
int32_t rzAddObjectLine(RzContext* ctx, int32_t id, uint16_t a, uint16_t b,
                        float thickness, int32_t paletteIndex);

/* Wheels: thin black cylinders (the faces can be textured with
   rzSetObjectWheelFaces). wheels: 4 RzWheel, each:
   hubVertex  the object's vertex at the wheel's centre (follows
              rzUpdateObjectVertices);
   front      != 0 for a front one (there must be two front ones and two
              rear ones: the car's front and the line of the wheels come
              from that);
   diameter   diameter in tiles.
   Calling again replaces the wheels (and zeroes the steering). Loading step.
   Errors: RZ_ERR_INVALID_ARG (vertex outside the object, diameter <= 0 or
   not finite, or not 2 + 2). */
int32_t rzSetObjectWheels(RzContext* ctx, int32_t id, const RzWheel* wheels);

/* Textures both faces (the discs) of each wheel with the object's texture
   (that of rzLoadObjectTexture): two squares of the atlas, one drawn on the
   OUTER face (the one facing out of the car) and the other on the INNER one.
   Each square is given by two opposite corners in UV (0..1); the face's disc
   is mapped on the circle inscribed in it. The tyre (the cylinder's side)
   stays black. The four wheels use the same pair of faces.
   Needs rzSetObjectWheels before. Calling again replaces the faces; does not allocate.
   Errors: RZ_ERR_INVALID_ARG (no wheels, null pointer or a UV not finite). */
int32_t rzSetObjectWheelFaces(RzContext* ctx, int32_t id,
                              const RzWheelFace* outer, const RzWheelFace* inner);

/* Each wheel's speed (speeds[4], in rzSetObjectWheels's order; tiles per
   frame, = the distance gone in this frame, on the vertices' scale). Every
   frame drawn, each wheel turns by distance / radius, with its own speed and
   diameter; positive = forwards; tiles per frame of 60 Hz (rzRender
   multiplies by the frame's time). It is kept: call once per frame (or when
   it changes). 0 to stop. Does not allocate.
   RZ_ERR_INVALID_ARG without rzSetObjectWheels before, with a null pointer
   or a value not finite. */
int32_t rzSetObjectWheelSpin(RzContext* ctx, int32_t id, const float* speeds);

/* Steering of the two front wheels (radians; positive turns left,
   anticlockwise seen from above); the rear ones stay straight. Does not allocate.
   RZ_ERR_INVALID_ARG without rzSetObjectWheels before or with an angle not finite. */
int32_t rzUpdateObjectWheels(RzContext* ctx, int32_t id, float steer);

/* The object's texture (one per object), read from an 8-bit PCX (loading step).
   The texture is square, W x W: W is the largest power of 2 that fits in the
   image's width (at least 256, at most 4096 or the driver's limit); what is
   left on the right is dropped. Vertically, the image is cut at W rows or
   completed below (repeating the last row).
   Pixels with the palette's index 255 are fully transparent (the solid
   colour 255 of rzAddObjectPolygon too).
   The PCX's palette gives the colours of the solid colour polygons
   (rzAddObjectPolygon) and of the object's lines.
   pcx, size: the whole file, in memory, as in rzLoadTileAtlas.
   With no texture loaded, or if this function fails (RZ_ERR_FORMAT,
   RZ_ERR_SIZE for a width < 256), the object uses the fallback texture and
   its palette (that of the generated magenta chequerboard: the atlas's).
   Calling again replaces the texture. */
int32_t rzLoadObjectTexture(RzContext* ctx, int32_t id, const uint8_t* pcx, int32_t size);

/* Replaces the fallback texture (that of the objects with no texture or
   whose loading failed), with the same rules as rzLoadObjectTexture. Counts
   for all objects, including those already made. On an error, the current
   fallback stays. pcx == NULL goes back to the magenta chequerboard
   generated by the library. */
int32_t rzLoadFallbackTexture(RzContext* ctx, const uint8_t* pcx, int32_t size);

/* The positions of all the vertices (vertexCount RzVertex; the same layout
   as an array of vertexCount * 3 uint32_t). Copies and recalculates the
   lighting; does not allocate. */
int32_t rzUpdateObjectVertices(RzContext* ctx, int32_t id,
                               const RzVertex* vertices);

/* Frees the object; the id can be reused by rzCreateObject. */
int32_t rzDestroyObject(RzContext* ctx, int32_t id);

/* ------------------------------------------------------------------------ */
/* Sprites (smoke, debris, splashing water)                                 */
/* ------------------------------------------------------------------------ */

/* Replaces all the sprites with the count of `sprites` (copied; 0 clears).
   No ids: each call is the whole array, in whatever order (the legacy may
   compact the array when a sprite dies). Each one becomes a quad facing the
   camera, of size `size`, by its type:
     RZ_SPRITE_DEBRIS  a semi-transparent circle with a more opaque spray
                       noise over it (8 variations that alternate by
                       themselves every few frames) in the colour `color`
                       of the game's palette (that of rzLoadTileAtlas's
                       PCX; before it, grey = index); alpha x opacity.
     RZ_SPRITE_SMOKE   a semi-transparent white disc with low frequency
                       noise that changes by itself with time; alpha =
                       noise x opacity (with a soft edge); `color` is ignored.
   Translucent, sorted by the renderer; with fog; no shadow.
   Cost: low, does not allocate; made to be called every frame.
   Errors (nothing changes): RZ_ERR_INVALID_ARG (sprites null with count > 0,
   count < 0, size <= 0 or not finite, unknown type, opacity < 0 or not
   finite), RZ_ERR_SIZE (count > 256). */
int32_t rzSetSprites(RzContext* ctx, const RzSprite* sprites, int32_t count);


/* Discs projected from above onto the objects (the host draws the same ones
   on the ground: the page's checkpoints): on every point of an object inside
   the circle, vertically, the disc's picture over the colour, translucent in
   the middle, a band of the colour and the white rim. Replaces the previous
   ones (0 removes them all); up to RZ_MAX_DECALS. Cheap, can be called every
   frame. Errors (nothing changes): RZ_ERR_INVALID_ARG (null with count > 0,
   count < 0, radius <= 0 or values not finite), RZ_ERR_SIZE (count >
   RZ_MAX_DECALS). */
int32_t rzSetObjectDecals(RzContext* ctx, const RzDecal* decals, int32_t count);


/* Faces: those that appear anticlockwise on the screen are dropped (the
   legacy's convention: seen from outside, the face is clockwise). */


/* ------------------------------------------------------------------------ */

/* Camera modes (rzSetCamera). All but the overview use the reference vertex
   of rzSetCameraTarget; without a target, any mode falls back to the overview. */
#define RZ_CAMERA_FOLLOW         0   /* chase on the rope (rzSetCameraFollow); default */
#define RZ_CAMERA_INTERNAL       1   /* at the reference vertex, oriented by
                                        rzSetCameraInternalAxes */
#define RZ_CAMERA_FOLLOW_FREEZE  2   /* the chase position freezes where it is,
                                        but goes on looking at the target */
#define RZ_CAMERA_OVERVIEW       3   /* fixed overview of the whole terrain */
#define RZ_CAMERA_FREE           4   /* free, wherever the host says (rzSetCameraFree);
                                        needs no target */

/* Chooses the camera's mode (RZ_CAMERA_*). Between chase, frozen and
   overview the camera makes its usual smooth transition; the internal one
   comes in and goes out with a hard cut (leaving it, the chase starts again
   behind the target, by the front of rzSetCameraInternalAxes). Errors:
   RZ_ERR_INVALID_ARG (unknown mode). */
int32_t rzSetCamera(RzContext* ctx, int32_t mode);

/* The camera's reference vertex: vertex `vertex` of object `id`, following
   rzUpdateObjectVertices; it need not be part of any polygon. The chase
   looks at it; the internal camera is on it.
   id < 0 (the default) or a destroyed object: no target (overview). */
int32_t rzSetCameraTarget(RzContext* ctx, int32_t id, int32_t vertex);

/* The internal camera's orientation: two axes in the legacy axes (as
   RzVertex: x column, y row, z up), `forward` where it looks and `up` the
   picture's up (the right comes from both, so the camera tilts with the
   object). They need be neither unit nor exactly perpendicular (up is
   adjusted). Made to be called every frame, together with
   rzUpdateObjectVertices. Errors: RZ_ERR_INVALID_ARG (null, not finite,
   zero length or parallel). */
int32_t rzSetCameraInternalAxes(RzContext* ctx, const RzVertex* forward,
                                const RzVertex* up);

/* Free camera (RZ_CAMERA_FREE): the eye at eye looking along forward, in
   the legacy axes (as RzVertex: x column, y row, z up, in tiles), with the
   picture's up upwards (forward cannot be vertical). A hard cut coming in;
   going out, the other cameras fly from where it was, but the frozen one
   (RZ_CAMERA_FOLLOW_FREEZE), which stays there. The fog and the boundary wall are around the eye. Made
   to be called every frame. Errors: RZ_ERR_INVALID_ARG (null, not finite,
   forward zero or vertical). */
int32_t rzSetCameraFree(RzContext* ctx, const RzVertex* eye, const RzVertex* forward);

/* Vertical field of view, in degrees, from 10 to 120 (default 50); the
   horizontal one comes from the picture's proportion. Counts for all camera
   modes, from the next rzRender. Cheap: can be called every frame (e.g.
   zoom). A wide FOV makes the shadow near the camera a little less sharp
   (the nearest cascade covers more area). Errors: RZ_ERR_INVALID_ARG (out of range or NaN). */
int32_t rzSetCameraFov(RzContext* ctx, float degrees);

/* The chase camera, as if pulled by a rope:
   distance  the rope's length, in tiles (default 3): with the target further
             than that, the camera is pulled; nearer than half of it, it
             backs away;
   height    the height wanted over the target, in tiles (default 1);
   stiffness the fraction of the way to the wanted position gone each frame
             of 60 Hz, in (0, 1] (default 0.08): smaller = smoother and
             later. */
int32_t rzSetCameraFollow(RzContext* ctx, float distance, float height,
                          float stiffness);

/* The camera of the last rzRender, for the host to draw over it on the same
   canvas (the frame's depth buffer stays there until the browser shows it).
   The renderer's world coordinates: (x, y, z) = (column, height, row) in
   tiles (with cellSize 1; the terrain's height = byte x heightScale). out
   receives RZ_VIEW_FLOATS floats:
     0..15   view-projection, by columns (as glUniformMatrix4fv without transposing)
     16..18  the eye's position   19..21  where it looks (unit)
     22      fog on (0/1)         23..25  the fog's origin
     26, 27  the fog's start and end (3D distance to the origin)
     28..30  background / fog colour, RGB 0..1
   Before the first rzRender, all 0. */
#define RZ_VIEW_FLOATS 32
int32_t rzGetView(const RzContext* ctx, float* out);

/* Moves the camera on and renders the frame on the canvas. Does not
   allocate. frames: the time since the previous rzRender, in frames of 60
   Hz (1 at 60 fps, 2 at 30...): the camera, the wheels and the sprites move
   by time, not by call, and look the same at any frame rate. Capped at 15 (a
   stall does not become a bigger jump). */
int32_t rzRender(RzContext* ctx, float frames);

/* Error checking in bulk: all the functions that return a code keep the
   first error in the context. Everything can be set up without looking at
   the returns and checked once at the end:

       rzCreateWindow(window, &ctx);
       rzSetHeightmap(ctx, ...);  rzLoadTileAtlas(ctx, ...);  ...
       const char* where;
       if (rzGetError(ctx, &where) != RZ_OK) { ... where = "rzLoadTileAtlas" ... }

   Returns the first error since the last call (RZ_OK if nothing failed) and
   clears the record. outFunction (may be NULL) receives the name of the
   function that failed first (a static string), or NULL.
   ctx NULL (rzCreate/rzCreateWindow failed): returns the creation's error,
   or RZ_ERR_INVALID_ARG if no creation failure was recorded.
   Calls with ctx NULL have nowhere to record: they only return the error. */
int32_t rzGetError(RzContext* ctx, const char** outFunction);

#ifdef __cplusplus
}
#endif

