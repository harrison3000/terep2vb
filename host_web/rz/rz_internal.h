// Tipos e funções internas do renderer (WebGL 2). Não faz parte da
// interface pública.
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "renderizeitor.h"
#include "rz_gl.h"

namespace rz {

// ---------------------------------------------------------------------------
// Constantes
// ---------------------------------------------------------------------------

constexpr int32_t kGridSize      = 256;                         // vértices por lado
constexpr int32_t kQuadsPerSide  = kGridSize - 1;               // 255
constexpr int32_t kVertexCount   = kGridSize * kGridSize;       // 65.536
constexpr int32_t kTriangleCount = kQuadsPerSide * kQuadsPerSide * 2; // 130.050
constexpr int32_t kPaletteSize   = 766;                         // soma de 3 alturas: 0..765

// Cor de fundo padrão (antes de rzSetBackgroundColor + rzLoadTileAtlas), já em float
constexpr float kBackgroundR = 32.0f / 255.0f;
constexpr float kBackgroundG = 40.0f / 255.0f;
constexpr float kBackgroundB = 48.0f / 255.0f;

constexpr float kPi              = 3.14159265358979f;
constexpr float kFovYDegrees     = 50.0f;   // padrão de rzSetCameraFov
constexpr float kFovYMin         = 10.0f;
constexpr float kFovYMax         = 120.0f;
constexpr float kAmbient         = 0.3f;
constexpr float kTexturedShading = 0.35f;   // força da luz sobre o chão texturizado (0 = textura pura)

// Visão geral (sem alvo): terreno inteiro, visto do lado +z, inclinação fixa
constexpr float kOverviewPitchDegrees = 50.0f;

// Escala do legado: byte 0 = altura 0, byte 255 = 16 tiles (mundo 255 x 255 x 16)
constexpr float kDefaultCellSize    = 1.0f;
constexpr float kDefaultHeightScale = 16.0f / 255.0f;

// Câmera de perseguição, na escala de um carro de ~0,85 tile
constexpr float kFollowDistance  = 3.0f;    // corda (tiles)
constexpr float kFollowHeight    = 1.0f;    // altura acima do alvo (tiles)
constexpr float kFollowStiffness = 0.08f;
constexpr float kFollowClearance    = 0.5f;   // altura mínima desejada sobre o chão (tiles)
constexpr float kFollowMinClearance = 0.1f;   // limite duro sobre o chão (tiles)
constexpr float kFollowClimb        = 0.2f;   // amortecimento ao subir
// Ponto olhado (alvo suavizado): o tremor do alvo em terreno esburacado não
// chega à câmera. Forte na vertical, leve na horizontal (pouco atraso).
constexpr float kFollowLookXZ       = 0.5f;
constexpr float kFollowLookY        = 0.1f;
// Zona morta do ângulo da câmera: uma bolha (esfera) em volta do alvo, que o
// alvo empurra por dentro. A câmera olha para o centro da bolha; enquanto o
// alvo anda dentro dela, o olhar não muda. A posição (corda) segue normal.
constexpr float kCameraBubbleRadius  = 0.3f;    // tiles
constexpr float kInternalNear        = 0.02f;   // câmera interna: near plane (tiles)
constexpr float kInternalEnterFrames = 30.0f;   // perseguição -> interna: voo suave (frames)

// Troca de alvo da câmera (rzSetCameraTarget com outro objeto): em vez de
// teleportar, a câmera vai até o novo alvo. A câmera avança por frame, então a
// velocidade é por frame, contando 60 fps.
//   - alvo a até kRetargetTurnDistance tiles: só vira (o ponto olhado desliza
//     até o novo alvo em kRetargetTurnFrames) e a corda traz a câmera;
//   - mais longe: voo em arco até atrás do novo alvo, subindo até
//     kRetargetArcHeight tiles no meio (altura cheia a partir de
//     kRetargetArcFull tiles), sem passar de kRetargetMaxSpeed.
constexpr float kRetargetTurnDistance = 10.0f;   // tiles
constexpr float kRetargetTurnFrames   = 30.0f;
constexpr float kRetargetArcFull      = 60.0f;   // tiles
constexpr float kRetargetArcHeight    = 25.0f;   // tiles
constexpr float kRetargetMaxSpeed     = 100.0f / 60.0f;   // tiles por frame
constexpr float kRetargetMinFrames    = 30.0f;

// Entrada e saída da visão geral: voo de duração fixa (a distância varia muito).
// A neblina migra do olho (seguindo) para o centro do mapa (visão geral, de
// terrainRadius até + kOverviewFogDepth tiles: só a continuação some nela);
// as sombras trocam de modo no meio do caminho.
constexpr float kOverviewFrames   = 120.0f;
constexpr float kOverviewFogDepth = 60.0f;   // tiles

constexpr int32_t kMaxWindowSize = 8192;

// Coordenada de vértice/sprite vinda do host (tiles): finita e |v| < 1e6
// (a comparação falha com NaN; infinito passa do limite)
inline bool validCoord(float v) { return v > -1.0e6f && v < 1.0e6f; }

// Neblina por distância (só seguindo um alvo; a visão geral fica sem): limpa
// até o início, some na cor de fundo no fim (tiles, distância 3D ao olho,
// curva smoothstep). Além do fim nada é desenhado. Padrão 30..65; muda em
// runtime com rzSetFog (RzContext::fogStart/fogEnd), fim limitado a kFogMaxEnd.
constexpr float kFogStartDefault = 30.0f;
constexpr float kFogEndDefault   = 65.0f;
constexpr float kFogCullMargin = 10.0f;   // tiles: objetos além do fim + raio + margem
                                          // não entram nem na tela nem nas sombras
                                          // (a margem cobre sombras longas para dentro)

// Borda do mundo (rz_border.cpp): continuação do terreno além do mapa, até o
// alcance da neblina + folga, e parede de limite com X vermelhos.
// Faixas da malha da continuação, de dentro para fora: até `limit` tiles da
// borda, células de `step` tiles. A última vai até kSkirtExtent.
struct SkirtBand { int32_t limit, step; };
constexpr SkirtBand kSkirtBands[] = { { 8, 1 }, { 16, 2 }, { 32, 4 }, { 128, 16 } };
constexpr int32_t kSkirtBandCount = int32_t(sizeof(kSkirtBands) / sizeof(kSkirtBands[0]));
constexpr int32_t kSkirtExtent   = kSkirtBands[kSkirtBandCount - 1].limit;   // 128: alinha as células de 16
constexpr float   kSkirtBlend    = 24.0f;  // tiles da altura da borda até a gerada
constexpr float   kSkirtNoiseLow  = 60.0f; // amplitude do ruído (unidade do byte), período 32
constexpr float   kSkirtNoiseHigh = 18.0f; // período 12
constexpr int32_t kSkirtTileBand  = 8;     // sorteio: quads a até 8 tiles do ponto da borda mais próximo
constexpr int32_t kSkirtJitterPercent = 12;   // % das células que sorteiam em vez de continuar a borda
constexpr float   kSkirtMeanderGrowth = 0.35f; // deslocamento ao longo da borda: até 0,35 x distância...
constexpr float   kSkirtMeanderMax    = 12.0f; // ...limitado a 12 tiles
constexpr int32_t kSkirtMeanderPeriod = 24;    // período do ruído do serpenteio (tiles)
constexpr float   kBorderWallHeight = 6.0f;   // tiles acima do chão
constexpr float   kBorderTopFade    = 2.5f;   // some aos poucos nos últimos tiles de cima
constexpr float   kBorderFadeFar    = 25.0f;  // alvo a 25 tiles: começa a aparecer
constexpr float   kBorderFadeNear   = 6.0f;   // a 6 tiles: totalmente visível
constexpr float   kBorderXSize      = 2.125f; // célula de cada círculo (tiles), presa ao mundo
constexpr float   kFogMaxEnd = 120.0f;   // rzSetFog (a continuação vai a kSkirtExtent, com folga)
static_assert(kFogEndDefault <= kFogMaxEnd && kFogMaxEnd <= float(kSkirtExtent),
              "a continuação tem que ir até o fim da neblina");

// Transparência das texturas PCX (atlas e objetos): o índice 255 da paleta
// vira alfa 0 (o resto, alfa 255); o shader descarta o pixel com alfa < 0,5.
// Mipmaps com média ponderada pelo alfa (a cor do 255 não vaza nas bordas).
// A sombra ignora (os buracos ainda projetam sombra).
constexpr int32_t  kTransparentIndex = 255;

// Sprites (rz_sprites.cpp): fumaça/detritos/água, quads virados para a câmera
constexpr int32_t kMaxSprites          = 256;    // máximo validado no legado
constexpr int32_t kSpriteFrames        = 8;      // variações do ruído (textura array)
constexpr int32_t kSpriteFrameTicks    = 4;      // frames de render por quadro
constexpr int32_t kSpriteNoiseSize     = 64;     // lado da textura de ruído
constexpr int32_t kSpriteClicks        = 2;      // "cliques do spray" por quadro
constexpr int32_t kSpriteDotsPerClick  = 260;    // pontinhos por clique
constexpr float   kSpriteClickRadius   = 0.36f;  // raio do clique (fração do lado)
constexpr float   kSpriteClickJitter   = 0.10f;  // deslocamento do centro do clique
// Detrito: alfa dentro do círculo por cliques que acertaram o pixel (0, 1, 2+);
// fora do círculo, 0 (ajustados no olho)
constexpr uint8_t kSpriteAlphaByHits[3] = { 40, 200, 255 };
constexpr float   kSmokeNoiseSpeed     = 0.4f;   // fumaça: avanço do ruído por segundo (60 frames)

// Sombras: cascatas da luz direcional (detalhes em rz_shadow.cpp), por
// distância ao olho: 0 de 0 a kCascadeSplit0 tiles, 1 até kCascadeSplit1 (as
// duas com terreno + objetos, refeitas todo frame) e 2 até o fim da neblina
// (só terreno; caixa em volta do olho, refeita quando ele anda
// kCascadeFarRecenter tiles ou o terreno muda). Transição de kCascadeBlend
// tiles entre elas. Na visão geral, só a 2, com o terreno inteiro e os objetos.
constexpr int32_t kShadowMaps           = 3;
// Tamanhos pedidos; limitados a GL_MAX_TEXTURE_SIZE na criação (ctx->shadowSize).
constexpr int32_t kCascadeNearSize      = 2048;
constexpr int32_t kCascadeMidSize       = 1024;
constexpr int32_t kCascadeFarSize       = 2048;
constexpr float   kCascadeSplit0        = 8.0f;   // tiles do olho
constexpr float   kCascadeSplit1        = 30.0f;
constexpr float   kCascadeBlend         = 2.0f;   // tiles de transição antes de cada divisa
constexpr float   kCascadeFarRecenter   = 10.0f;  // tiles andados até refazer a cascata 2
constexpr float   kCascadeFarMargin     = 4.0f;   // tiles além da neblina + recentragem
constexpr int32_t kShadowScissorMargin  = 4;     // texels em volta da área refeita da cascata 2
constexpr int32_t kUnitShadowFirst      = 1;      // unidades de textura 1..3 (0: atlas/objeto)
// Posições e índices de cada objeto num uniform block (ObjectData, nos
// shaders object.vert e object_depth.vert, layout std140): kMaxObjectVertices
// vec4 de posição e depois kMaxObjectIndices int, quatro por ivec4. 16 KB,
// o mínimo que todo WebGL 2 aceita num bloco.
constexpr int32_t kMaxObjectVertices    = 256;
constexpr int32_t kMaxObjectIndices     = 3072;
constexpr int32_t kObjectIndicesOffset  = kMaxObjectVertices * 16;              // bytes
constexpr int32_t kObjectDataSize       = kObjectIndicesOffset + kMaxObjectIndices * 4;
constexpr GLuint  kObjectDataBinding    = 0;      // ponto de ligação do bloco
constexpr float   kShadowTexturedDim  = 0.48f;  // chão texturizado na sombra (era 0,6; 20 % mais escuro)
constexpr float   kShadowLight        = kAmbient * 0.8f;   // luz na sombra (cores flat e objetos):
                                                         // 20 % abaixo do ambiente
constexpr float   kShadowOffsetFactor = 2.0f;   // glPolygonOffset no passe de profundidade
constexpr float   kShadowOffsetUnits  = 4.0f;
constexpr float   kShadowNormalOffset = 1.5f;   // normal offset na leitura, em texels do mapa
constexpr float   kShadowTerminator   = 0.15f;  // n · l abaixo disso: a sombra entra (smoothstep)
constexpr float   kCascadeSizeStep    = 1.1f;   // caixas 0 e 1: tamanho em degraus (não muda todo frame)

// Texturas: blocos 16x16 numa textura array (uma camada por bloco), com
// mipmaps 16, 8, 4, 2, 1 gerados na carga (média 2x2, como no software).
constexpr int32_t kTileSize      = 16;
constexpr int32_t kMipLevels     = 5;
constexpr int32_t kMaxTiles      = 256;                         // índice é um byte
constexpr int32_t kAtlasSize     = 256;                         // atlas: 16 x 16 blocos de 16x16

// ---------------------------------------------------------------------------
// Matemática
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

// Matriz 4x4, vetores coluna: v' = M · v. Acesso m[linha, coluna].
// Enviada ao shader por setMatrices (que a transpõe: o WebGL não transpõe).
struct Mat4 {
    float e[16];

