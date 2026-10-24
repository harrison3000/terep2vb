#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArrayShadow;
flat in float vLight;
flat in vec4 vColor;                      // cor sólida; alfa: 0 texturizado, 0,5 buraco, 1 cor sólida
in vec2 vUV;
in vec3 vShadow0, vShadow1, vShadow2;
in vec3 vWorld;
in float vNdotL;

// Terminador: com a luz rasante ou por trás (n · l perto de 0 ou negativo) a
// face fica na sombra de vez, em vez de alternar luz/sombra pela resolução
// do mapa; a luz direta ali já é quase zero, então a troca é suave.
uniform float uShadowTerminator;

// Neblina: smoothstep da distância a uFogOrigin entre uFogStart e uFogEnd, na
// cor uFogColor (a de fundo).
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogColor;
uniform vec3  uEye;
uniform vec3  uFogOrigin;   // olho (seguindo) ou centro do mapa (visão geral)

uniform sampler2D       uTexture;
uniform sampler2DArrayShadow uShadow;     // as três cascatas, uma por camada
uniform vec3  uCascade;                   // fim da cascata 0, fim da 1, faixa de transição (mundo)
uniform int   uCascadeOnlyFar;            // visão geral: só a cascata 2
uniform float uShadowLight;               // luz na sombra (kShadowLight)

out vec4 fragColor;

// Sombra: 1 iluminado, 0 na sombra. Fora da caixa de um mapa, ele não sombreia.
// sampler2DArrayShadow com GL_LINEAR: PCF 2x2 do hardware. textureGrad com
// derivadas zero (nível 0): o mapa não tem mipmap e a leitura fica dentro de
// um if (sem derivadas; o GLSL ES não tem textureLod para array shadow).
float shadowLit(float cascade, vec3 c) {
    if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
    return textureGrad(uShadow, vec4(c.xy, cascade, c.z), vec2(0.0), vec2(0.0));   // nível 0
}

// Cascatas (rz_shadow.cpp): escolhe pela distância ao olho; numa faixa de
// uCascade.z antes de cada divisa, mistura as duas vizinhas. Visão geral: só a 2.
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

// Fator de neblina (0 limpo, 1 só neblina)
float fogFactor() {
    if (uFogOn == 0) return 0.0;
    return smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin));
}


void main() {
    // Derivadas antes de qualquer desvio: a neblina sai cedo em parte dos pixels
    vec2 dUVx = dFdx(vUV), dUVy = dFdy(vUV);
    float fog = fogFactor();
    if (fog >= 1.0) {
        fragColor = vec4(uFogColor, 0.0);
        return;
    }
    vec3 texel;
    if (vColor.a < 0.25) {                                   // texturizado
        vec4 tex = textureGrad(uTexture, vUV, dUVx, dUVy);   // nearest no nível, linear entre níveis
        if (tex.a < 0.5) discard;                            // índice 255 do PCX: transparente
        texel = tex.rgb;
    } else if (vColor.a < 0.75) {
        discard;                                             // a cor sólida 255: transparente
    } else {
        texel = vColor.rgb;                                  // cor sólida: a mesma luz do texel
    }
    float lit = shadowTerm() * smoothstep(0.0, uShadowTerminator, vNdotL);
    vec3 color = texel * mix(min(uShadowLight, vLight), vLight, lit);
    fragColor = vec4(mix(color, uFogColor, fog), 0.0);
}
