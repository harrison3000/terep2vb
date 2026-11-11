// The OpenGL renderer's shaders (GLSL ES 3.00, WebGL 2), by name: the
// renderer (host_web/rz) asks for them by these names, with its #defines
// (CASCADES: 3 shadow cascades, or 1 in medium quality), and webgl.js puts
// the "#version 300 es" line and the defines before the text.
// Comments as the renderer's.

export const shaders = {

// Terrain: mesh built on the CPU at loading (rz_terrain.cpp), a vertex per
// triangle corner, with the triangle's colour (no light), (u, v, block) and
// the face's normal. Flat light: ambient + diffuse by the normal, per triangle
// (the three vertices have the same normal; `flat` takes the last one's).
'terrain.vert': `layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;      // B, G, R, 0 normalised
layout(location = 2) in vec4 aUvLayer;    // u, v in {0, 1}; block 0..255; -
layout(location = 3) in vec4 aNormal;     // the face's normal (10 bits per axis)
uniform mat4 uViewProj;
uniform mat4 uModel;                      // identity; wheels: each wheel's position/turn
uniform mat4 uShadowMatrix[3];            // light: cascades 0, 1 and 2
uniform vec3 uShadowOffset;               // normal offset of each cascade (world)
uniform vec3  uLight;                     // direction to the light (normalised)
uniform float uAmbient;

flat out vec3  vColor;                    // the triangle's base colour, WITHOUT light
flat out int   vLayer;
flat out float vLight;                    // the triangle's flat light, 0..1
out float vLightSmooth;                   // the same light, interpolated (normals per vertex): wheels
out vec2 vUV;
out vec3 vShadow0;
#if CASCADES == 3
out vec3 vShadow1, vShadow2;
#endif
out vec3 vWorld;                          // for the fog
out float vNdotL;                         // normal · light (shadow at the terminator)

void main() {
    vec3 n = normalize(mat3(uModel) * aNormal.xyz);
    vNdotL = dot(n, uLight);
    vLight = uAmbient + (1.0 - uAmbient) * max(vNdotL, 0.0);
    vLightSmooth = vLight;
    vColor = aColor.bgr;
    vUV    = aUvLayer.xy;
    vLayer = int(aUvLayer.z);
    vec4 world = uModel * vec4(aPosition, 1.0);
    // Normal offset: reads the shadow map at a point pushed along the normal, the
    // more the lower the light (the acne goes without the shadow coming loose)
    vec3 push = n * clamp(1.0 - vNdotL, 0.0, 1.0);
    vShadow0 = (uShadowMatrix[0] * (world + vec4(push * uShadowOffset.x, 0.0))).xyz * 0.5 + 0.5;
#if CASCADES == 3
    vShadow1 = (uShadowMatrix[1] * (world + vec4(push * uShadowOffset.y, 0.0))).xyz * 0.5 + 0.5;
    vShadow2 = (uShadowMatrix[2] * (world + vec4(push * uShadowOffset.z, 0.0))).xyz * 0.5 + 0.5;
#endif
    vWorld = world.xyz;
    gl_Position = uViewProj * world;
}
`,

// Fixed filter, sampler state only: nearest within the mipmap level and
// linear blending between levels (GL_NEAREST_MIPMAP_LINEAR); textureGrad with the
// derivatives taken before the fog's branch.
// With a texture, the colour is multiplied by a light shading (the triangle's
// flat light toned down by uShading) and, in the shadow, by uShadowDim. In the flat
// colours, which come lit already, the triangle's light drops to uShadowLight.
// The output has alpha 0 (the canvas is opaque: alpha false in webgl.js).
'terrain.frag': `precision highp float;
precision highp int;
precision highp sampler2DArray;
precision highp sampler2DArrayShadow;
flat in vec3  vColor;                     // base colour, no light
flat in int   vLayer;
flat in float vLight;
in float vLightSmooth;
in vec2 vUV;
in vec3 vShadow0;
#if CASCADES == 3
in vec3 vShadow1, vShadow2;
#endif
in vec3 vWorld;
in float vNdotL;

// Terminator: with the light grazing or from behind (n · l near 0 or negative)
// the face goes into the shadow for good, instead of alternating light/shadow by
// the map's resolution; the direct light there is almost zero, so the change is smooth.
uniform float uShadowTerminator;

// Fog: smoothstep of the distance to uFogOrigin between uFogStart and uFogEnd, in
// the colour uFogColor (the background's).
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogColor;
uniform vec3  uEye;
uniform vec3  uFogOrigin;   // eye (following) or the map's centre (overview)

uniform sampler2DArray  uAtlas;
uniform sampler2DArrayShadow uShadow;     // the three cascades, one per layer
uniform vec3  uCascade;                   // end of cascade 0, end of 1, transition band (world)
uniform int   uCascadeOnlyFar;            // overview: only cascade 2
uniform int   uTextured;
uniform int   uSmooth;                    // 1: per-pixel light (vLightSmooth), for the wheels
uniform float uShading;
uniform float uShadowLight;               // light in the shadow (kShadowLight)
uniform float uShadowDim;                 // textured ground in the shadow: a fixed factor

out vec4 fragColor;

// Shadow: 1 lit, 0 in the shadow. Outside a map's box, it does not shade.
// sampler2DArrayShadow with GL_LINEAR: the hardware's 2x2 PCF. textureGrad with
// zero derivatives (level 0): the map has no mipmap and the read is inside
// an if (no derivatives; GLSL ES has no textureLod for array shadow).
float shadowLit(float cascade, vec3 c) {
    if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
    return textureGrad(uShadow, vec4(c.xy, cascade, c.z), vec2(0.0), vec2(0.0));   // level 0
}

#if CASCADES == 1
// A single cascade (medium quality): up to uCascade.y, fading over the band of
// uCascade.z before it; the overview uses the same layer.
float shadowTerm() {
    if (uCascadeOnlyFar != 0) return shadowLit(0.0, vShadow0);
    float d = length(vWorld - uEye);
    float b = uCascade.z;
    if (d < uCascade.y - b) return shadowLit(0.0, vShadow0);
    if (d < uCascade.y) return mix(shadowLit(0.0, vShadow0), 1.0, (d - (uCascade.y - b)) / b);
    return 1.0;
}
#else
// Cascades (rz_shadow.cpp): picks by the distance to the eye; in a band of
// uCascade.z before each split, blends the two neighbours. Overview: only 2.
float shadowTerm() {
    if (uCascadeOnlyFar != 0) return shadowLit(2.0, vShadow2);
    float d = length(vWorld - uEye);
    float b = uCascade.z;
    if (d < uCascade.x - b) return shadowLit(0.0, vShadow0);
    if (d < uCascade.x)
        return mix(shadowLit(0.0, vShadow0), shadowLit(1.0, vShadow1), (d - (uCascade.x - b)) / b);
    if (d < uCascade.y - b) return shadowLit(1.0, vShadow1);
    if (d < uCascade.y)
        return mix(shadowLit(1.0, vShadow1), shadowLit(2.0, vShadow2), (d - (uCascade.y - b)) / b);
    return shadowLit(2.0, vShadow2);
}
#endif

// Fog factor (0 clear, 1 only fog)
float fogFactor() {
    if (uFogOn == 0) return 0.0;
    return smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin));
}


void main() {
    // Derivatives before any branch: the fog leaves early in some of the pixels
    vec2 dUVx = dFdx(vUV), dUVy = dFdy(vUV);
    float fog = fogFactor();
    if (fog >= 1.0) {                        // only fog: neither texture nor shadow
        fragColor = vec4(uFogColor, 0.0);
        return;
    }
    float lit   = shadowTerm() * smoothstep(0.0, uShadowTerminator, vNdotL);
    float base  = (uSmooth != 0) ? vLightSmooth : vLight;   // per pixel in the wheels, flat elsewhere
    float light = mix(min(uShadowLight, base), base, lit);
    vec3 color;
    if (uTextured == 0) {
        color = vColor * light;                             // vColor is the base colour already, no light
        fragColor = vec4(mix(color, uFogColor, fog), 0.0);
        return;
    }
    vec3 uvw = vec3(vUV, float(vLayer));
    vec4 tex = textureGrad(uAtlas, uvw, dUVx, dUVy);         // nearest in the level, linear between levels
    if (tex.a < 0.5) discard;                                // the PCX's index 255: transparent
    vec3 texel = tex.rgb;
    color = texel * mix(1.0, vLight, uShading) * mix(uShadowDim, 1.0, lit);
    fragColor = vec4(mix(color, uFogColor, fog), 0.0);
}
`,

// Objects: position in the world, the polygon's outward normal (the same in all
// its triangles, even if not planar), UV and colour. Flat light as in the terrain,
// on the texture or on the vertex's solid colour.
'object.vert': `layout(location = 0) in vec3 aPosition;   // lines and the wheels' faces: position directly
layout(location = 1) in vec4 aNormal;     // the same, normal (10 bits per axis)
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec3 aPolygon;   // objects: vertex (-1: uses aPosition/aNormal),
                                          // the polygon's start and length in uIndices
layout(location = 4) in vec4 aColor;      // solid colour and, in the alpha, the corner's kind (kCorner*)
uniform mat4 uViewProj;
uniform mat4 uModel;                      // identity; wheels: each wheel's position/turn
uniform int  uFromBuffer;                 // 1: objects (positions in uPositions); 0: the wheels' faces
// The object's block (rz_internal.h: kMaxObjectVertices, kMaxObjectIndices)
layout(std140) uniform ObjectData {
    vec4  uPositions[256];                // positions in the world of the object's vertices
    ivec4 uIndices[768];                  // the polygons' indices (without the closings), four per ivec4
};
uniform mat4 uShadowMatrix[3];            // light: cascades 0, 1 and 2
uniform vec3 uShadowOffset;               // normal offset of each cascade (world)
uniform vec3  uLight;                     // direction to the light (normalised)
uniform float uAmbient;
flat out float vLight;
out vec4 vColor;                          // interpolated: the shaded polygons (the others: the same at the corners)
flat out vec3 vNormal;                    // outward normal (the glass texels' highlight)
out vec2 vUV;
out vec3 vShadow0;
#if CASCADES == 3
out vec3 vShadow1, vShadow2;
#endif
out vec3 vWorld;                          // for the fog
out float vNdotL;                         // normal · light (shadow at the terminator)

vec3 vertexAt(int i) { return uPositions[i].xyz; }
int indexAt(int i) { return uIndices[i / 4][i % 4]; }

// The polygon's outward normal (Newell): a single one for the whole polygon,
// even if not planar. In the legacy's direction (clockwise seen from outside),
// Newell's points in; the outward one is the opposite. Degenerate: down (as packNormal).
vec3 polygonNormal(int first, int count) {
    vec3 n = vec3(0.0);
    vec3 cur = vertexAt(indexAt(first + count - 1));
    for (int k = 0; k < count; ++k) {
        vec3 next = vertexAt(indexAt(first + k));
        n += vec3((next.y - cur.y) * (next.z + cur.z),
                  (next.z - cur.z) * (next.x + cur.x),
                  (next.x - cur.x) * (next.y + cur.y));
        cur = next;
    }
    return dot(n, n) > 1.0e-20 ? n : vec3(0.0, -1.0, 0.0);
}

void main() {
    vec3 position = aPosition;
    vec3 normal = aNormal.xyz;
    if (uFromBuffer != 0 && aPolygon.x >= 0) {
        position = vertexAt(aPolygon.x);
        normal = polygonNormal(aPolygon.y, aPolygon.z);
    }
    vec3 n = normalize(mat3(uModel) * normal);
    vNdotL = dot(n, uLight);
    vLight = uAmbient + (1.0 - uAmbient) * max(vNdotL, 0.0);
    vNormal = n;
    vUV = aUV;
    vColor = aColor;
    vec4 world = uModel * vec4(position, 1.0);
    // Normal offset: reads the shadow map at a point pushed along the normal, the
    // more the lower the light (the acne goes without the shadow coming loose)
    vec3 push = n * clamp(1.0 - vNdotL, 0.0, 1.0);
    vShadow0 = (uShadowMatrix[0] * (world + vec4(push * uShadowOffset.x, 0.0))).xyz * 0.5 + 0.5;
#if CASCADES == 3
    vShadow1 = (uShadowMatrix[1] * (world + vec4(push * uShadowOffset.y, 0.0))).xyz * 0.5 + 0.5;
    vShadow2 = (uShadowMatrix[2] * (world + vec4(push * uShadowOffset.z, 0.0))).xyz * 0.5 + 0.5;
#endif
    vWorld = world.xyz;
    gl_Position = uViewProj * world;
}
`,

// The same filter as the ground's (sampler state).
'object.frag': `precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArrayShadow;
flat in float vLight;
in vec4 vColor;                           // solid colour (shaded: interpolated in RGB); alpha: 0 textured,
                                          // 0.5 hole, 1 solid colour
flat in vec3 vNormal;
in vec2 vUV;
in vec3 vShadow0;
#if CASCADES == 3
in vec3 vShadow1, vShadow2;
#endif
in vec3 vWorld;
in float vNdotL;

// Terminator: with the light grazing or from behind (n · l near 0 or negative)
// the face goes into the shadow for good, instead of alternating light/shadow by
// the map's resolution; the direct light there is almost zero, so the change is smooth.
uniform float uShadowTerminator;

// Fog: smoothstep of the distance to uFogOrigin between uFogStart and uFogEnd, in
// the colour uFogColor (the background's).
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogColor;
uniform vec3  uEye;
uniform vec3  uFogOrigin;   // eye (following) or the map's centre (overview)

uniform sampler2D       uTexture;
uniform int   uTexelMode;                 // 0 opaque (cut at 0.5), 1 opaque without the glass, 2 the glass only
uniform vec3  uLight;                     // glass: direction to the light and the highlight (as glass.frag)
uniform float uSpecular, uShininess;
uniform sampler2DArrayShadow uShadow;     // the three cascades, one per layer
uniform vec3  uCascade;                   // end of cascade 0, end of 1, transition band (world)
uniform int   uCascadeOnlyFar;            // overview: only cascade 2
uniform float uShadowLight;               // light in the shadow (kShadowLight)
uniform int   uDecalCount;                // discs projected from above (rzSetObjectDecals)
uniform vec4  uDecal[4];                  // x, z (world), radius, alpha
uniform vec3  uDecalColor[4];

out vec4 fragColor;

// The discs projected over the colour: the picture of the page's checkpoints
// (web/checkpoints.js): translucent in the middle, a band of the colour and the white rim.
vec3 decals(vec3 color) {
    for (int i = 0; i < 4; ++i) {           // RZ_MAX_DECALS
        if (i >= uDecalCount) break;
        float r = length(vWorld.xz - uDecal[i].xy) / uDecal[i].z;
        if (r > 1.02) continue;
        const float aa = 0.02;                // no derivatives inside the loop
        float band = smoothstep(0.78 - aa, 0.78 + aa, r);
        float rim  = smoothstep(0.92 - aa, 0.92 + aa, r);
        float edge = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);
        vec3  c = mix(mix(uDecalColor[i] * 0.7, uDecalColor[i], band), vec3(1.0), rim);
        float a = mix(mix(0.22, 0.85, band), 0.90, rim) * edge * uDecal[i].w;
        color = mix(color, c, a);
    }
    return color;
}

// Shadow: 1 lit, 0 in the shadow. Outside a map's box, it does not shade.
// sampler2DArrayShadow with GL_LINEAR: the hardware's 2x2 PCF. textureGrad with
// zero derivatives (level 0): the map has no mipmap and the read is inside
// an if (no derivatives; GLSL ES has no textureLod for array shadow).
float shadowLit(float cascade, vec3 c) {
    if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
    return textureGrad(uShadow, vec4(c.xy, cascade, c.z), vec2(0.0), vec2(0.0));   // level 0
}

#if CASCADES == 1
// A single cascade (medium quality): up to uCascade.y, fading over the band of
// uCascade.z before it; the overview uses the same layer.
float shadowTerm() {
    if (uCascadeOnlyFar != 0) return shadowLit(0.0, vShadow0);
    float d = length(vWorld - uEye);
    float b = uCascade.z;
    if (d < uCascade.y - b) return shadowLit(0.0, vShadow0);
    if (d < uCascade.y) return mix(shadowLit(0.0, vShadow0), 1.0, (d - (uCascade.y - b)) / b);
    return 1.0;
}
#else
// Cascades (rz_shadow.cpp): picks by the distance to the eye; in a band of
// uCascade.z before each split, blends the two neighbours. Overview: only 2.
float shadowTerm() {
    if (uCascadeOnlyFar != 0) return shadowLit(2.0, vShadow2);
    float d = length(vWorld - uEye);
    float b = uCascade.z;
    if (d < uCascade.x - b) return shadowLit(0.0, vShadow0);
    if (d < uCascade.x)
        return mix(shadowLit(0.0, vShadow0), shadowLit(1.0, vShadow1), (d - (uCascade.x - b)) / b);
    if (d < uCascade.y - b) return shadowLit(1.0, vShadow1);
    if (d < uCascade.y)
        return mix(shadowLit(1.0, vShadow1), shadowLit(2.0, vShadow2), (d - (uCascade.y - b)) / b);
    return shadowLit(2.0, vShadow2);
}
#endif

// Fog factor (0 clear, 1 only fog)
float fogFactor() {
    if (uFogOn == 0) return 0.0;
    return smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin));
}


void main() {
    // Derivatives before any branch: the fog leaves early in some of the pixels
    vec2 dUVx = dFdx(vUV), dUVy = dFdy(vUV);
    float fog = fogFactor();
    if (uTexelMode == 2) {
        // Glass texels (rz_texture.cpp: alpha 0.5, rgb = grey): the filter and
        // the highlight, as glass.frag, with the glass's blending (rz_glass.cpp)
        if (vColor.a >= 0.25 || fog >= 1.0) discard;
        vec4 tex = textureGrad(uTexture, vUV, dUVx, dUVy);
        if (tex.a < 0.25 || tex.a >= 0.75) discard;
        float spec = 0.0;
        if (vNdotL > 0.0) {
            vec3 h = normalize(uLight + normalize(uEye - vWorld));
            spec = pow(max(dot(vNormal, h), 0.0), uShininess) * uSpecular * shadowTerm();
        }
        fragColor = vec4(vec3(spec * (1.0 - fog)), mix(tex.r, 1.0, fog));
        return;
    }
    if (fog >= 1.0) {
        fragColor = vec4(uFogColor, 0.0);
        return;
    }
    vec3 texel;
    if (vColor.a < 0.25) {                                   // textured
        vec4 tex = textureGrad(uTexture, vUV, dUVx, dUVy);   // nearest in the level, linear between levels
        if (tex.a < (uTexelMode == 1 ? 0.75 : 0.5)) discard; // the PCX's index 255 (and the glass)
        texel = tex.rgb;
    } else if (vColor.a < 0.75) {
        discard;                                             // the solid colour 255: transparent
    } else {
        texel = vColor.rgb;                                  // solid colour: the same light as the texel's
    }
    float lit = shadowTerm() * smoothstep(0.0, uShadowTerminator, vNdotL);
    vec3 color = decals(texel * mix(min(uShadowLight, vLight), vLight, lit));
    fragColor = vec4(mix(color, uFogColor, fog), 0.0);
}
`,

// Glass (rz_glass.cpp): output rgb = specular highlight, a = the filter's grey;
// blending GL_ONE, GL_SRC_ALPHA → destination = highlight + destination x grey.
'glass.vert': `layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;     // outward, per polygon
layout(location = 2) in float aTint;
uniform mat4 uViewProj;
uniform mat4 uShadowMatrix[3];
flat out vec3  vNormal;
flat out float vTint;
out vec3 vWorld;
out vec3 vShadow0;
#if CASCADES == 3
out vec3 vShadow1, vShadow2;
#endif
void main() {
    vec4 world = vec4(aPosition, 1.0);
    vNormal = aNormal;
    vTint = aTint;
    vWorld = aPosition;
    vShadow0 = (uShadowMatrix[0] * world).xyz * 0.5 + 0.5;
#if CASCADES == 3
    vShadow1 = (uShadowMatrix[1] * world).xyz * 0.5 + 0.5;
    vShadow2 = (uShadowMatrix[2] * world).xyz * 0.5 + 0.5;
#endif
    gl_Position = uViewProj * world;
}
`,
'glass.frag': `precision highp float;
precision highp int;
precision highp sampler2DArrayShadow;
flat in vec3  vNormal;
flat in float vTint;
in vec3 vWorld;
in vec3 vShadow0;
#if CASCADES == 3
in vec3 vShadow1, vShadow2;
#endif
uniform sampler2DArrayShadow uShadow;     // the three cascades, one per layer
uniform vec3  uCascade;
uniform int   uCascadeOnlyFar;
uniform vec3  uLight;                     // direction to the light (normalised)
uniform float uSpecular, uShininess;
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogColor;
uniform vec3  uEye;
uniform vec3  uFogOrigin;   // eye (following) or the map's centre (overview)
out vec4 fragColor;

float shadowLit(float cascade, vec3 c) {
    if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
    return textureGrad(uShadow, vec4(c.xy, cascade, c.z), vec2(0.0), vec2(0.0));   // level 0
}

#if CASCADES == 1
// A single cascade (medium quality): up to uCascade.y, fading over the band of
// uCascade.z before it; the overview uses the same layer.
float shadowTerm() {
    if (uCascadeOnlyFar != 0) return shadowLit(0.0, vShadow0);
    float d = length(vWorld - uEye);
    float b = uCascade.z;
    if (d < uCascade.y - b) return shadowLit(0.0, vShadow0);
    if (d < uCascade.y) return mix(shadowLit(0.0, vShadow0), 1.0, (d - (uCascade.y - b)) / b);
    return 1.0;
}
#else
// Cascades (rz_shadow.cpp): picks by the distance to the eye; in a band of
// uCascade.z before each split, blends the two neighbours. Overview: only 2.
float shadowTerm() {
    if (uCascadeOnlyFar != 0) return shadowLit(2.0, vShadow2);
    float d = length(vWorld - uEye);
    float b = uCascade.z;
    if (d < uCascade.x - b) return shadowLit(0.0, vShadow0);
    if (d < uCascade.x)
        return mix(shadowLit(0.0, vShadow0), shadowLit(1.0, vShadow1), (d - (uCascade.x - b)) / b);
    if (d < uCascade.y - b) return shadowLit(1.0, vShadow1);
    if (d < uCascade.y)
        return mix(shadowLit(1.0, vShadow1), shadowLit(2.0, vShadow2), (d - (uCascade.y - b)) / b);
    return shadowLit(2.0, vShadow2);
}
#endif

void main() {
    float fog = uFogOn != 0 ? smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin)) : 0.0;
    if (fog >= 1.0) discard;
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uEye - vWorld);
    float ndl = dot(n, uLight);
    float spec = 0.0;
    if (ndl > 0.0) {
        float lit = shadowTerm();
        vec3 h = normalize(uLight + v);
        spec = pow(max(dot(n, h), 0.0), uShininess) * uSpecular * lit;
    }
    spec *= 1.0 - fog;
    fragColor = vec4(vec3(spec), mix(vTint, 1.0, fog));
}
`,

// Sprites (rz_sprites.cpp): a quad facing the camera, opened in the vertex
// shader from the centre, the radius and the corner (-1/+1); fog.
//   Debris (type 0): alpha from the noise texture (array, one layer per picture);
//     colour with the light already.
//   Smoke (type 1): a white disc, alpha = 3D value noise (position in the disc
//     + centre in the world, time) x opacity, with the edge smoothed.
'sprite.vert': `// One sprite per instance (rz_sprites.cpp): the quad comes from gl_VertexID, six
// vertices, two triangles
layout(location = 0) in vec4 aCenterRadius;
layout(location = 1) in ivec2 aFrameType;   // the noise's picture, type
layout(location = 2) in vec4 aColor;        // B, G, R, 0 normalised
layout(location = 3) in float aOpacity;     // smoke: multiplier of the noise
uniform mat4 uViewProj;
uniform vec3 uRight, uUp;
out vec3 vUV;
flat out vec3 vColor;
flat out float vOpacity;
flat out int vType;
flat out vec3 vCenter;
out vec3 vWorld;
const vec2 kCorners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                 vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 corner = kCorners[gl_VertexID];
    vec3 p = aCenterRadius.xyz + (uRight * corner.x + uUp * corner.y) * aCenterRadius.w;
    vUV = vec3(corner * 0.5 + 0.5, float(aFrameType.x));
    vColor = aColor.bgr;
    vOpacity = aOpacity;
    vType = aFrameType.y;
    vCenter = aCenterRadius.xyz;
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
`,
'sprite.frag': `precision highp float;
precision highp int;
precision highp sampler2DArray;
in vec3 vUV;
flat in vec3 vColor;
flat in float vOpacity;
flat in int vType;
flat in vec3 vCenter;
in vec3 vWorld;
uniform sampler2DArray uNoise;
uniform float uTime;        // seconds x the speed of the smoke's noise
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogColor;
uniform vec3  uEye;
uniform vec3  uFogOrigin;   // eye (following) or the map's centre (overview)
out vec4 fragColor;
float hash3(vec3 p) {
    p = fract(p * 0.3183099 + 0.1) * 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float valueNoise(vec3 x) {          // 0..1, smooth (cubic interpolation)
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i),                   hash3(i + vec3(1.0, 0.0, 0.0)), f.x),
                   mix(hash3(i + vec3(0.0, 1.0, 0.0)), hash3(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0.0, 0.0, 1.0)), hash3(i + vec3(1.0, 0.0, 1.0)), f.x),
                   mix(hash3(i + vec3(0.0, 1.0, 1.0)), hash3(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);
}
void main() {
    vec3 color;
    float a;
    if (vType == 1) {               // smoke
        vec2 q = vUV.xy * 2.0 - 1.0;
        float d = length(q);
        if (d >= 1.0) discard;
        vec3 np = vec3(q * 1.6 + vCenter.xz * 0.7 + vec2(vCenter.y * 0.5), uTime);
        float n = 0.65 * valueNoise(np) + 0.35 * valueNoise(np * 2.03 + 17.0);
        a = n * (1.0 - smoothstep(0.5, 1.0, d));
        color = vec3(1.0);
    } else {                        // debris
        a = texture(uNoise, vUV).r;
        color = vColor;
    }
    a *= vOpacity;
    if (a <= 0.0) discard;
    float fog = uFogOn != 0 ? smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin)) : 0.0;
    if (fog >= 1.0) discard;
    fragColor = vec4(mix(color, uFogColor, fog), a);
}
`,

// Boundary wall (rz_border.cpp): semi-transparent, with red Xs of
// uXSize tiles; only near the target (fade between uFadeFar and uFadeNear, the
// horizontal distance from the target to the wall's point); with fog.
// Sky (rz_sky.cpp): a triangle that covers the screen; each pixel's ray comes
// from the camera's basis and the focals. Base: the background colour (the fog's,
// which blends at the horizon); the sun (light background) or the moon (dark) in the
// light's direction; discreet clouds on a high plane, in cells, fading at the horizon.
'sky.vert': `out vec2 vNdc;
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
`,
'sky.frag': `precision highp float;
precision highp int;
in vec2 vNdc;
uniform vec3  uRight, uUp, uForward;      // the camera (world, y up)
uniform vec3  uFocal;                     // focalX, focalY
uniform vec3  uLight;                     // direction to the light
uniform vec3  uColor;                     // background colour
uniform float uTime;                      // seconds
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    vec3 dir = normalize(uForward + uRight * (vNdc.x / uFocal.x) + uUp * (vNdc.y / uFocal.y));
    vec3 color = uColor;
    float day = step(0.45, dot(uColor, vec3(0.299, 0.587, 0.114)));   // light background: day

    // sun: a glare (no disc); moon: 32 x 32 pixels, pale, with craters,
    // 70% lit (waxing gibbous: the terminator is a half ellipse)
    float d = max(dot(dir, uLight), 0.0);
    if (day > 0.5) {
        float glare = pow(d, 6.0) * 0.18 + pow(d, 60.0) * 0.45 + pow(d, 800.0) * 1.2;
        color = mix(color, vec3(1.0, 0.97, 0.88), clamp(glare, 0.0, 1.0));
    } else if (d > 0.9) {
        vec3 side = normalize(cross(uLight, vec3(0.0, 1.0, 0.0)));
        vec3 top = cross(side, uLight);
        vec2 m = vec2(dot(dir, side), dot(dir, top)) / 0.05;   // the moon's radius: 0.05 rad
        vec2 cell = floor(m * 16.0);                           // 32 x 32 on the disc
        vec2 q = (cell + 0.5) / 16.0;
        if (length(q) < 1.0) {
            float crater = step(0.75, hash(floor(cell / 2.0) + 3.0)) * 0.16 + noise(q * 4.0) * 0.12;
            // lit where x > -0.4 sqrt(1 - y²): one half + 0.4 of the other half = 70%
            float lit = step(-0.4 * sqrt(max(1.0 - q.y * q.y, 0.0)), q.x);
            color = mix(color, vec3(0.86, 0.88, 0.92) * (1.0 - crater), 0.12 + 0.88 * lit);
        }
    }

    // clouds: a high plane, in cells (slightly pixelated), drifting slowly
    if (dir.y > 0.0) {
        vec2 uv = dir.xz / max(dir.y, 0.05) * 3.0 + vec2(uTime * 0.03, uTime * 0.01);
        uv = floor(uv * 12.0) / 12.0;
        float n = noise(uv) * 0.6 + noise(uv * 2.1 + 7.0) * 0.3 + noise(uv * 4.3 + 13.0) * 0.1;
        float cloud = smoothstep(0.55, 0.8, n) * smoothstep(0.03, 0.3, dir.y);
        vec3 tint = day > 0.5 ? vec3(1.0) : uColor + vec3(0.12);
        color = mix(color, tint, cloud * (day > 0.5 ? 0.45 : 0.35));
    }
    fragColor = vec4(color, 0.0);
}
`,

'wall.vert': `layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;          // tiles along the wall / above the base
uniform mat4 uViewProj;
out vec2 vUV;
out vec3 vWorld;
void main() {
    vUV = aUV;
    vWorld = aPosition;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
`,
'wall.frag': `precision highp float;
precision highp int;
in vec2 vUV;
in vec3 vWorld;
uniform vec3  uTarget;
uniform float uFadeNear, uFadeFar;
uniform float uXSize;
uniform float uCellSize;
uniform float uTop, uTopFade;   // v (tiles above the base) of the top and the band over which the wall fades
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogColor;
uniform vec3  uEye;
uniform vec3  uFogOrigin;   // eye (following) or the map's centre (overview)
out vec4 fragColor;
void main() {
    float near = 1.0 - smoothstep(uFadeNear, uFadeFar, length(vWorld.xz - uTarget.xz));
    if (near <= 0.0) discard;
    // Pattern fixed to the world: along the wall (u, tiles) and at the world's
    // height (does not follow the relief). A cell of uXSize tiles per circle:
    // a red disc with a white border and a hollow X in the middle (shows the background).
    // Radius R (fraction of the cell): diameter = half the cell, so the space
    // between two neighbouring circles is a circle's size.
    const float R = 0.25;
    vec2  p  = vec2(vUV.x, vWorld.y / uCellSize) / uXSize;
    vec2  c  = fract(p) - 0.5;
    float r  = length(c);
    float aa = fwidth(p.x) + 0.002;                        // soft edge, ~1 pixel
    float disc  = 1.0 - smoothstep(R - aa, R + aa, r);
    float inner = 1.0 - smoothstep(R * 0.83 - aa, R * 0.83 + aa, r);
    float ring  = disc - inner;                            // white border
    float dx    = min(abs(c.x - c.y), abs(c.x + c.y)) * 0.70710678;
    float xMask = (1.0 - smoothstep(R * 0.13 - aa, R * 0.13 + aa, dx))
                * (1.0 - smoothstep(R * 0.6 - aa, R * 0.6 + aa, r));
    float red   = inner * (1.0 - xMask);
    vec3  color = vec3(0.62, 0.09, 0.07);                  // background: translucent red, dark
    float alpha = 0.20;
    color = mix(color, vec3(0.92, 0.10, 0.07), red);  alpha = mix(alpha, 0.85, red);
    color = mix(color, vec3(1.0, 1.0, 1.0), ring);    alpha = mix(alpha, 0.90, ring);
    alpha *= near;
    alpha *= 1.0 - smoothstep(uTop - uTopFade, uTop, vUV.y);   // top: fades out gradually
    if (uFogOn != 0) {
        float fog = smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin));
        color = mix(color, uFogColor, fog);
        alpha *= 1.0 - fog;
    }
    fragColor = vec4(color, alpha);
}
`,

// Shadow pass: depth only, from the light's point of view. It serves the
// terrain and the objects (position in attribute 0 in both).
'depth.vert': `layout(location = 0) in vec3 aPosition;
uniform mat4 uLightViewProj;
uniform mat4 uModel;                      // identity; wheels: each wheel's position/turn
void main() {
    gl_Position = uLightViewProj * (uModel * vec4(aPosition, 1.0));
}
`,
'depth.frag': `precision highp float;
precision highp int;
void main() {
}
`,

// The objects' shadow pass: position from the object's block (fragment: depth.frag)
'object_depth.vert': `// The objects' shadow pass: the position comes from the object's block (as in
// object.vert); the lines bring the position directly (vertex -1).
layout(location = 0) in vec3 aPosition;
layout(location = 3) in ivec3 aPolygon;   // the object's vertex (-1: uses aPosition), polygon
uniform mat4 uLightViewProj;
// The object's block (rz_internal.h: kMaxObjectVertices, kMaxObjectIndices)
layout(std140) uniform ObjectData {
    vec4  uPositions[256];                // positions in the world of the object's vertices
    ivec4 uIndices[768];                  // the polygons' indices, four per ivec4
};
void main() {
    vec3 position = aPolygon.x >= 0 ? uPositions[aPolygon.x].xyz : aPosition;
    gl_Position = uLightViewProj * vec4(position, 1.0);
}
`,
};
