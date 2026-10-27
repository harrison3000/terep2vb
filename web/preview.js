// The previews of the 30th Anniversary Edition's choices (terep2.js): a PCX
// image, decoded by the game's own decoder (in terep2.wasm), and a car
// (.dat) drawn with its texture, turning, with WebGL 2.

// The image ({ width, height, pixels, palette }) of a PCX file, null if it
// cannot be read.
export function decodePcx(wasm, buffer) {
    const file = new Uint8Array(buffer), p = wasm.web_alloc(file.length);
    new Uint8Array(wasm.memory.buffer, p, file.length).set(file);
    const ok = wasm.web_pcx(p, file.length);
    wasm.web_free(p);
    if (!ok)
        return null;
    const width = wasm.web_pcx_width(), height = wasm.web_pcx_height(), palette = wasm.web_pcx_palette();
    return {
        width, height,
        pixels: new Uint8Array(wasm.memory.buffer, wasm.web_pcx_pixels(), width * height).slice(),
        palette: palette ? new Uint8Array(wasm.memory.buffer, palette, 768).slice() : null,
    };
}

// Draws the image on the canvas, in a palette (grey without one), cut to
// width x height (the top left).
export function drawPcx(canvas, image, palette = image.palette, width = image.width, height = image.height) {
    canvas.width = width = Math.min(width, image.width);
    canvas.height = height = Math.min(height, image.height);
    const ctx = canvas.getContext('2d'), out = ctx.createImageData(width, height);
    for (let y = 0, i = 0; y < height; y++)
        for (let x = 0; x < width; x++, i++) {
            const c = image.pixels[y * image.width + x];
            out.data.set(palette ? palette.subarray(c * 3, c * 3 + 3) : [c, c, c], i * 4);
            out.data[i * 4 + 3] = 255;
        }
    ctx.putImageData(out, 0, 0);
}

// A car file (see src/car_file.cpp): its points ({ x, y, z, radius, wheel },
// in map units, z up) and what its model (src/car_draw.cpp) draws: the
// program read straight through, its polygons and wheels taken whatever
// the view (a depth buffer sorts them out), its jumps passed over: polygons
// ({ points, uv: [[u, v]...] or null, colour }, the points as { x, y, z })
// and wheels ({ at, axle, uv: the four corners of the sprites seen from
// either end of the axle, views 0 and 8 }).  The
// shadows (polygons on the ground under the car) and the smoke are left out.
export function decodeCar(buffer) {
    const dv = new DataView(buffer), size = buffer.byteLength;
    const u16 = (at) => at + 2 <= size ? dv.getUint16(at, true) : 0;
    const vo = u16(0), mo = u16(4), points = [];
    for (let i = 0, at = vo + 2; i < u16(vo) && at + 0x1c <= size; i++, at += 0x1c)
        points.push({ x: dv.getInt32(at, true) / 65536, y: dv.getInt32(at + 4, true) / 65536,
            z: dv.getInt32(at + 8, true) / 65536, radius: dv.getInt16(at + 24, true), wheel: u16(at + 26) });

    // the model's point k: vertex k / 2 (k odd: the ground under it), then
    // vertices 0 and 1 moved along the front axle (not steered here)
    const n = points.length;
    const point = (k) => k < 2 * n ? (k & 1 ? null : points[k >> 1])
        : k === 2 * n && n > 0 ? { ...points[0], x: points[0].x + 128 }
        : k === 2 * n + 1 && n > 1 ? { ...points[1], x: points[1].x - 128 } : null;

    const code = new Uint8Array(buffer, Math.min(mo, size));
    const rd8 = (pc) => pc < code.length ? code[pc] : 0, rd16 = (pc) => rd8(pc) | rd8(pc + 1) << 8;
    const polygons = [], wheels = [];
    const polygon = (pc, count, step, uv, colour) => {
        const corners = [], uvs = [];
        for (let i = 0; i < count; i++, pc += step) {
            corners.push(point(rd16(pc)));
            uvs.push([rd16(pc + 2) / 256, rd16(pc + 4) / 256]);    // 8.8 texels
        }
        if (corners.every(Boolean))
            polygons.push({ points: corners, uv: uv ? uvs : null, colour });
    };
    for (let pc = 0, len = 0; pc < code.length; pc += len) {
        const op = rd8(pc++) & 0x1f, count = rd8(pc) + 1;
        len = 0;
        switch (op) {
        case 1: len = 4; break;                                         // pixel
        case 2: case 3: len = 6; break;                                 // nearer, facing
        case 4:                                                         // flat
            polygon(pc + 1, count, 2, false, rd8(pc + 1 + count * 2));
            len = 1 + count * 2 + 2;
            break;
        case 6:                                                         // shaded: its first shade
            polygon(pc + 1, count, 4, false, rd8(pc + 4));
            len = 1 + count * 4;
            break;
        case 7:                                                         // shaded from a table: its base
            polygon(pc + 3, count, 4, false, rd8(pc + 2));
            len = 3 + count * 4;
            break;
        case 8:                                                         // textured
            polygon(pc + 1, count, 6, true, 0);
            len = 1 + count * 6;
            break;
        case 9: len = 20; break;                                        // sprite (smoke)
        case 10: {                                                      // wheel: its views 0 and 8
            const at = point(rd16(pc)), q = point(rd16(pc + 2));
            const uv = [0, 8].map((view) => [0, 1, 2, 3].map((i) =>
                [rd16(pc + 6 + view * 20 + 4 + 4 * i) / 256, rd16(pc + 6 + view * 20 + 6 + 4 * i) / 256]));
            if (at)
                wheels.push({ at, axle: q && q !== at ? [q.x - at.x, q.y - at.y, q.z - at.z] : [1, 0, 0], uv });
            len = 186;
            break;
        }
        case 16: len = 4; break;                                        // sort2
        case 17: case 18: case 19: case 20: len = 2; break;             // jumps, call
        }
    }
    return { points, polygons, wheels };
}

