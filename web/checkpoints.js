// A map's checkpoints, with the OpenGL renderer only: read from and written
// to their text file, raced through (the next ones and the ones after them
// drawn over the renderer's frame, in its WebGL context, with its depth
// buffer, after rzRender; the lap, the time over the text's canvas), and
// made in the editor, a free camera flying over the map (editCheckpoints()
// in the console), which shows them all.
//
// The file:
//     laps 3
//     target_time 1m45s
//
//     1 : 12.45, 34.23, 3.0
//     2*: 18.45, 30.23, 5.0
//     2*: 32.45, 30.23, 5.0
//     3 : 50.00, 40.22, 3.0
//     4F: 61.50, 44.00, 2.50, 90.0, 4.0
// a checkpoint a line: the order it is passed in, * where the way forks (any
// of the checkpoints of that number will do), its centre (x, y: a column and
// a row of the map) and its diameter, all in tiles; the height is the map's.
// With F, a ring of fire up in the air (for the jumps): its centre (x, y,
// and its height over the ground there), the way it is gone through (degrees
// on the map: 0 towards +x, 90 towards +y) and its diameter.

// ---------------------------------------------------------------------------
// The file

const NUMBER = String.raw`[-+]?(?:\d+\.?\d*|\.\d+)`;
const LINE = new RegExp(String.raw`^(\d+)\s*([*Ff]*)\s*:\s*(${NUMBER}(?:\s*,\s*${NUMBER})*)$`);

// '1m45s', '1m', '45.5s', '105' (seconds) -> seconds, or NaN
function parseTime(text) {
    const m = /^(?:(\d+)m)?\s*(?:(\d+(?:\.\d*)?)s?)?$/.exec(text.trim());
    return m && (m[1] || m[2]) ? +(m[1] || 0) * 60 + +(m[2] || 0) : NaN;
}

function formatTime(seconds) {
    const m = Math.floor(seconds / 60), s = +(seconds - m * 60).toFixed(2);
    return (m ? m + 'm' : '') + (s || !m ? s + 's' : '');
}

// Inside the map's wall (x and y from 0 to 255): where checkpoints can be
const insideWall = (x, y) => x >= 0 && x <= 255 && y >= 0 && y <= 255;

// A checkpoint's line (LINE's match) -> its point, or null (also out of the wall).
function point(m) {
    const fork = m[2].includes('*'), fire = /f/i.test(m[2]), v = m[3].split(',').map(Number);
    if (v.length !== (fire ? 5 : 3) || !(v[v.length - 1] > 0) || !insideWall(v[0], v[1]))
        return null;
    return fire ? { n: +m[1], fork, fire, x: v[0], y: v[1], z: v[2], angle: v[3], d: v[4] }
                : { n: +m[1], fork, fire, x: v[0], y: v[1], d: v[2] };
}

// The text -> { laps, targetTime (seconds, or null), points, errors: [text] }; a point
// { n, fork, fire, x, y, d } and, with fire, z (over the ground) and angle (degrees).
// Blank lines and lines starting with # are skipped.
export function parseCheckpoints(text) {
    const data = { laps: 1, targetTime: null, points: [], errors: [] };
    text.split(/\r?\n/).forEach((raw, k) => {
        const line = raw.trim();
        let m;
        if (!line || line.startsWith('#'))
            return;
        if ((m = /^laps\s+(\d+)$/.exec(line)))
            data.laps = +m[1];
        else if ((m = /^target_time\s+(.+)$/.exec(line)) && !isNaN(parseTime(m[1])))
            data.targetTime = parseTime(m[1]);
        else if ((m = LINE.exec(line)) && (m = point(m)))
            data.points.push(m);
        else
            data.errors.push(`line ${k + 1}: ${line}`);
    });
    return data;
}

export function formatCheckpoints(data) {
    const width = Math.max(1, ...data.points.map((p) => String(p.n).length));
    const suffix = (p) => (p.fork ? '*' : '') + (p.fire ? 'F' : '');
    const swidth = Math.max(1, ...data.points.map((p) => suffix(p).length));
    const lines = [`laps ${data.laps}`];
    if (data.targetTime !== null)
        lines.push(`target_time ${formatTime(data.targetTime)}`);
    lines.push('');
    for (const p of [...data.points].sort((a, b) => a.n - b.n))
        lines.push(`${String(p.n).padStart(width)}${suffix(p).padEnd(swidth)}: ${p.x.toFixed(2)}, ${p.y.toFixed(2)}, ` +
                   (p.fire ? `${p.z.toFixed(2)}, ${p.angle.toFixed(1)}, ` : '') + p.d.toFixed(1));
    return lines.join('\n') + '\n';
}

// ---------------------------------------------------------------------------
// The map's ground

const HEIGHT_SCALE = 16 / 256;      // tiles per unit of a height (gl_renderer.cpp's rzSetTerrainScale)

// The ground's height (tiles) at (x, y), on the renderer's triangles: each
// quad split along its (1, 0)-(0, 1) diagonal (rz_terrain.cpp).
function groundHeight(heights, x, y) {
    x = Math.min(Math.max(x, 0), 254.9999);
    y = Math.min(Math.max(y, 0), 254.9999);
    const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy, i = iy * 256 + ix;
    const h00 = heights[i], h10 = heights[i + 1], h01 = heights[i + 256], h11 = heights[i + 257];
    const h = fx + fy <= 1 ? h00 + (h10 - h00) * fx + (h01 - h00) * fy
                           : h11 + (h01 - h11) * (1 - fx) + (h10 - h11) * (1 - fy);
    return h * HEIGHT_SCALE;
}

// Where a ray from eye along dir (tiles, x y z up) meets the ground, within
// reach tiles of it, or null.
function hitGround(heights, eye, dir, reach = 200) {
    const above = (t) => eye[2] + dir[2] * t - groundHeight(heights, eye[0] + dir[0] * t, eye[1] + dir[1] * t);
    let t0 = 0;
    for (let t = 0.05; t <= reach; t0 = t, t += 0.05 + t * 0.01) {
        if (above(t) > 0)
            continue;
        for (let k = 0; k < 20; k++) {          // bisection between the last point above and this one
            const mid = (t0 + t) / 2;
            if (above(mid) > 0) t0 = mid; else t = mid;
        }
        return [eye[0] + dir[0] * t, eye[1] + dir[1] * t];
    }
    return null;
}

// ---------------------------------------------------------------------------
// Drawing, in the style of the renderer's wall (its shader in shaders.js),
// fading in the fog as the rest:
//   a disc on the ground for each checkpoint: translucent, a band of colour
//   and a white rim at the edge;
//   in a race, a cylinder over the next checkpoints (and a lower one over
//   those after them): translucent, fading upwards, stripes going up, and
//   puffs of smoke rising around its foot;
//   and floating in the next ones an arrow standing on its edge (turned 90
//   degrees around where it points), pointing to the ones after, or,
//   before a fork, a question mark facing the camera; on the last of the
//   race, a chequered flag waving, facing the camera;
//   a ring of fire for the checkpoints up in the air: flames flickering
//   out of it, added to what is behind (they glow).
// The discs are also projected from above on the cars, by the renderer
// (rzSetObjectDecals): a car in a checkpoint has its drawing on it.

const KIND_DISC = 0, KIND_CYLINDER = 1, KIND_ARROW = 2, KIND_QUESTION = 3, KIND_SMOKE = 4, KIND_FLAG = 5,
      KIND_FIRE = 6;
const FLOATS = 6;                   // a vertex: aPosition, aLocal, aSeed