    float&       operator[](int row, int col)       { return e[row * 4 + col]; }
    const float& operator[](int row, int col) const { return e[row * 4 + col]; }

    static Mat4 identity();
};

Mat4 operator*(const Mat4& a, const Mat4& b);

// ---------------------------------------------------------------------------
// Objetos
// ---------------------------------------------------------------------------

// Triângulo de objeto: índices de vértice e o polígono de origem (para a cor).
// Cantos do triângulo como posições em Object::indices (dali saem o vértice e o UV).
struct ObjectTriangle {
    int32_t  a, b, c;
    uint16_t polygon;
};

// Vértice de objeto enviado à GPU: posição no mundo, luz flat do polígono
// (cinza 0x00LLLLLL; em memória B, G, R, 0, lido como vec4 normalizado), que
// multiplica a textura, e UV.
// Linha de um objeto (rzAddObjectLine): prisma de seção quadrada entre dois
// vértices, refeito a cada envio do VBO (6 faces, 2 triângulos cada)
constexpr int32_t kLineVertices = 36;
struct ObjectLine {
    uint16_t a, b;
    int16_t  palette;
    float    half;          // meia grossura, mundo
};

struct GpuVertex {
    float    x, y, z;
    uint32_t normal;           // normal da face (packNormal); a luz é feita no shader
    float    u, v;
};
static_assert(sizeof(GpuVertex) == 24);

// Canto de triângulo dos objetos (VBO estático): nos polígonos, só o vértice
// do objeto, o polígono (início e tamanho em `indices`) e o UV; posição e
// normal saem das texturas de posições e índices no vertex shader. Nas linhas (vertex = -1),
// posição e normal diretas, refeitas a cada update.
struct ObjectVertex {
    float    x, y, z;          // só linhas
    uint32_t normal;           // só linhas (packNormal)
    float    u, v;             // só polígonos texturizados
    int32_t  vertex;           // índice do vértice do objeto, ou -1 (linha)
    int32_t  polygonStart, polygonLength;
    uint32_t color;            // R, G, B em bytes e o tipo do canto no alfa (kCorner*)
};
static_assert(sizeof(ObjectVertex) == 40);

// O tipo do canto, no alfa de ObjectVertex::color (object.frag): texturizado
// (amostra a textura no UV), de cor sólida (a cor do vértice: rzAddObjectPolygon
// e linhas) ou buraco (a cor sólida 255, transparente: não desenha, mas faz sombra)
constexpr uint32_t kCornerTextured = 0, kCornerHole = 128, kCornerSolid = 255;

// Vértice do terreno: um por canto de triângulo (malha não indexada, porque a
// cor flat e o bloco são do triângulo/quad, não do ponto da grade).
struct TerrainVertex {
    float    x, y, z;
    uint32_t color;            // cor flat do triângulo (sem luz), 0x00RRGGBB
    uint8_t  u, v, layer;      // canto do quad (0/1) e bloco do atlas
    uint8_t  pad;
    uint32_t normal;           // normal da face (packNormal); a luz é feita no shader
};
static_assert(sizeof(TerrainVertex) == 24);

// Vértice do vidro (rz_glass.cpp): posição, normal de fora do polígono, cinza
struct GlassVertex {
    float x, y, z;
    float nx, ny, nz;
    float tint;                // 0 (preto) .. 1 (transparente) = tom / 15
};
static_assert(sizeof(GlassVertex) == 28);

// Rodas (rz_wheels.cpp): pneus, geometria de revolução com ombros arredondados
constexpr int32_t  kWheelSegments      = 24;        // lados do anel
constexpr int32_t  kWheelShoulderSteps = 3;         // passos que arredondam cada ombro do pneu
constexpr float    kWheelWidth    = 0.4f;           // largura = 0,4 x diâmetro (12:30)
constexpr uint32_t kWheelColor    = 0x001C1C1Cu;    // quase preto (para a luz ainda aparecer)
// Casca de cor fixa: ombro (x2, com passos) + banda de rodagem (x1) + a parede
// do recuo da jante (x2). Cada banda = kWheelSegments quads (2 triângulos).
constexpr int32_t  kWheelSkinBands = 2 * kWheelShoulderSteps + 3;
constexpr int32_t  kWheelSideVerts = kWheelSkinBands * kWheelSegments * 6;     // casca do pneu (cor fixa)
// Faces texturizadas: a parede lateral (anel plano, x2) + a jante recuada
// (leque até o centro, x2).
constexpr int32_t  kWheelCapVerts  = 2 * kWheelSegments * 6                    // paredes laterais (anéis)
                                   + 2 * kWheelSegments * 3;                   // jantes recuadas (leques)

// Vidro: brilho especular "embaçado" (Blinn-Phong de expoente baixo)
constexpr float kGlassSpecular  = 0.45f;
constexpr float kGlassShininess = 12.0f;

struct Object {
    bool      alive   = false;
    bool      positioned = false;   // já recebeu rzUpdateObjectVertices
    bool      gpuDirty   = false;   // cantos/VBO desatualizados (polígonos, linhas): reenviar no próximo frame
    bool      positionsDirty = false;   // posições novas (rzUpdateObjectVertices): reenviar ao bloco
    Vec3      center = { 0.0f, 0.0f, 0.0f };   // esfera envolvente (refeita em cada update)
    float     radius = 0.0f;