// The triangles of a wheel: a short cylinder across the car, the sprites
// of views 0 and 8 on its ends, the tread dark.
function wheelTriangles(wheel, radius) {
    const len = Math.hypot(...wheel.axle), ax = wheel.axle.map((c) => c / len);
    // two directions across the axle: e1 up as much as it can be, e0 along the car
    let e1 = [-ax[2] * ax[0], -ax[2] * ax[1], 1 - ax[2] * ax[2]];
    const l1 = Math.hypot(...e1) || 1;
    e1 = e1.map((c) => c / l1);
    const e0 = [ax[1] * e1[2] - ax[2] * e1[1], ax[2] * e1[0] - ax[0] * e1[2], ax[0] * e1[1] - ax[1] * e1[0]];
    const half = radius * 0.4;
    // the sprite's corners at (-1, -1), (-1, 1), (1, 1), (1, -1) of (e0, e1)
    const uvAt = (s, t, side) => { const c = wheel.uv[side < 0 ? 0 : 1]; return [0, 1].map((k) => ((1 - s) * (1 - t) * c[0][k] + (1 - s) * (1 + t) * c[1][k]
        + (1 + s) * (1 + t) * c[2][k] + (1 + s) * (1 - t) * c[3][k]) / 4); };
    const at = (s, t, side) => ({
        x: wheel.at.x + radius * (s * e0[0] + t * e1[0]) + side * half * ax[0],
        y: wheel.at.y + radius * (s * e0[1] + t * e1[1]) + side * half * ax[1],
        z: wheel.at.z + radius * (s * e0[2] + t * e1[2]) + side * half * ax[2],
    });
    const tris = [], N = 16;
    for (let i = 0; i < N; i++) {
        const a0 = i / N * 2 * Math.PI, a1 = (i + 1) / N * 2 * Math.PI;
        const s0 = Math.cos(a0), t0 = Math.sin(a0), s1 = Math.cos(a1), t1 = Math.sin(a1);
        for (const side of [-1, 1])
            tris.push({ points: [at(0, 0, side), at(s0, t0, side), at(s1, t1, side)],
                uv: [uvAt(0, 0, side), uvAt(s0, t0, side), uvAt(s1, t1, side)] });
        tris.push({ points: [at(s0, t0, -1), at(s1, t1, -1), at(s1, t1, 1)], rgb: [24, 24, 24] });
        tris.push({ points: [at(s0, t0, -1), at(s1, t1, 1), at(s0, t0, 1)], rgb: [24, 24, 24] });
    }
    return tris;
}

// The car's WebGL 2 drawing on a canvas: its program, and the triangles and
// texture of the car, texture and palette it was last given.
const VERTEX_SHADER = `#version 300 es
in vec3 pos; in vec2 uv; in vec3 rgb;
uniform vec3 mid; uniform float angle, scale, aspect;
out vec2 vUv; out vec3 vRgb;
void main() {
    vec3 p = pos - mid;
    float c = cos(angle), s = sin(angle), ce = cos(0.45), se = sin(0.45);
    float rx = p.x * c - p.y * s, ry = p.x * s + p.y * c;
    gl_Position = vec4(rx * scale / aspect, (p.z * ce + ry * se) * scale, (ry * ce - p.z * se) * scale * 0.5, 1.0);
    vUv = uv; vRgb = rgb;
}`;
const FRAGMENT_SHADER = `#version 300 es
precision mediump float;
in vec2 vUv; in vec3 vRgb;
uniform sampler2D tex; uniform vec2 texSize;
out vec4 colour;
void main() {
    colour = vUv.x < 0.0 ? vec4(vRgb, 1.0) : texture(tex, vUv / texSize);
}`;