const VERTEX = `#version 300 es
layout(location = 0) in vec3 aPosition;     // world: (x, height, y); billboards: their foot
layout(location = 1) in vec2 aLocal;        // disc: tiles from the centre; cylinder: around (0..1),
                                            // tiles over the ground; arrow: light, tiles
                                            // along it; billboards: the corner (-1..1); fire:
                                            // around (0..1), tiles out of the ring
layout(location = 2) in float aSeed;        // smoke: the puff's (0..1); arrow: tiles across (up)
uniform mat4 uViewProj;
uniform int   uKind;
uniform vec3  uShift;                       // tiles (the arrow's and the question mark's bobbing)
uniform float uSize;                        // the question mark's half size, tiles
uniform float uTime;
uniform vec3  uRight, uUp;                  // the camera's (billboards)
out vec2 vLocal;
out vec3 vWorld;
out float vFade;                            // smoke: the puff's life (0..1); arrow: aSeed
void main() {
    vLocal = aLocal;
    vWorld = aPosition + uShift;
    vFade = aSeed;
    if (uKind == 3 || uKind == 5) {
        vWorld += (uRight * aLocal.x + uUp * aLocal.y) * uSize;
    } else if (uKind == 4) {                // rises, grows and drifts, again and again
        float t = fract(uTime * 0.35 + aSeed);
        float size = 0.35 + 0.6 * t;
        vWorld += vec3(sin(aSeed * 40.0 + uTime) * 0.15 * t, 1.6 * t + 0.1, cos(aSeed * 57.0 + uTime) * 0.15 * t)
                + (uRight * aLocal.x + uUp * aLocal.y) * size;
        vFade = t;
    }
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}`;

const FRAGMENT = `#version 300 es
precision highp float;
precision highp int;                        // as the vertex shader's (uKind is in both)
in vec2 vLocal;
in vec3 vWorld;
in float vFade;
uniform int   uKind;                        // KIND_*
uniform float uRadius;                      // disc
uniform float uHeight;                      // cylinder
uniform float uTime;                        // seconds
uniform vec3  uColor;
uniform float uAlpha;                       // the whole shape's (highlight, preview)
uniform sampler2D uQuestion;                // the question mark: white, outlined
uniform int   uFogOn;
uniform float uFogStart, uFogEnd;
uniform vec3  uFogOrigin, uFogColor;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    vec3 color;
    float alpha;
    if (uKind == 0) {
        float r  = length(vLocal) / uRadius;
        float aa = fwidth(r) + 0.002;
        if (r > 1.0 + aa) discard;
        float band = smoothstep(0.78 - aa, 0.78 + aa, r);
        float rim  = smoothstep(0.92 - aa, 0.92 + aa, r);
        float edge = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);
        color = uColor * 0.7;
        alpha = 0.22;
        color = mix(color, uColor, band);        alpha = mix(alpha, 0.85, band);
        color = mix(color, vec3(1.0), rim);      alpha = mix(alpha, 0.90, rim);
        alpha *= edge;
    } else if (uKind == 1) {
        float h = max(vLocal.y, 0.0);
        float fade = pow(clamp(1.0 - h / uHeight, 0.0, 1.0), 1.5);
        float stripe = 0.5 + 0.5 * sin((h * 0.8 - uTime * 1.2) * 6.2831853);
        float foot = 1.0 - smoothstep(0.0, 0.5, h);         // brighter where it meets the ground
        color = mix(uColor, vec3(1.0), 0.25 * stripe + 0.3 * foot);
        alpha = (0.16 + 0.18 * stripe + 0.35 * foot) * fade;
    } else if (uKind == 2) {                // waves shaped like its head, going where it points
        float wave = 0.5 + 0.5 * sin((vLocal.y + abs(vFade) * 0.8 - uTime * 1.5) * 6.2831853 / 0.7);
        color = mix(uColor, vec3(1.0), 0.1 + 0.4 * wave * wave) * vLocal.x;
        alpha = 0.9;
    } else if (uKind == 3) {
        vec4 t = texture(uQuestion, vLocal * vec2(0.5, -0.5) + 0.5);
        color = mix(uColor, vec3(1.0), t.r);     // the canvas's white glyph, its outline the colour
        alpha = t.a * 0.95;
    } else if (uKind == 5) {                // a pole on the left, the cloth waving from it
        vec2 p = vLocal;
        float pole = step(-0.95, p.x) * step(p.x, -0.85);
        float u = (p.x + 0.85) / 1.8;                      // 0..1 along the cloth
        float phase = u * 7.0 - uTime * 5.0;
        float v = (p.y - 0.1 - sin(phase) * 0.07 * u) / 0.85;   // 0..1 up the cloth
        float cloth = step(0.0, u) * step(u, 1.0) * step(0.0, v) * step(v, 1.0);
        if (pole < 0.5 && cloth < 0.5) discard;
        float white = mod(floor(u * 6.0) + floor(v * 4.0), 2.0);
        color = pole > 0.5 ? vec3(0.75) : vec3(0.08 + 0.87 * white) * (0.8 + 0.2 * cos(phase));
        alpha = 1.0;
    } else if (uKind == 6) {                // the ring burning, flames going out of it
        float off = vLocal.y;
        float a = vLocal.x * 64.0;                         // around: 64 noise cells (the seam is hard to see)
        float n = 0.6 * noise(vec2(a, off * 3.0 - uTime * 4.0)) + 0.4 * noise(vec2(a * 2.3, off * 6.0 - uTime * 7.0));
        float core = 1.0 - smoothstep(0.05, 0.2, abs(off));
        float flame = n * (1.0 - smoothstep(-0.1, 0.9, off)) * smoothstep(-0.4, -0.15, off);
        float heat = clamp(core * (0.7 + 0.3 * n) + flame * 1.3 - 0.25 * (1.0 - n), 0.0, 1.2);
        if (heat <= 0.01) discard;
        color = heat < 0.5 ? mix(vec3(0.6, 0.05, 0.0), vec3(1.0, 0.45, 0.05), heat * 2.0)
                           : mix(vec3(1.0, 0.45, 0.05), vec3(1.0, 0.95, 0.6), min(heat * 2.0 - 1.0, 1.0));
        color = mix(color, uColor, 0.15);
        alpha = clamp(heat * 1.3, 0.0, 1.0);
    } else {
        float r = length(vLocal);
        float n = noise(vLocal * 2.5 + vec2(vFade * 3.0, vWorld.x * 7.0));
        color = mix(vec3(0.85), uColor, 0.15);
        alpha = (1.0 - smoothstep(0.2, 1.0, r)) * (0.4 + 0.6 * n) * sin(3.14159 * vFade) * 0.45;
    }
    alpha *= uAlpha;
    if (uFogOn != 0) {
        float fog = smoothstep(uFogStart, uFogEnd, length(vWorld - uFogOrigin));
        color = mix(color, uFogColor, fog);
        alpha *= 1.0 - fog;
    }
    fragColor = vec4(color, alpha);
}`;

const COLORS = {
    normal:  [0.92, 0.10, 0.07],    // the wall's red
    fork:    [0.95, 0.55, 0.08],
    aimed:   [1.00, 0.90, 0.30],
    preview: [1.00, 1.00, 1.00],
};
const LIFT = 0.03;                  // tiles over the ground (and a polygon offset): no fighting with it
const STEP = 0.25;                  // tiles between the rings of a disc
const SEGMENTS = 48;
const SINK = 0.3;                   // tiles a cylinder goes into the ground (no gap on a slope)