    std::vector<Vec3>     world;          // posições no mundo, atualizadas pelo host
    std::vector<int32_t>  polygonStart;   // em `indices`
    std::vector<int32_t>  polygonLength;  // sem o índice de fechamento
    std::vector<uint16_t> indices;        // cópia dos índices dos polígonos (sem os fechamentos)
    std::vector<float>    uvs;            // (u, v) de cada entrada de `indices`; 0 nos de cor sólida
    std::vector<int16_t>  polygonPalette; // índice de cor (rzAddObjectPolygon) ou -1 (UVs próprios)
    std::vector<ObjectTriangle> triangles; // leque de cada polígono, montado na carga
    std::vector<ObjectLine> lines;        // rzAddObjectLine: prisma fino gerado no envio
    std::vector<ObjectVertex> staging;    // triangles.size() * 3 + lines.size() * kLineVertices
    std::vector<float>    positionStaging;   // world em vec4, para o bloco
    GLuint    dataBuffer = 0;       // o uniform block do objeto: posições e índices

    GLuint    vao = 0;
    GLuint    vbo = 0;
    // Vidro (rz_glass.cpp): polígonos translúcidos, VBO próprio
    std::vector<int32_t>  glassStart, glassLength;
    std::vector<uint8_t>  glassTone;      // 0..15
    std::vector<uint16_t> glassIndices;
    std::vector<GlassVertex> glassStaging; // triângulos do leque, preenchido no envio
    GLuint    glassVao = 0, glassVbo = 0;
    bool      glassDirty = false;

