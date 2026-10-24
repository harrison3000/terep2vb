#version 300 es
// Um sprite por instância (rz_sprites.cpp): o quad sai de gl_VertexID, seis
// vértices, dois triângulos
layout(location = 0) in vec4 aCenterRadius;
layout(location = 1) in ivec2 aFrameType;   // quadro do ruído, tipo
layout(location = 2) in vec4 aColor;        // B, G, R, 0 normalizados
layout(location = 3) in float aOpacity;     // fumaça: multiplicador do ruído
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