// The arrow, flat, pointing along +a (a, b in tiles; centre at the origin):
// its outline, counter-clockwise, and its face's triangles (indices); stood
// on its edge (b up), so that its faces show from the sides.
const ARROW_OUTLINE = [[0.75, 0], [0, 0.55], [0, 0.22], [-0.75, 0.22], [-0.75, -0.22], [0, -0.22], [0, -0.55]];
const ARROW_TOP = [[0, 1, 6], [2, 3, 4], [2, 4, 5]];
const ARROW_SCALE = 1.4, ARROW_THICKNESS = 0.18;
const FLOAT_HEIGHT = 2.5;           // tiles over the ground: the middle of the arrow, of the question mark
const QUESTION_SIZE = 0.9;          // tiles, half the question mark's side
const FLAG_SIZE = 1.2;              // tiles, half the flag's side (with its pole)
const SMOKE_SPACING = 0.6;          // tiles around the foot of a cylinder between puffs
const FIRE_IN = 0.4, FIRE_OUT = 0.9;   // tiles: the fire inside the ring, its flames out of it
const QUESTION_TEXTURE_UNIT = 7;    // one the renderer does not use

// The question mark's texture: a white "?" outlined (the outline is
// coloured in the shader), from a 2D canvas.
function questionTexture(gl) {
    const c = document.createElement('canvas');
    c.width = c.height = 128;
    const g = c.getContext('2d');
    g.font = 'bold 112px system-ui, sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.lineJoin = 'round';
    g.lineWidth = 18;
    g.strokeStyle = '#000';             // red channel 0: the colour
    g.strokeText('?', 64, 68);
    g.fillStyle = '#fff';
    g.fillText('?', 64, 68);
    const t = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0 + QUESTION_TEXTURE_UNIT);
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, c);
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.activeTexture(gl.TEXTURE0);
    return t;
}

// Shapes, each with its own vertices in one buffer: discs, cylinders and
// smoke ({ kind, x, y, d, height }), arrows ({ kind, x, y, to: [x, y] }),
// question marks and flags ({ kind, x, y }), rings of fire ({ kind, x, y,
// z, angle, d }); arrows, question marks and flags at h over the ground
// (FLOAT_HEIGHT without it).
// The vertices of a disc of diameter d (Markers.set)
const discVertices = (d) => Math.max(2, Math.ceil((d / 2 + 0.05) / STEP)) * SEGMENTS * 6;