    // Rodas (rz_wheels.cpp): staging vazio = sem rodas
    uint16_t  wheelVertex[4] = {};
    uint8_t   wheelFront[4] = {};
    float     wheelDiameter[4] = {};    // mundo
    float     wheelSteer = 0.0f;        // dianteiras; radianos, positivo = esquerda
    float     wheelRoll[4] = {};        // giro acumulado de cada roda; radianos
    float     wheelSpeed = 0.0f;        // velocidade do carro (tiles/frame); gira as rodas
    std::vector<TerrainVertex> wheelStaging;    // parte plana: pneu (+ faces sem textura)
    GLuint    wheelVao = 0, wheelVbo = 0;
    int32_t   wheelFlatVerts = 0;       // vértices em uso no VBO plano
    bool      wheelsDirty = false;      // malha local a refazer (rodas/faces novas)
    // Malha de cada roda no referencial dela (x frente, y cima, z eixo, centro
    // no cubo), feita só na carga; a cada frame só muda wheelModel[i]
    // (posição, esterçamento e giro), aplicada no vertex shader (uModel)
    int32_t   wheelFlatFirst[4] = {}, wheelFlatCount[4] = {};
    int32_t   wheelCapFirst[4] = {},  wheelCapCount[4] = {};
    Mat4      wheelModel[4] = {};
    // Faces texturizadas (rzSetObjectWheelFaces): os discos, com a textura do
    // objeto; VBO próprio no formato dos objetos (GpuVertex).
    bool      wheelFacesSet = false;
    float     wheelFaceOuter[4] = {};   // u0, v0, u1, v1 do quadrado da face externa
    float     wheelFaceInner[4] = {};   // idem, face interna
    uint8_t   wheelPlusOuter[4] = {};   // 1 = a tampa do lado +eixo é a externa (fixado no 1o build)
    float     wheelUpSign = 1.0f;       // sentido do "cima" das rodas, fixado no 1o build (carro em pé)
    bool      wheelOuterResolved = false;   // já fixou wheelUpSign e interna/externa de cada roda
    std::vector<GpuVertex> wheelCapStaging;
    GLuint    wheelCapVao = 0, wheelCapVbo = 0;
    int32_t   wheelCapVerts = 0;        // vértices em uso no VBO das faces