function carView(canvas) {
    if (canvas.carView)
        return canvas.carView;
    const gl = canvas.getContext('webgl2');
    if (!gl)
        return null;
    const program = gl.createProgram();
    for (const [type, source] of [[gl.VERTEX_SHADER, VERTEX_SHADER], [gl.FRAGMENT_SHADER, FRAGMENT_SHADER]]) {
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        gl.attachShader(program, shader);
    }
    gl.linkProgram(program);
    const view = { gl, program, buffer: gl.createBuffer(), texture: gl.createTexture(), count: 0, given: [] };
    gl.bindVertexArray(gl.createVertexArray());
    gl.bindBuffer(gl.ARRAY_BUFFER, view.buffer);
    for (const [name, size, offset] of [['pos', 3, 0], ['uv', 2, 12], ['rgb', 3, 20]]) {
        const at = gl.getAttribLocation(program, name);
        gl.enableVertexAttribArray(at);
        gl.vertexAttribPointer(at, size, gl.FLOAT, false, 32, offset);
    }
    gl.bindTexture(gl.TEXTURE_2D, view.texture);
    for (const p of [gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER])
        gl.texParameteri(gl.TEXTURE_2D, p, gl.NEAREST);
    for (const p of [gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T])
        gl.texParameteri(gl.TEXTURE_2D, p, gl.CLAMP_TO_EDGE);
    return canvas.carView = view;
}

// The car's triangles (8 floats a corner: x, y, z, u, v, r, g, b; u < 0 for
// a colour) and its texture (RGBA), in a palette.
function upload(view, car, texture, palette) {
    const { gl } = view, colour = (i) => palette ? [palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]] : [i, i, i];
    const data = [];
    const triangle = (p, uv, rgb) => {
        for (let i = 0; i < 3; i++)
            data.push(p[i].x, p[i].y, p[i].z, ...(uv ? uv[i] : [-1, -1]), ...(rgb || [0, 0, 0]).map((c) => c / 255));
    };
    for (const poly of car.polygons)            // as fans
        for (let i = 1; i + 1 < poly.points.length; i++)
            triangle([poly.points[0], poly.points[i], poly.points[i + 1]],
                poly.uv && [poly.uv[0], poly.uv[i], poly.uv[i + 1]], colour(poly.colour));
    view.body = data.length / 8;
    for (const wheel of car.wheels)
        for (const t of wheelTriangles(wheel, wheel.at.radius || 20))
            triangle(t.points, t.uv, t.rgb);
    gl.bindBuffer(gl.ARRAY_BUFFER, view.buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data), gl.STATIC_DRAW);
    view.count = data.length / 8;

    const w = texture?.width || 1, h = texture?.height || 1, rgba = new Uint8Array(w * h * 4).fill(128);
    if (texture)
        texture.pixels.forEach((c, i) => rgba.set([...colour(c), 255], i * 4));
    gl.bindTexture(gl.TEXTURE_2D, view.texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
    view.texSize = [w, h];

    const n = car.points.length;
    view.mid = ['x', 'y', 'z'].map((k) => car.points.reduce((s, p) => s + p[k], 0) / n);
    view.size = Math.max(...car.points.map((p) => Math.hypot(p.x - view.mid[0], p.y - view.mid[1], p.z - view.mid[2]) + p.radius));
}

// Draws the car turned by angle (radians, around z), seen from a little
// above, with its texture (an image of decodePcx) in a palette, with WebGL.
export function drawCar(canvas, car, angle, texture, palette) {
    const view = carView(canvas);
    if (!view || !car.points.length)
        return;
    const { gl } = view;
    if (view.given[0] !== car || view.given[1] !== texture || view.given[2] !== palette) {
        upload(view, car, texture, palette);
        view.given = [car, texture, palette];
    }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.cullFace(gl.BACK);
    gl.useProgram(view.program);
    const u = (name) => gl.getUniformLocation(view.program, name);
    gl.uniform3fv(u('mid'), view.mid);
    gl.uniform1f(u('angle'), angle);
    gl.uniform1f(u('scale'), 0.9 / view.size);
    gl.uniform1f(u('aspect'), canvas.width / canvas.height);
    gl.uniform2fv(u('texSize'), view.texSize);
    gl.enable(gl.CULL_FACE);            // the body without the back of its polygons, as the game
    gl.drawArrays(gl.TRIANGLES, 0, view.body);
    gl.disable(gl.CULL_FACE);           // the wheels whichever way they turn
    gl.drawArrays(gl.TRIANGLES, view.body, view.count - view.body);
}