// Another Markers (shared) can lend its program and texture: only the
// buffer is its own.  vertices: how many to make room for at first (set
// grows it if needs be).
class Markers {
    constructor(gl, vertices, shared = null) {
        this.gl = gl;
        this.scratch = new Float32Array(FLOATS * vertices);    // set's vertices
        this.shapes = [];           // the shapes set, each with its vertices: first, count
        this.vao = gl.createVertexArray();
        this.buffer = gl.createBuffer();
        gl.bindVertexArray(this.vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
        gl.vertexAttribPointer(0, 3, gl.FLOAT, false, FLOATS * 4, 0);
        gl.vertexAttribPointer(1, 2, gl.FLOAT, false, FLOATS * 4, 12);
        gl.vertexAttribPointer(2, 1, gl.FLOAT, false, FLOATS * 4, 20);
        gl.enableVertexAttribArray(0);
        gl.enableVertexAttribArray(1);
        gl.enableVertexAttribArray(2);
        gl.bindVertexArray(null);
        gl.bindBuffer(gl.ARRAY_BUFFER, null);
        if (shared) {
            this.program = shared.program;
            this.u = shared.u;
            this.question = shared.question;
            return;
        }
        const shader = (type, text) => {
            const s = gl.createShader(type);
            gl.shaderSource(s, text);
            gl.compileShader(s);
            if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
                throw new Error('checkpoint shader: ' + gl.getShaderInfoLog(s));
            return s;
        };
        const p = this.program = gl.createProgram();
        gl.attachShader(p, shader(gl.VERTEX_SHADER, VERTEX));
        gl.attachShader(p, shader(gl.FRAGMENT_SHADER, FRAGMENT));
        gl.linkProgram(p);
        if (!gl.getProgramParameter(p, gl.LINK_STATUS))
            throw new Error('checkpoint shader: ' + gl.getProgramInfoLog(p));
        this.u = {};
        for (const name of ['uViewProj', 'uShift', 'uSize', 'uRight', 'uUp', 'uKind', 'uRadius', 'uHeight', 'uTime',
                            'uColor', 'uAlpha', 'uQuestion', 'uFogOn', 'uFogStart', 'uFogEnd', 'uFogOrigin', 'uFogColor'])
            this.u[name] = gl.getUniformLocation(p, name);
        this.question = questionTexture(gl);
    }

    // The shapes into the buffer, on the ground (heights).
    set(shapes, heights) {
        let out = this.scratch, n = 0;                  // n floats written
        const put = (v) => {                            // a vertex (FLOATS floats)
            if (n + FLOATS > out.length) {
                const bigger = new Float32Array(out.length * 2);
                bigger.set(out);
                out = this.scratch = bigger;
            }
            out.set(v, n);
            n += FLOATS;
        };
        const ground = (x, y) => groundHeight(heights, x, y);
        const quad = (a, b, c, e) => { put(a); put(b); put(c); put(a); put(c); put(e); };
        // a billboard's four corners at the foot (x, h, y): the shader turns them to the camera
        const billboard = (x, h, y, seed) =>
            quad([x, h, y, -1, -1, seed], [x, h, y, 1, -1, seed], [x, h, y, 1, 1, seed], [x, h, y, -1, 1, seed]);
        this.shapes = shapes.map((shape) => {
            let s = shape;
            const first = n / FLOATS;
            if (s.kind === KIND_DISC) {         // ring by ring, on the ground at each vertex (each once)
                const r = s.d / 2 + 0.05, rings = Math.max(2, Math.ceil(r / STEP));   // a little more: the edge's smoothing
                const grid = [];
                for (let ring = 0; ring <= rings; ring++)
                    for (let seg = 0; seg <= SEGMENTS; seg++) {
                        const a = seg / SEGMENTS * 2 * Math.PI, rr = ring / rings * r;
                        const lx = Math.cos(a) * rr, ly = Math.sin(a) * rr;
                        grid.push([s.x + lx, ground(s.x + lx, s.y + ly) + LIFT, s.y + ly, lx, ly, 0]);
                    }
                const at = (ring, seg) => grid[ring * (SEGMENTS + 1) + seg];
                for (let ring = 0; ring < rings; ring++)
                    for (let seg = 0; seg < SEGMENTS; seg++)
                        quad(at(ring, seg), at(ring + 1, seg), at(ring + 1, seg + 1), at(ring, seg + 1));
            } else if (s.kind === KIND_CYLINDER) {   // the wall, from under the ground at its edge up
                const r = s.d / 2;
                const at = (seg, top) => {
                    const a = seg / SEGMENTS * 2 * Math.PI, x = s.x + Math.cos(a) * r, y = s.y + Math.sin(a) * r;
                    const h = top ? s.height : -SINK;
                    return [x, ground(x, y) + h, y, seg / SEGMENTS, h, 0];
                };
                for (let seg = 0; seg < SEGMENTS; seg++)
                    quad(at(seg, false), at(seg + 1, false), at(seg + 1, true), at(seg, true));
            } else if (s.kind === KIND_ARROW) {      // both faces and the edges, lit by a fixed amount each
                let fx = s.to[0] - s.x, fy = s.to[1] - s.y;
                const len = Math.hypot(fx, fy) || 1;
                fx /= len;
                fy /= len;
                s = { ...s, along: [fx, 0, fy] };       // (world) where it bobs
                const y0 = ground(s.x, s.y) + (s.h ?? FLOAT_HEIGHT);
                const at = ([a, b], c, light) => {      // b: up; c: across, the thickness
                    a *= ARROW_SCALE;
                    b *= ARROW_SCALE;
                    return [s.x + a * fx - c * fy, y0 + b, s.y + a * fy + c * fx, light, a, b];
                };
                const half = ARROW_THICKNESS / 2;
                for (const tri of ARROW_TOP) {
                    for (const i of tri) put(at(ARROW_OUTLINE[i], half, 1));
                    for (const i of tri) put(at(ARROW_OUTLINE[i], -half, 0.85));
                }
                ARROW_OUTLINE.forEach((p, i) => {
                    const q = ARROW_OUTLINE[(i + 1) % ARROW_OUTLINE.length];
                    quad(at(p, -half, 0.7), at(q, -half, 0.7), at(q, half, 0.7), at(p, half, 0.7));
                });
            } else if (s.kind === KIND_QUESTION || s.kind === KIND_FLAG) {
                billboard(s.x, ground(s.x, s.y) + (s.h ?? FLOAT_HEIGHT), s.y, 0);
            } else if (s.kind === KIND_FIRE) {       // a band around the ring, upright, across the way through it
                const r = s.d / 2, rad = s.angle * Math.PI / 180, h = ground(s.x, s.y) + s.z;
                const sx = -Math.sin(rad), sy = Math.cos(rad);   // the ring's sideways, on the map
                const at = (seg, off) => {
                    const a = seg / SEGMENTS * 2 * Math.PI, rr = r + off;
                    const side = Math.cos(a) * rr;
                    return [s.x + sx * side, h + Math.sin(a) * rr, s.y + sy * side, seg / SEGMENTS, off, 0];
                };
                for (let seg = 0; seg < SEGMENTS; seg++)
                    quad(at(seg, -FIRE_IN), at(seg + 1, -FIRE_IN), at(seg + 1, FIRE_OUT), at(seg, FIRE_OUT));
            } else {                                // smoke: puffs around the foot, each its own time
                const r = s.d / 2, n = Math.max(6, Math.round(2 * Math.PI * r / SMOKE_SPACING));
                for (let i = 0; i < n; i++) {
                    const a = (i + 0.5) / n * 2 * Math.PI, x = s.x + Math.cos(a) * r, y = s.y + Math.sin(a) * r;
                    billboard(x, ground(x, y), y, (i * 0.618034) % 1);
                }
            }
            return { ...s, first, count: n / FLOATS - first };
        });
        const gl = this.gl;
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
        gl.bufferData(gl.ARRAY_BUFFER, out, gl.DYNAMIC_DRAW, 0, n);
        gl.bindBuffer(gl.ARRAY_BUFFER, null);
    }

    // Over the frame just drawn, with its camera (view: rzGetView's floats);
    // styles: for each shape set, { color, alpha }.  The renderer's state
    // is left as it sets it up again each frame (no program, no VAO bound,
    // no blending, depth written, texture unit 0 active).
    draw(view, styles) {
        const gl = this.gl, u = this.u, time = performance.now() / 1000;
        // the camera's right and up, for the billboards (from where it looks; up stays up)
        const f = [view[19], view[20], view[21]];
        let r = [-f[2], 0, f[0]];
        const rl = Math.hypot(r[0], r[2]) || 1;
        r = [r[0] / rl, 0, r[2] / rl];
        const up = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];
        gl.useProgram(this.program);
        gl.uniformMatrix4fv(u.uViewProj, false, view.subarray(0, 16));
        gl.uniform1f(u.uTime, time);
        gl.uniform3fv(u.uRight, r);
        gl.uniform3fv(u.uUp, up);
        gl.uniform1i(u.uQuestion, QUESTION_TEXTURE_UNIT);
        gl.activeTexture(gl.TEXTURE0 + QUESTION_TEXTURE_UNIT);
        gl.bindTexture(gl.TEXTURE_2D, this.question);
        gl.uniform1i(u.uFogOn, view[22]);
        gl.uniform3f(u.uFogOrigin, view[23], view[24], view[25]);
        gl.uniform1f(u.uFogStart, view[26]);
        gl.uniform1f(u.uFogEnd, view[27]);
        gl.uniform3f(u.uFogColor, view[28], view[29], view[30]);
        gl.bindVertexArray(this.vao);
        gl.enable(gl.DEPTH_TEST);
        gl.depthMask(false);
        gl.disable(gl.CULL_FACE);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.colorMask(true, true, true, false);
        gl.polygonOffset(-1, -4);
        this.shapes.forEach((s, k) => {
            const style = styles[k];
            if (s.kind === KIND_DISC)
                gl.enable(gl.POLYGON_OFFSET_FILL);
            else
                gl.disable(gl.POLYGON_OFFSET_FILL);
            gl.blendFunc(gl.SRC_ALPHA, s.kind === KIND_FIRE ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
            gl.uniform1i(u.uKind, s.kind);
            gl.uniform1f(u.uRadius, s.d / 2);
            gl.uniform1f(u.uHeight, s.height || 1);
            const bob = 0.25 * Math.sin(time * 3);      // the arrow back and forth, the question mark up and down
            gl.uniform3fv(u.uShift, s.kind === KIND_ARROW ? s.along.map((v) => v * bob)
                                  : s.kind === KIND_QUESTION ? [0, bob, 0] : [0, 0, 0]);
            gl.uniform1f(u.uSize, s.kind === KIND_FLAG ? FLAG_SIZE : QUESTION_SIZE);
            gl.uniform3fv(u.uColor, style.color);
            gl.uniform1f(u.uAlpha, style.alpha);
            gl.drawArrays(gl.TRIANGLES, s.first, s.count);
        });
        gl.disable(gl.POLYGON_OFFSET_FILL);
        gl.colorMask(true, true, true, true);
        gl.disable(gl.BLEND);
        gl.depthMask(true);
        gl.bindVertexArray(null);
        gl.useProgram(null);
        gl.bindTexture(gl.TEXTURE_2D, null);
        gl.activeTexture(gl.TEXTURE0);
    }
}

// Where a world point (x, height, y) falls on a canvas w x h, or null behind
// the camera.
function project(view, x, h, y, w, hgt) {
    const m = view;
    const cx = m[0] * x + m[4] * h + m[8] * y + m[12];
    const cy = m[1] * x + m[5] * h + m[9] * y + m[13];
    const cw = m[3] * x + m[7] * h + m[11] * y + m[15];
    if (cw <= 0.01)
        return null;
    return [(cx / cw * 0.5 + 0.5) * w, (0.5 - cy / cw * 0.5) * hgt];
}

// ---------------------------------------------------------------------------
// The checkpoints of the game being played, and the editor

const EMPTY = 'laps 3\n\n';
const DEFAULT_DIAMETER = 3;
const SPEED = 10;                   // tiles a second flying (x4 with Shift)
const TURN = 0.0025;                // radians a pixel of the mouse
const KEY_TURN = 1.6;               // radians a second with the arrows
const MAP_END = 255;                // tiles: the wall, at 0 and here on both axes
const EYE_OUT = 10, EYE_LOW = 5, EYE_OVER = 1, EYE_HIGH = 50;   // tiles: where the camera can go (see checkpointsBeforeFrame)

let wasm = null, glCanvas = null, markers = null;
let previewMarkers = null;          // the editor's preview, apart: it moves every frame
let mapName = 'checkpoints';
let data = parseCheckpoints(EMPTY);
let heights = null;                 // the map's, a copy (from the module's memory, which can grow)
let dirty = true;                   // the shapes' buffer is to be made again

// The editor's state: the free camera (the game's axes: x column, y row, z
// up), the keys held, what is aimed at (the camera's centre on the ground).
const ed = {
    on: false, eye: [128, 128, 20], yaw: 0, pitch: -0.5, keys: new Set(),
    aim: null, aimed: -1, diameter: DEFAULT_DIAMETER, panel: null, text: null, status: null,
};

// A game starts with the OpenGL renderer (its canvas, the module's exports,
// the map's name, the text of its checkpoints file or null).
export function startCheckpoints(module, canvas, name, text) {
    wasm = module;
    glCanvas = canvas;
    mapName = name || 'checkpoints';
    data = parseCheckpoints(text ?? EMPTY);
    if (data.errors.length)
        console.warn(`${mapName} checkpoints:`, data.errors);
    const p = wasm.web_heightmap();
    heights = new Uint8Array(wasm.memory.buffer, p, 256 * 256).slice();
    try {
        // room for 30 checkpoints of 5 tiles in the editor; the preview, one
        markers ??= new Markers(canvas.getContext('webgl2'), 30 * discVertices(5));
        previewMarkers ??= new Markers(canvas.getContext('webgl2'), discVertices(DEFAULT_DIAMETER * 2), markers);
    } catch (e) {
        console.error(e);
        markers = null;
    }
    startRace();
}

export function stopCheckpoints() {
    if (ed.on)
        closeEditor();
    wasm = null;
}

export const editingCheckpoints = () => ed.on;
// Whether the text's canvas has something of this module's every frame
export const checkpointsOverlayOn = () => ed.on || race.on;

// ---------------------------------------------------------------------------
// The race: the checkpoints in their order, lap after lap (a lap ends at the
// last number), timed from the start of the game (in physics ticks, which
// stop with the editor); car 1, the player's, passes a checkpoint when one
// of its wheels goes into the circle of one of the next number, or through
// the ring, when it is a ring of fire.

const TICKS_PER_SECOND = 120;
const NEXT_HEIGHT = 6, AFTER_HEIGHT = 3;   // tiles: the cylinders of the next checkpoints, of the ones after
const AFTER_ALPHA = 0.6;

const race = {
    on: false,
    numbers: [],                    // the checkpoints' numbers, in order (one lap)
    step: 0, total: 0,              // checkpoints passed, of laps x numbers
    start: 0, finish: null,         // in ticks (web_ticks)
    last: null,                     // where the car's wheels were at the last frame
};

// From the start, with the checkpoints as they are now.
function startRace() {
    race.numbers = [...new Set(data.points.map((p) => p.n))].sort((a, b) => a - b);
    race.total = race.numbers.length * Math.max(1, data.laps);
    race.on = race.total > 0;
    race.step = 0;
    race.start = wasm.web_ticks();
    race.finish = null;
    race.last = null;
    dirty = true;
}

// The checkpoints to pass at a step (one, or the forks of a number); none
// after the last.
const checkpointsAt = (step) => step < race.total
    ? data.points.filter((p) => p.n === race.numbers[step % race.numbers.length]) : [];

// The distance from the point p to the segment a-b (2D).
function segmentDistance(a, b, p) {
    const dx = b[0] - a[0], dy = b[1] - a[1], len2 = dx * dx + dy * dy;
    const t = len2 ? Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2)) : 0;
    return Math.hypot(a[0] + dx * t - p[0], a[1] + dy * t - p[1]);
}