    GLuint    texture = 0;          // 0: usa a textura fallback do contexto
    uint8_t   palette[768];         // RGB da paleta do PCX da textura (as cores sólidas)

    int32_t vertexCount() const   { return int32_t(world.size()); }
    int32_t triangleCount() const { return int32_t(triangles.size()); }
    int32_t drawVertexCount() const { return int32_t(staging.size()); }   // polígonos + linhas
};

// Locais de uniforms dos programas
struct FogUniforms {
    GLint on = -1, start = -1, end = -1, color = -1, eye = -1, origin = -1;
};

struct ShadowUniforms {
    GLint matrices = -1, cascade = -1, onlyFar = -1, offset = -1;
    GLint maps[kShadowMaps] = { -1, -1, -1 };
};

struct TerrainProgram {
    GLuint program = 0;
    GLint  viewProj = -1, atlas = -1, textured = -1, shading = -1, ambient = -1;
    GLint  smooth = -1;    // luz por pixel (normal interpolada) em vez de flat: rodas
    GLint  model = -1;     // uModel: identidade, exceto nas rodas
    ShadowUniforms shadow;
    FogUniforms    fog;
};

struct ObjectProgram {
    GLuint program = 0;
    GLint  viewProj = -1, texture = -1, ambient = -1;
    GLint  model = -1;     // uModel: identidade, exceto nas rodas
    GLint  fromBuffer = -1; // 1: objetos (posições/normais do bloco); 0: faces das rodas
    ShadowUniforms shadow;
    FogUniforms    fog;
};

struct GlassProgram {
    GLuint program = 0;
    GLint  viewProj = -1, light = -1, specular = -1, shininess = -1;
    ShadowUniforms shadow;
    FogUniforms    fog;
};

struct WallProgram {
    GLuint program = 0;
    GLint  viewProj = -1, target = -1, fadeNear = -1, fadeFar = -1, xSize = -1, cellSize = -1;
    GLint  top = -1, topFade = -1;     // altura do topo e faixa de fade (v, tiles)
    FogUniforms fog;
};

struct SpriteProgram {
    GLuint program = 0;
    GLint  viewProj = -1, right = -1, up = -1, noise = -1, time = -1;
    FogUniforms fog;
};

// Sprite no mundo (rz_sprites.cpp), convertido em rzSetSprites
struct Sprite {
    Vec3     center;
    float    radius;       // mundo
    float    opacity;      // fumaça: multiplicador do ruído
    uint32_t color;        // detrito: 0x00RRGGBB, já com luz
    uint8_t  type;         // RZ_SPRITE_*
};
struct SpriteKey { float depth; int32_t index; };    // ordenação de trás para frente
struct SpriteVertex {      // 28 bytes; seis por sprite
    float    x, y, z, radius;
    float    opacity;         // fumaça
    int8_t   cornerX, cornerY, frame, type;
    uint32_t color;           // B, G, R, 0 em bytes
};

struct DepthProgram {
    GLuint program = 0;
    GLint  lightViewProj = -1;
    GLint  model = -1;     // uModel: identidade, exceto nas rodas
};

// Imagem paletizada lida de um PCX (rz_pcx.cpp)
struct PcxImage {
    int32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;   // width * height, linha 0 em cima
    uint8_t palette[768];          // 256 x (R, G, B)
};

} // namespace rz

// ---------------------------------------------------------------------------
// Contexto
// ---------------------------------------------------------------------------

struct RzContext {
    int32_t   width  = 0;             // o drawing buffer do canvas
    int32_t   height = 0;

    float     fogStart = rz::kFogStartDefault;   // tiles (rzSetFog)
    float     fogEnd   = rz::kFogEndDefault;
    float     backgroundR = rz::kBackgroundR;
    float     backgroundG = rz::kBackgroundG;
    float     backgroundB = rz::kBackgroundB;
    // Fundo: índice na paleta do atlas (rzSetBackgroundColor; -1 = cor padrão).
    // A cor em float acima é refeita quando o índice ou a paleta mudam.
    int32_t   backgroundIndex = -1;
    bool      hasAtlasPalette = false;
    uint8_t   atlasPalette[768];      // RGB do PCX do atlas (rzLoadTileAtlas)

    rz::TerrainProgram terrainProgram;
    rz::ObjectProgram  objectProgram;
    rz::DepthProgram   depthProgram;
    rz::DepthProgram   objectDepthProgram;   // sombra dos objetos (posição do bloco)
    rz::WallProgram    wallProgram;
    rz::GlassProgram   glassProgram;
    rz::SpriteProgram  spriteProgram;

    // Sprites (rz_sprites.cpp): o array do último rzSetSprites; vetores
    // reservados na criação (kMaxSprites), sem alocar depois
    std::vector<rz::Sprite>       sprites;
    std::vector<rz::SpriteKey>    spriteOrder;
    std::vector<rz::SpriteVertex> spriteStaging;
    uint32_t   spritePalette[256] = {};              // 0x00RRGGBB com luz (atlas ou cinza)
    rz::GLuint spriteVao = 0, spriteVbo = 0, spriteNoiseTex = 0;
    uint32_t   frameCount = 0;                       // rzRender chamados (quadro do ruído)

