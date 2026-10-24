#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;   // no vertex shader o padrão é lowp: posições quantizadas
// Passe da sombra dos objetos: a posição vem da textura das posições (como em
// object.vert); as linhas trazem a posição direta (vértice -1).
layout(location = 0) in vec3 aPosition;
layout(location = 3) in ivec3 aPolygon;   // vértice do objeto (-1: usa aPosition), polígono
uniform mat4 uLightViewProj;
uniform sampler2D uPositions;            // kRowTexels texels por linha (rz_internal.h)
void main() {
    vec3 position = aPolygon.x >= 0 ? texelFetch(uPositions, ivec2(aPolygon.x % 256, aPolygon.x / 256), 0).xyz : aPosition;
    gl_Position = uLightViewProj * vec4(position, 1.0);
}