// Whether the way from a to b (x, y, z: tiles) goes through the ring of fire
// p: across its plane, within its circle.
function throughRing(a, b, p) {
    const rad = p.angle * Math.PI / 180, nx = Math.cos(rad), ny = Math.sin(rad);
    const cz = groundHeight(heights, p.x, p.y) + p.z;
    const da = (a[0] - p.x) * nx + (a[1] - p.y) * ny, db = (b[0] - p.x) * nx + (b[1] - p.y) * ny;
    if (da === db || (da > 0) === (db > 0) && db !== 0)
        return false;
    const t = da / (da - db);
    const q = [a[0] + (b[0] - a[0]) * t - p.x, a[1] + (b[1] - a[1]) * t - p.y, a[2] + (b[2] - a[2]) * t - cz];
    return Math.hypot(...q) <= p.d / 2;
}

// One frame of the race: has a wheel of the car, on its way from where it
// was, gone into one of the next checkpoints?  (Without wheels, the
// reference point.)
function updateRace() {
    if (!race.on || ed.on || race.finish !== null || !wasm.web_car_count())
        return;
    const n = wasm.web_car_wheel_count(0), at = [];
    for (let k = 0; k < n; k++)
        at.push([wasm.web_car_wheel_x(0, k), wasm.web_car_wheel_y(0, k), wasm.web_car_wheel_z(0, k)]);
    if (!n)
        at.push([wasm.web_car_x(0), wasm.web_car_y(0), wasm.web_car_z(0)]);
    const entered = (p) => at.some((w, k) => {
        const from = race.last?.[k];
        return p.fire ? from !== undefined && throughRing(from, w, p)
                      : segmentDistance(from ?? w, w, [p.x, p.y]) <= p.d / 2;
    });
    if (checkpointsAt(race.step).some(entered)) {
        race.step++;
        dirty = true;
        if (race.step === race.total)
            race.finish = wasm.web_ticks();
    }
    race.last = at;
}

const raceSeconds = () => ((race.finish ?? wasm.web_ticks()) - race.start) / TICKS_PER_SECOND;

// 83.456 -> '1:23.45'
function clock(seconds) {
    const m = Math.floor(seconds / 60), s = seconds - m * 60;
    return `${m}:${s < 10 ? '0' : ''}${(Math.floor(s * 100) / 100).toFixed(2)}`;
}

// Where the arrow, the question mark or the flag of a checkpoint floats:
// tiles over the ground (over a ring of fire, above it).
const floatHeight = (p) => (p.fire ? p.z + p.d / 2 + FIRE_OUT + 1 : FLOAT_HEIGHT);

// The shape of a ring of fire
const fireShape = (p) => ({ kind: KIND_FIRE, x: p.x, y: p.y, z: p.z, angle: p.angle, d: p.d });

// The shapes of the race, and their styles: the next checkpoints, a disc
// and a tall cylinder with smoke at its foot (or their ring of fire) and,
// floating in it (over the ring), an arrow to the nearest of the ones
// after, or a question mark when the way forks there (more than one after,
// and this one not itself a fork); those, a disc and a low cylinder with
// smoke (or the ring), fainter.
function raceShapes() {
    const next = checkpointsAt(race.step), after = checkpointsAt(race.step + 1);
    const shapes = [], styles = [];
    const add = (shape, p, alpha) => {
        shapes.push(shape);
        styles.push({ color: p.fork ? COLORS.fork : COLORS.normal, alpha });
    };
    for (const [list, height, alpha] of [[next, NEXT_HEIGHT, 1], [after, AFTER_HEIGHT, AFTER_ALPHA]])
        for (const p of list) {
            if (p.fire) {
                add(fireShape(p), p, alpha);
                continue;
            }
            add({ kind: KIND_DISC, x: p.x, y: p.y, d: p.d }, p, alpha);
            add({ kind: KIND_CYLINDER, x: p.x, y: p.y, d: p.d, height }, p, alpha);
            add({ kind: KIND_SMOKE, x: p.x, y: p.y, d: p.d }, p, alpha);
        }
    for (const [list, step, alpha] of [[next, race.step, 1], [after, race.step + 1, AFTER_ALPHA]])
        if (step === race.total - 1)                // the race's last
            for (const p of list)
                add({ kind: KIND_FLAG, x: p.x, y: p.y, h: floatHeight(p) }, p, alpha);
    const nearest = (p) => after.reduce((a, b) => (Math.hypot(b.x - p.x, b.y - p.y) < Math.hypot(a.x - p.x, a.y - p.y) ? b : a));
    for (const p of next)
        if (after.length > 1 && !p.fork)            // the way forks here
            add({ kind: KIND_QUESTION, x: p.x, y: p.y, h: floatHeight(p) }, p, 1);
        else if (after.length) {                    // on a fork's way: on to the nearest
            const to = nearest(p);
            add({ kind: KIND_ARROW, x: p.x, y: p.y, h: floatHeight(p), to: [to.x, to.y] }, p, 1);
        }
    return { shapes, styles };
}