    // Borda do mundo (rz_border.cpp), refeita com a malha do terreno
    std::vector<float> extHeights;      // (255 + 2 x 128 + 2)^2, unidade do byte
    rz::GLuint skirtVao = 0, skirtVbo = 0;
    std::vector<rz::TerrainVertex> skirtStaging;    // malha da continuação na CPU (reaproveitada)
    int32_t    skirtVertexCount = 0;
    int32_t    skirtFirst[4] = {}, skirtCount[4] = {};   // regiões N, L, S, O (drawSkirt)
    rz::GLuint wallVao = 0, wallVbo = 0;
    int32_t    wallVertexCount = 0;

    // Sombras (rz_shadow.cpp): três mapas, FBO só com profundidade cada
    rz::GLuint shadowFbo[rz::kShadowMaps] = {};
    rz::GLuint shadowTex[rz::kShadowMaps] = {};
    rz::Mat4   shadowMatrix[rz::kShadowMaps] = {};     // do frame; contíguas (glUniformMatrix4fv)
    float      shadowTexel[rz::kShadowMaps] = {};      // tamanho do texel de cada mapa (mundo)
    int32_t    shadowSize[rz::kShadowMaps] = {};      // lado de cada mapa (já limitado pelo driver)
    bool       terrainShadowDirty = true;            // terreno mudou: cascata 2 refeita...
    bool       terrainShadowAll   = true;            // ...inteira, ou só a caixa abaixo (mundo)
    rz::Vec3   terrainShadowLo = { 0.0f, 0.0f, 0.0f }, terrainShadowHi = { 0.0f, 0.0f, 0.0f };
    bool       cascadeFarValid = false;              // cascata 2 desenhada para o modo atual
    bool       cascadeFarOverview = false;           // ...na visão geral (terreno inteiro + objetos)
    rz::Vec3   cascadeFarCenter = { 0.0f, 0.0f, 0.0f };   // olho quando a cascata 2 foi refeita
    float      cascadeFarHalf = 0.0f;                // meia-largura dela (muda com a neblina)
    uint32_t   cascadeFarRedraws = 0;                // recentragens da cascata 2 (testes)

    // Do frame (updateCamera): olho e neblina
    rz::Vec3   eyePos = { 0.0f, 0.0f, 0.0f };
    rz::Vec3   targetPos = { 0.0f, 0.0f, 0.0f };     // vértice-alvo (parede de limite)
    rz::Vec3   camRight   = { 1.0f, 0.0f, 0.0f };    // eixos da câmera (sprites)
    rz::Vec3   camUp      = { 0.0f, 1.0f, 0.0f };
    rz::Vec3   camForward = { 0.0f, 0.0f, -1.0f };
    bool       fogOn = false;                        // há neblina (sempre, com câmera montada)
    rz::Vec3   fogOrigin = { 0.0f, 0.0f, 0.0f };     // centro da neblina: olho, ou centro do mapa
    float      fogNear = 0.0f, fogFar = 0.0f;        // início/fim da neblina (mundo)
    bool       wallOn = false;                       // parede de limite (perto do alvo)
    bool       shadowWholeTerrain = true;           // visão geral: só a cascata 2, terreno inteiro

    // Projeção
    float focalX = 1.0f;     // f / aspecto
    float focalY = 1.0f;     // f = 1 / tan(fov/2)
    float fovY   = rz::kFovYDegrees;   // graus, vertical (rzSetCameraFov)

    // Terreno
    std::vector<uint8_t> heights;      // 256x256, cópia na CPU (malha e câmera de perseguição)
    uint8_t   terrainMinHeight = 0, terrainMaxHeight = 255;   // faixa das alturas (cascatas de sombra)
    float     cellSize    = rz::kDefaultCellSize;
    float     heightScale = rz::kDefaultHeightScale;
    rz::GLuint terrainVao = 0;
    rz::GLuint terrainVbo = 0;         // 130.050 x 3 TerrainVertex, remontado na carga
    std::vector<rz::TerrainVertex> terrainStaging;  // a mesma malha na CPU (rzUpdateTerrain)
    const uint8_t* heightSource = nullptr;  // buffers do host (rzSetHeightmap/rzSetTileMap),
    const uint8_t* tileSource   = nullptr;  // relidos por rzUpdateTerrain
    bool hasTerrain() const { return !heights.empty(); }

    // Texturas (opcionais; sem as duas, desenha com as cores flat)
    rz::GLuint atlasTex = 0;           // array 16x16 x 256 camadas, 5 níveis
    std::vector<uint8_t> tileMap;      // 256x256, cópia na CPU (bloco de cada quad)
    bool      hasAtlas   = false;
    bool      hasTileMap = false;

    // Textura dos objetos sem textura própria (ou cuja carga falhou): xadrez
    // gerado ou PCX de rzLoadFallbackTexture
    rz::GLuint fallbackTex = 0;
    bool       hasFallbackPalette = false;   // false: o xadrez gerado (cores sólidas: a paleta do atlas)
    uint8_t    fallbackPalette[768];         // RGB da paleta do PCX da fallback

    // Câmera
    float terrainRadius = 0.0f;   // R: raio da esfera envolvente do terreno
    float nearPlane = 0.0f;       // calculados a cada frame pela posição da câmera
    float farPlane  = 0.0f;

    // Alvo da câmera: vértice de um objeto (câmera de perseguição), ou nenhum
    // (visão geral fixa do terreno)
    int32_t cameraTargetObject = -1;
    int32_t cameraTargetVertex = 0;
    int32_t cameraMode = RZ_CAMERA_FOLLOW;          // rzSetCamera

