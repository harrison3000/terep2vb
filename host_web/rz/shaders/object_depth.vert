#version 300 es
// Passe da sombra dos objetos: a posição vem do bloco do objeto (como em
// object.vert); as linhas trazem a posição direta (vértice -1).
layout(location = 0) in vec3 aPosition;
layout(location = 3) in ivec3 aPolygon;   // vértice do objeto (-1: usa aPosition), polígono
uniform mat4 uLightViewProj;
// O bloco do objeto (rz_internal.h: kMaxObjectVertices, kMaxObjectIndices)
layout(std140) uniform ObjectData {
    vec4  uPositions[256];                // posições no mundo dos vértices do objeto
    ivec4 uIndices[768];                  // índices dos polígonos, quatro por ivec4
};
void main() {
    vec3 position = aPolygon.x >= 0 ? uPositions[aPolygon.x].xyz : aPosition;
    gl_Position = uLightViewProj * vec4(position, 1.0);
}