// The race on the minimap (its 2D context, k pixels a tile), under the
// cars: the next checkpoints, and fainter the ones of the two steps after;
// a ring of fire as a bar across its way.
export function checkpointsMinimap(ctx2d, k) {
    if (!wasm || !race.on || ed.on || race.finish !== null)
        return;
    ctx2d.save();
    ctx2d.lineWidth = Math.max(1, k * 0.5);
    for (let s = 2; s >= 0; s--) {               // the next one on top
        ctx2d.globalAlpha = s ? 0.45 : 1;
        for (const p of checkpointsAt(race.step + s)) {
            ctx2d.beginPath();
            if (p.fire) {
                const rad = p.angle * Math.PI / 180, r = Math.max(4, p.d / 2 * k);
                const sx = -Math.sin(rad) * r, sy = Math.cos(rad) * r, w = Math.max(2, k * 0.8);
                ctx2d.moveTo(p.x * k - sx, p.y * k - sy);
                ctx2d.lineTo(p.x * k + sx, p.y * k + sy);
                ctx2d.strokeStyle = '#fff';
                ctx2d.lineWidth = w + 2;
                ctx2d.stroke();
                ctx2d.strokeStyle = '#ffa020';
                ctx2d.lineWidth = w;
                ctx2d.stroke();
                ctx2d.lineWidth = Math.max(1, k * 0.5);
                continue;
            }
            ctx2d.arc(p.x * k, p.y * k, Math.max(3, p.d / 2 * k), 0, 2 * Math.PI);
            ctx2d.fillStyle = p.fork ? '#f28c14' : '#eb1a12';
            ctx2d.fill();
            ctx2d.strokeStyle = '#fff';
            ctx2d.stroke();
        }
    }
    ctx2d.restore();
}

const forward = () => [Math.cos(ed.pitch) * Math.cos(ed.yaw), Math.cos(ed.pitch) * Math.sin(ed.yaw), Math.sin(ed.pitch)];

// Before the module advances: the editor's camera moves (ms since the last
// animation frame) and goes to the renderer.
export function checkpointsBeforeFrame(ms) {
    if (!ed.on || !wasm)
        return;
    const dt = Math.min(ms, 100) / 1000, k = ed.keys;
    const held = (...codes) => codes.some((c) => k.has(c));
    if (document.activeElement !== ed.text) {
        ed.yaw += ((held('ArrowRight') ? 1 : 0) - (held('ArrowLeft') ? 1 : 0)) * KEY_TURN * dt;
        ed.pitch += ((held('ArrowUp') ? 1 : 0) - (held('ArrowDown') ? 1 : 0)) * KEY_TURN * dt;
        ed.pitch = Math.max(-1.5, Math.min(1.5, ed.pitch));
        const speed = SPEED * (held('ShiftLeft', 'ShiftRight') ? 4 : 1) * dt;
        const along = (held('KeyW') ? 1 : 0) - (held('KeyS') ? 1 : 0);
        const side = (held('KeyD') ? 1 : 0) - (held('KeyA') ? 1 : 0);
        const up = (held('KeyE', 'Space') ? 1 : 0) - (held('KeyQ', 'KeyC') ? 1 : 0);
        const c = Math.cos(ed.yaw), s = Math.sin(ed.yaw);   // W A S D on the level, where the camera turns
        ed.eye[0] += (c * along - s * side) * speed;
        ed.eye[1] += (s * along + c * side) * speed;
        ed.eye[2] += up * speed;
    }
    // up to EYE_OUT tiles out of the wall; between EYE_LOW (and EYE_OVER the
    // ground) and EYE_HIGH up
    ed.eye[0] = Math.max(-EYE_OUT, Math.min(MAP_END + EYE_OUT, ed.eye[0]));
    ed.eye[1] = Math.max(-EYE_OUT, Math.min(MAP_END + EYE_OUT, ed.eye[1]));
    ed.eye[2] = Math.min(EYE_HIGH, Math.max(EYE_LOW, groundHeight(heights, ed.eye[0], ed.eye[1]) + EYE_OVER, ed.eye[2]));
    const f = forward();
    wasm.web_free_camera(1, ...ed.eye, ...f);
    ed.aim = hitGround(heights, ed.eye, f);
    // aimed: the nearest along the ray, a ring of fire it goes through or
    // the checkpoint on the ground where it lands (of those, the one whose
    // centre is the nearest)
    ed.aimed = -1;
    let best = Infinity;
    const groundT = ed.aim ? Math.hypot(ed.aim[0] - ed.eye[0], ed.aim[1] - ed.eye[1],
                                        groundHeight(heights, ed.aim[0], ed.aim[1]) - ed.eye[2]) : Infinity;
    data.points.forEach((p, i) => {
        let t = Infinity;
        if (p.fire)
            t = ringHit(ed.eye, f, p) ?? Infinity;
        else if (ed.aim) {
            const d = Math.hypot(p.x - ed.aim[0], p.y - ed.aim[1]);
            if (d <= p.d / 2)
                t = groundT + d * 1e-3;
        }
        if (t < best) {
            best = t;
            ed.aimed = i;
        }
    });
}

// How far along the ray from eye along dir (unit) it goes through the ring of
// fire p (its flames too), or null.
function ringHit(eye, dir, p) {
    const rad = p.angle * Math.PI / 180, nx = Math.cos(rad), ny = Math.sin(rad);
    const den = dir[0] * nx + dir[1] * ny;
    if (Math.abs(den) < 1e-6)
        return null;
    const cz = groundHeight(heights, p.x, p.y) + p.z;
    const t = ((p.x - eye[0]) * nx + (p.y - eye[1]) * ny) / den;
    if (t <= 0)
        return null;
    const q = [eye[0] + dir[0] * t - p.x, eye[1] + dir[1] * t - p.y, eye[2] + dir[2] * t - cz];
    return Math.hypot(...q) <= p.d / 2 + FIRE_OUT / 2 ? t : null;
}

// After the module drew a frame: the race goes on, and the checkpoints are
// drawn over it (in the editor all of them, the one aimed at blinking, and
// where a new one would go).
let shown = null;                   // the shapes in the buffer, and their styles
let edShapes = [];                  // the editor's, in the buffer
let decalMemory = 0;                // RZ_MAX_DECALS RzDecal in the module's memory

// The discs among shapes (styles: theirs) projected on the cars too, from
// the next frame: the nearest to the camera, as many as the renderer takes.
const MAX_DECALS = 4;
function projectDiscs(shapes, styles, view) {
    const discs = shapes.map((s, k) => [s, styles[k]]).filter(([s]) => s.kind === KIND_DISC)
        .sort(([a], [b]) => Math.hypot(a.x - view[16], a.y - view[18]) - Math.hypot(b.x - view[16], b.y - view[18]))
        .slice(0, MAX_DECALS);
    decalMemory ||= wasm.web_alloc(MAX_DECALS * 32);
    const f = new Float32Array(wasm.memory.buffer, decalMemory, MAX_DECALS * 8);
    discs.forEach(([s, style], i) => f.set([s.x, s.y, s.d / 2, style.alpha, ...style.color, 0], i * 8));
    wasm.web_decals(decalMemory, discs.length);
}