    // Câmera interna (rzSetCameraInternalAxes): base ortonormal no mundo
    bool     internalValid = false;
    rz::Vec3 internalForward = { 0.0f, 0.0f, -1.0f };
    rz::Vec3 internalUp      = { 0.0f, 1.0f, 0.0f };
    // Entrada suave na interna vindo da perseguição (ou da congelada)
    bool     internalEnterActive = false;
    float    internalEnterT = 0.0f;
    rz::Vec3 internalEye0 = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 internalF0   = { 0.0f, 0.0f, -1.0f };    // frente e cima da câmera no início
    rz::Vec3 internalU0   = { 0.0f, 1.0f, 0.0f };

    // Câmera de perseguição ("na corda"); estado avança a cada rzRender
    float    followDistance  = rz::kFollowDistance;
    float    followHeight    = rz::kFollowHeight;
    float    followStiffness = rz::kFollowStiffness;
    bool     followInitialized = false;
    float    followAppliedDistance = 0.0f;   // corda com que followEye foi calculado
    rz::Vec3 followEye = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 followLook = { 0.0f, 0.0f, 0.0f };   // alvo suavizado: puxa a corda (posição da câmera)
    rz::Vec3 aimPoint = { 0.0f, 0.0f, 0.0f };     // centro da bolha: para onde a câmera olha (perseguição normal)
    bool     aimValid = false;                    // false: a próxima perseguição normal começa com a bolha no alvo

    // Troca de alvo suave (rz_camera.cpp): pedida pelo rzSetCameraTarget,
    // montada no próximo frame (quando a posição do novo alvo é conhecida)
    bool     retargetPending = false;
    bool     retargetActive  = false;
    bool     retargetArc     = false;    // voo em arco; senão só vira e a corda age
    float    retargetT       = 0.0f;     // progresso 0..1
    float    retargetStep    = 0.0f;     // progresso por frame
    float    retargetLift    = 0.0f;     // altura extra no meio do arco (mundo)
    rz::Vec3 retargetEye0  = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 retargetLook0 = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 retargetDir   = { 0.0f, 0.0f, 0.0f };   // direção horizontal do voo (unitária)

    // Entrada/saída da visão geral (rz_camera.cpp)
    bool     camHasLast      = false;    // já houve um frame (camLast* valem)
    bool     camLastFollowing = false;
    int32_t  camLastMode = RZ_CAMERA_OVERVIEW;   // modo efetivo do frame anterior
    rz::Vec3 camLastEye  = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 camLastLook = { 0.0f, 0.0f, 0.0f };
    bool     overviewActive  = false;    // transição em andamento
    bool     overviewToward  = false;    // indo para a visão geral (senão saindo dela)
    float    overviewT       = 0.0f;
    float    overviewBlend   = 1.0f;     // 0 = neblina/sombras de perseguição, 1 = da visão geral
    float    overviewBlend0  = 1.0f;
    rz::Vec3 overviewEye0  = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 overviewLook0 = { 0.0f, 0.0f, 0.0f };
    rz::Vec3 overviewDir   = { 0.0f, 0.0f, 0.0f };

    // Objetos (id = índice no vetor; slots livres são reaproveitados)
    std::vector<rz::Object> objects;

    // Erros acumulados desde o último rzGetError: o primeiro (código e função)
    // e quantos houve
    int32_t     firstError = RZ_OK;
    const char* firstErrorFunction = nullptr;
    int32_t     errorCount = 0;
};

namespace rz {

// rz_terrain.cpp
void buildPalette(uint32_t* palette);
void updateTerrainBounds(RzContext* ctx);
bool buildTerrainMesh(RzContext* ctx);      // malha inteira + borda + sombra (carga)
bool rebuildTerrainTiles(RzContext* ctx);   // só blocos (rzSetTileMap)
bool updateTerrain(RzContext* ctx);         // rzUpdateTerrain: só o que mudou
void buildAtlasLevels(uint32_t* tiles, const uint8_t* indices, const uint8_t* paletteRGB);
void downsample(const uint32_t* src, uint32_t* dst, int32_t dstSide);
void fillTransparent(uint32_t* img, int32_t side);   // cor dos texels alfa 0 = vizinhos opacos
Vec3 lightDirection();
// Normal de face para o vértice (GL_INT_2_10_10_10_REV normalizado: x, y, z
// em 10 bits com sinal). Não precisa vir normalizada; a nula (triângulo
// degenerado) vira "para baixo", que só recebe a luz ambiente, como antes.
uint32_t packNormal(Vec3 n);
// Layout de TerrainVertex no VAO ligado (terreno, continuação e rodas):
// 0 posição, 1 cor, 2 (u, v, bloco, -), 3 normal
void setTerrainVertexLayout();
// Layout de GpuVertex no VAO ligado (objetos e faces das rodas):
// 0 posição, 1 normal, 2 (u, v)
void setObjectVertexLayout();

// rz_pcx.cpp: decodifica um PCX de 8 bits inteiro (o arquivo na memória).
// Devolve RZ_OK, RZ_ERR_FORMAT ou RZ_ERR_SIZE.
int32_t loadPcx(const uint8_t* file, int32_t size, PcxImage& img);
// rz_pcx.cpp: o canto 256x256 de um PCX de 8 bits (indices: 256*256 bytes,
// paletteRGB: 768 bytes). Devolve RZ_OK, RZ_ERR_FORMAT ou RZ_ERR_SIZE.
int32_t loadPcxAtlas(const uint8_t* file, int32_t size, uint8_t* indices, uint8_t* paletteRGB);

// rz_camera.cpp: avança a câmera um frame e devolve view-projection (convenção GL);
// também guarda o foco da sombra.
Mat4 updateCamera(RzContext* ctx);
Mat4 lookAt(Vec3 eye, Vec3 at, Vec3 up = { 0.0f, 1.0f, 0.0f });

// rz_shadow.cpp
bool createShadowMaps(RzContext* ctx);
void destroyShadowMaps(RzContext* ctx);
void renderShadowMaps(RzContext* ctx);
void initShadowUniforms(GLuint program, ShadowUniforms& u);
void markTerrainShadowAll(RzContext* ctx);                     // mapa 0 inteiro no próximo frame
void markTerrainShadowBox(RzContext* ctx, Vec3 lo, Vec3 hi);   // só a caixa (mundo), acumulando
void bindShadowMaps(const RzContext* ctx, const ShadowUniforms& u);

// rz_border.cpp
bool  createBorder(RzContext* ctx);
void  destroyBorder(RzContext* ctx);
void  buildBorder(RzContext* ctx);                 // carga, no fim de buildTerrainMesh
void  rebuildSkirtMesh(RzContext* ctx);            // só a malha da continuação (blocos)
void  moveSkirtEdgeVertex(RzContext* ctx, int32_t gc, int32_t gr);   // ponto da borda mudou: só o y
void  setExtendedHeight(RzContext* ctx, int32_t gc, int32_t gr, float height);   // ponto do mapa
uint8_t skirtTile(const RzContext* ctx, int32_t c, int32_t r);   // bloco de uma célula da continuação
float extendedGroundHeight(const RzContext* ctx, float x, float z);
void  drawSkirt(const RzContext* ctx);             // programa do terreno em uso
void  drawSkirtDepth(const RzContext* ctx);        // passe de sombra
void  drawWall(const RzContext* ctx, const Mat4& viewProj);

// rz_wheels.cpp
void freeWheels(Object& o);
void prepareWheels(RzContext* ctx, Object& o);
void drawWheelsDepth(const RzContext* ctx, const Object& o);
void drawWheels(RzContext* ctx, const Mat4& viewProj);
void drawWheelFaces(RzContext* ctx, const Mat4& viewProj);   // discos, com a textura do objeto

// rz_glass.cpp
bool createGlassProgram(RzContext* ctx);
void freeGlass(Object& o);
void drawGlass(RzContext* ctx, const Mat4& viewProj);

// rz_sprites.cpp
bool createSprites(RzContext* ctx);
void destroySprites(RzContext* ctx);
void setSpritePalette(RzContext* ctx, const uint8_t* paletteRGB);   // nullptr: rampa de cinza
void drawSprites(RzContext* ctx, const Mat4& viewProj);

// Erros (rzGetError): guarda o primeiro erro de uma função exportada. Usado
// em todo return de erro delas: return recordError(ctx, "rzX", RZ_ERR_...);
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
GLuint linkProgram(const char* vertexSource, const char* fragmentSource);
// Shaders de rz_shaders.h (#embed: bytes terminados em 0)
inline GLuint linkProgram(const unsigned char* vertexSource, const unsigned char* fragmentSource) {
    return linkProgram(reinterpret_cast<const char*>(vertexSource), reinterpret_cast<const char*>(fragmentSource));
}

// rz_render.cpp: neblina (programa já em uso)
void initFogUniforms(GLuint program, FogUniforms& u);
void bindFog(const RzContext* ctx, const FogUniforms& u);

// rz_render.cpp
bool createRenderer(RzContext* ctx);
void destroyRenderer(RzContext* ctx);
void renderFrame(RzContext* ctx);
// Mat4 (linha a linha) para um uniform mat4 (coluna a coluna, sem o
// transpose que o WebGL não tem); count até kShadowMaps
inline void setMatrices(GLint loc, const Mat4* m, int32_t count) {
    float columns[16 * kShadowMaps];
    for (int32_t k = 0; k < count; ++k)
        for (int32_t r = 0; r < 4; ++r)
            for (int32_t c = 0; c < 4; ++c) columns[16 * k + 4 * c + r] = m[k].e[4 * r + c];
    glUniformMatrix4fv(loc, count, GL_FALSE, columns);
}
inline void setMatrix(GLint loc, const Mat4& m) { setMatrices(loc, &m, 1); }

// Texel RGBA8 (em memória R, G, B, A) das texturas, a partir de 0xRRGGBB e do alfa
inline uint32_t rgbaTexel(uint32_t rgb, uint32_t alpha) {
    return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16) | (alpha << 24);
}

// rz_texture.cpp: texturas 2D quadradas dos objetos (mipmaps na CPU)
GLuint uploadSquareTexture(const uint32_t* rgba, int32_t side);
void   applyFilter2D(GLuint texture, int32_t side);
bool   createFallbackTexture(RzContext* ctx);
int32_t mipLevels(int32_t side);   // log2(side) + 1

// rz_object.cpp
bool objectInRange(const RzContext* ctx, const Object& o);   // não está todo além da neblina
void prepareObjects(RzContext* ctx);          // reenvia os VBOs alterados
// passe da sombra (programa já ligado): onlyId >= 0 desenha só ele; skipId >= 0 pula ele
void drawObjectsDepth(RzContext* ctx, const Mat4& lightViewProj, int32_t onlyId, int32_t skipId);
void drawObjects(RzContext* ctx, const Mat4& viewProj);
void destroyAllObjects(RzContext* ctx);
void markObjectsDirty(RzContext* ctx);        // a paleta das cores sólidas mudou: cantos a reenviar

} // namespace rz