export function checkpointsAfterFrame() {
    if (!wasm || !markers)
        return;
    updateRace();
    const p = wasm.web_view();
    if (!p)
        return;
    const view = new Float32Array(wasm.memory.buffer, p, 32);
    if (view[15] === 0 && view[11] === 0)       // no frame yet
        return;
    if (!ed.on) {
        if (dirty) {
            const { shapes, styles } = race.on ? raceShapes() : { shapes: [], styles: [] };
            markers.set(shapes, heights);
            shown = { shapes, styles };
            dirty = false;
        }
        markers.draw(view, shown.styles);
        projectDiscs(shown.shapes, shown.styles, view);
        return;
    }
    if (dirty) {                                // the checkpoints: made again only when they change
        edShapes = data.points.map((p) => (p.fire ? fireShape(p) : { kind: KIND_DISC, x: p.x, y: p.y, d: p.d }));
        markers.set(edShapes, heights);
        dirty = false;
    }
    const pulse = 0.75 + 0.25 * Math.sin(performance.now() / 150);
    const styles = data.points.map((p, i) => ({
        color: i === ed.aimed ? COLORS.aimed : p.fork ? COLORS.fork : COLORS.normal,
        alpha: i === ed.aimed ? pulse : 1,
    }));
    markers.draw(view, styles);
    if (ed.aim && ed.aimed < 0 && insideWall(...ed.aim)) {   // the preview, every frame: it moves with the aim
        const shape = { kind: KIND_DISC, x: ed.aim[0], y: ed.aim[1], d: ed.diameter };
        const style = { color: COLORS.preview, alpha: 0.5 };
        previewMarkers.set([shape], heights);
        previewMarkers.draw(view, [style]);
        projectDiscs([...edShapes, shape], [...styles, style], view);
    } else {
        projectDiscs(edShapes, styles, view);
    }
}

// The race's lap, checkpoint and time at the top right, on the text's canvas
// (cleared before, see terep2.js); the target time, and at the end whether
// it was made.
function raceOverlay(ctx2d) {
    const w = ctx2d.canvas.width, size = Math.max(14, ctx2d.canvas.height / 30);
    const n = race.numbers.length, done = race.finish !== null;
    const lines = [];
    if (done) {
        lines.push(['Finished ' + clock(raceSeconds()), '#fff']);
        if (data.targetTime !== null)
            lines.push(raceSeconds() <= data.targetTime ? ['Qualified!', '#6f6'] : ['Not qualified', '#f66'],
                       ['Target ' + clock(data.targetTime), '#ccc']);
    } else {
        lines.push([`Lap ${Math.floor(race.step / n) + 1}/${Math.max(1, data.laps)}`, '#fff'],
                   [`Checkpoint ${race.step % n + 1}/${n}`, '#fff'],
                   [clock(raceSeconds()), '#fff']);
        if (data.targetTime !== null)
            lines.push(['Target ' + clock(data.targetTime), raceSeconds() > data.targetTime ? '#f66' : '#ccc']);
    }
    ctx2d.save();
    ctx2d.font = `bold ${size}px system-ui, sans-serif`;
    ctx2d.textAlign = 'right';
    ctx2d.textBaseline = 'top';
    ctx2d.lineWidth = size / 5;
    ctx2d.strokeStyle = '#000';
    // under the game's title (at y 10 of its 320 x 200, two lines of 7.5:
    // gl_renderer.cpp's draw_text)
    const top = 30 * Math.min(w / 320, ctx2d.canvas.height / 200);
    lines.forEach(([text, color], k) => {
        const y = top + k * size * 1.2;
        ctx2d.fillStyle = color;
        ctx2d.strokeText(text, w - size * 0.6, y);
        ctx2d.fillText(text, w - size * 0.6, y);
    });
    ctx2d.restore();
}

// The editor's labels (each checkpoint's number) and its crosshair, or the
// race's text, on the text's canvas (cleared before, see terep2.js).
export function checkpointsOverlay(ctx2d) {
    if (!wasm)
        return;
    if (!ed.on) {
        if (race.on)
            raceOverlay(ctx2d);
        return;
    }
    const p = wasm.web_view();
    if (!p)
        return;
    const view = new Float32Array(wasm.memory.buffer, p, 32);
    const w = ctx2d.canvas.width, h = ctx2d.canvas.height, size = Math.max(12, h / 45);
    ctx2d.save();
    ctx2d.font = `bold ${size}px system-ui, sans-serif`;
    ctx2d.textAlign = 'center';
    ctx2d.textBaseline = 'middle';
    ctx2d.lineWidth = size / 5;
    ctx2d.strokeStyle = '#000';
    data.points.forEach((pt, i) => {
        const over = pt.fire ? pt.z + pt.d / 2 + FIRE_OUT + 0.3 : 0.8;    // over a ring of fire, over its flames
        const s = project(view, pt.x, groundHeight(heights, pt.x, pt.y) + over, pt.y, w, h);
        if (!s)
            return;
        const label = pt.n + (pt.fork ? '*' : '') + (pt.fire ? 'F' : '');
        ctx2d.fillStyle = i === ed.aimed ? '#ffe64d' : '#fff';
        ctx2d.strokeText(label, s[0], s[1]);
        ctx2d.fillText(label, s[0], s[1]);
    });
    ctx2d.strokeStyle = '#fffc';         // the crosshair
    ctx2d.lineWidth = Math.max(1, size / 10);
    ctx2d.beginPath();
    ctx2d.moveTo(w / 2 - size / 2, h / 2); ctx2d.lineTo(w / 2 + size / 2, h / 2);
    ctx2d.moveTo(w / 2, h / 2 - size / 2); ctx2d.lineTo(w / 2, h / 2 + size / 2);
    ctx2d.stroke();
    ctx2d.restore();
}

// ---------------------------------------------------------------------------
// The editor

// The checkpoints changed in the editor: the text follows.
function changed() {
    dirty = true;
    if (ed.text)
        ed.text.value = formatCheckpoints(data);
    showStatus();
}

function showStatus(errors = []) {
    if (!ed.status)
        return;
    const a = ed.aimed >= 0 ? data.points[ed.aimed] : null;
    ed.status.textContent = errors.length ? 'Not read: ' + errors.join('; ')
        : `${data.points.length} checkpoints. New ones: ${ed.diameter.toFixed(1)} tiles` +
          (a ? `. Aimed: ${a.n}${a.fork ? '*' : ''}${a.fire ? 'F' : ''}, ${a.d.toFixed(1)} tiles` +
               (a.fire ? `, ${a.z.toFixed(2)} up, ${a.angle.toFixed(1)}°` : '') : '');
}

const lastNumber = () => Math.max(0, ...data.points.map((p) => p.n));
const round2 = (v) => Math.round(v * 100) / 100;

// A checkpoint where the camera aims: the next number, or (fork) one more
// of the last number, all of which become forks.  A ring of fire (fire)
// stands there just over the ground, the way through it the camera's.
function place(fork, fire = false) {
    if (!ed.aim || !insideWall(...ed.aim))
        return;
    const n = fork ? Math.max(1, lastNumber()) : lastNumber() + 1;
    if (fork)
        for (const p of data.points)
            if (p.n === n)
                p.fork = true;
    const p = { n, fork, fire, x: round2(ed.aim[0]), y: round2(ed.aim[1]), d: ed.diameter };
    if (fire) {
        p.z = round2(ed.diameter / 2 + 0.5);
        p.angle = Math.round((ed.yaw * 180 / Math.PI % 360 + 360) % 360 * 10) / 10;
    }
    data.points.push(p);
    changed();
}

function editKey(e, up) {
    if (up) {
        ed.keys.delete(e.code);
        return;
    }
    ed.keys.add(e.code);
    const a = ed.aimed >= 0 ? data.points[ed.aimed] : null;
    switch (e.code) {
    case 'Enter': place(false); break;
    case 'KeyB': place(true); break;
    case 'KeyR': place(e.shiftKey, true); break;
    case 'PageUp': case 'PageDown':                 // a ring of fire up or down
        if (a?.fire) { a.z = round2(a.z + (e.code === 'PageUp' ? 0.25 : -0.25)); changed(); }
        break;
    case 'Comma': case 'Period':                    // a ring of fire turned
        if (a?.fire) { a.angle = ((a.angle + (e.code === 'Period' ? 15 : -15)) % 360 + 360) % 360; changed(); }
        break;
    case 'KeyF': if (a) { a.fork = !a.fork; changed(); } break;
    case 'Delete': case 'KeyX': if (a) { data.points.splice(ed.aimed, 1); changed(); } break;
    case 'Equal': case 'NumpadAdd': if (a) { a.n++; changed(); } break;
    case 'Minus': case 'NumpadSubtract': if (a && a.n > 1) { a.n--; changed(); } break;
    case 'BracketLeft': case 'BracketRight': resize(e.code === 'BracketRight' ? 1 : -1); break;
    }
}

// The aimed checkpoint, or the new ones, half a tile bigger or smaller.
function resize(sign) {
    const a = ed.aimed >= 0 ? data.points[ed.aimed] : null;
    if (a) {
        a.d = Math.max(0.5, a.d + sign * 0.5);
        ed.diameter = a.d;
        changed();
    } else {
        ed.diameter = Math.max(0.5, ed.diameter + sign * 0.5);
        showStatus();
    }
}

// The page's keys while the editor is open: true when taken (not the game's).
export function checkpointsKey(e, up) {
    if (!ed.on)
        return false;
    if (e.target === ed.text)                    // typing in the text: its own
        return true;
    if (e.code === 'Escape')                     // leaves the mouse look (the browser does it), not the game
        return true;
    if (e.code !== 'F11' && e.code !== 'F12')
        e.preventDefault();
    editKey(e, up);
    return true;
}

function buildPanel() {
    const panel = document.createElement('div');
    panel.id = 'checkpointEditor';
    panel.innerHTML = `
      <b>Checkpoints</b> <span class="cpName"></span>
      <p class="small">Click the picture to look with the mouse (Esc frees it).
      W A S D fly on the level, E / Space up, Q / C down, Shift faster, arrows turn.
      Click or Enter: a checkpoint where the cross is (the next number);
      B or Shift+click: a fork (one more of the last number). On one: F fork or not,
      + / − its number, X / Delete removes it; [ ] or the wheel: its diameter
      (or the new ones'). Right click removes too.
      R (Shift+R a fork): a ring of fire there, facing the way the camera looks;
      on one, Page Up / Down raise or lower it, , and . turn it.</p>
      <textarea spellcheck="false"></textarea>
      <p class="cpStatus small"></p>
      <button class="cpCopy">Copy</button> <button class="cpSave">Save file</button>
      <button class="cpClose">Close</button>`;
    document.body.appendChild(panel);
    ed.panel = panel;
    ed.text = panel.querySelector('textarea');
    ed.status = panel.querySelector('.cpStatus');
    ed.text.addEventListener('input', () => {
        const parsed = parseCheckpoints(ed.text.value);
        if (!parsed.errors.length) {
            data = parsed;
            dirty = true;
        }
        showStatus(parsed.errors);
    });
    ed.text.addEventListener('keydown', (e) => e.stopPropagation());
    ed.text.addEventListener('keyup', (e) => e.stopPropagation());
    panel.querySelector('.cpCopy').onclick = () => navigator.clipboard?.writeText(formatCheckpoints(data));
    panel.querySelector('.cpSave').onclick = () => {
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([formatCheckpoints(data)], { type: 'text/plain' }));
        a.download = mapName + '.txt';
        a.click();
        URL.revokeObjectURL(a.href);
    };
    panel.querySelector('.cpClose').onclick = () => closeEditor();
}

// The mouse on the picture: pointer lock to look around; then clicks place
// and remove, the wheel changes diameters.
function onPointerDown(e) {
    if (!ed.on || e.target.closest('#checkpointEditor'))
        return;
    e.preventDefault();                          // (which also keeps the focus where it is)
    if (document.activeElement === ed.text)
        ed.text.blur();
    if (document.pointerLockElement !== glCanvas) {
        glCanvas.requestPointerLock?.();
        return;
    }
    if (e.button === 0)
        place(e.shiftKey);
    else if (e.button === 2 && ed.aimed >= 0) {
        data.points.splice(ed.aimed, 1);
        changed();
    }
}
function onMouseMove(e) {
    if (!ed.on || document.pointerLockElement !== glCanvas)
        return;
    ed.yaw += e.movementX * TURN;
    ed.pitch = Math.max(-1.5, Math.min(1.5, ed.pitch - e.movementY * TURN));
}
function onWheel(e) {
    if (!ed.on || e.target.closest('#checkpointEditor'))
        return;
    e.preventDefault();
    resize(e.deltaY < 0 ? 1 : -1);
}
const noMenu = (e) => { if (ed.on && !e.target.closest('#checkpointEditor')) e.preventDefault(); };

function openEditor() {
    if (!wasm || !markers) {
        console.log('editCheckpoints(): only in a game with the WebGL renderer');
        return false;
    }
    if (!ed.panel)
        buildPanel();
    ed.panel.querySelector('.cpName').textContent = mapName;
    ed.panel.hidden = false;
    ed.text.value = formatCheckpoints(data);
    // the camera starts where the game's was, looking the same way
    const p = wasm.web_view();
    if (p) {
        const v = new Float32Array(wasm.memory.buffer, p, 32);
        if (v[15] || v[11]) {
            ed.eye = [v[16], v[18], v[17]];
            ed.yaw = Math.atan2(v[21], v[19]);
            ed.pitch = Math.max(-1.5, Math.min(1.5, Math.asin(Math.max(-1, Math.min(1, v[20])))));
        }
    }
    ed.keys.clear();
    ed.on = true;
    dirty = true;                               // the buffer: the editor's shapes
    wasm.web_pause(1);
    wasm.web_editor_view(1);
    addEventListener('pointerdown', onPointerDown);
    addEventListener('mousemove', onMouseMove);
    addEventListener('wheel', onWheel, { passive: false });
    addEventListener('contextmenu', noMenu);
    showStatus();
    return true;
}

function closeEditor() {
    ed.on = false;
    ed.keys.clear();
    ed.panel.hidden = true;
    if (document.pointerLockElement)
        document.exitPointerLock();
    removeEventListener('pointerdown', onPointerDown);
    removeEventListener('mousemove', onMouseMove);
    removeEventListener('wheel', onWheel);
    removeEventListener('contextmenu', noMenu);
    if (wasm) {
        wasm.web_free_camera(0, 0, 0, 0, 1, 0, 0);
        wasm.web_pause(0);
        wasm.web_editor_view(0);
        startRace();                            // again: the checkpoints may have changed
    }
}

// In the console: opens the editor in the game being played (WebGL), or
// closes it.
globalThis.editCheckpoints = () => (ed.on ? (closeEditor(), false) : openEditor());
