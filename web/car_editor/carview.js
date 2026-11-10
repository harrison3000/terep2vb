// The car in the editor: its polygons and wheels drawn with WebGL 2, and over
// them, on a 2D canvas, the ground, the springs and the vertices; the orbit
// camera, and what is under the pointer.  Positions are the file's: in tiles,
// x across the car, y along it (the front is -y), z up.

import { wheelTriangles } from '../preview.js';

const FOV = 40 * Math.PI / 180;
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const scale = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => scale(a, 1 / (Math.hypot(...a) || 1));
const xyz = (p) => [p.x, p.y, p.z];

const VERTEX_SHADER = `#version 300 es
in vec3 pos; in vec2 uv; in vec4 rgba;
uniform mat4 viewProj;
out vec2 vUv; out vec4 vRgba;
void main() { vUv = uv; vRgba = rgba; gl_Position = viewProj * vec4(pos, 1.0); }`;

// backs: 0 the back of a polygon is not drawn (as in the game), 1 in red, 2 as the front
const FRAGMENT_SHADER = `#version 300 es
precision mediump float;
in vec2 vUv; in vec4 vRgba;
uniform sampler2D tex; uniform vec2 texSize; uniform int backs;
out vec4 colour;
void main() {
    if (!gl_FrontFacing && backs == 0) discard;
    colour = vRgba;
    if (vUv.x >= 0.0) colour.rgb *= texture(tex, vUv / texSize).rgb;
    if (!gl_FrontFacing && backs == 1) colour.rgb = mix(colour.rgb, vec3(0.8, 0.1, 0.1), 0.65);
}`;

const SPRING_COLOURS = { rope: '#e0a030', spring: '#50c070', damper: '#60a0f0' };
const VERTEX_COLOURS = { body: '#c8c8c8', reference: '#e060e0', 'wheel front': '#40d0e0', 'wheel back': '#f09040' };
export const springKind = (s) => s.type === 0 ? 'rope' : s.type === 1 ? 'spring' : 'damper';

export class CarView {
    constructor(canvas, overlay) {
        this.canvas = canvas;
        this.overlay = overlay;
        this.doc = null;
        this.camera = { yaw: 0.7, pitch: 0.45, dist: 3, target: [0, 0, 0], ortho: false };
        this.show = { solid: true, springs: true, labels: false, backs: false };
        this.sel = { vertices: [], spring: null, polygon: null };
        this.hover = null;              // { type: 'vertex' | 'spring' | 'polygon', index }
        this.box = null;                // [x0, y0, x1, y1], the selection being dragged
        this.texture = null;
        this.polys = [];
        this.gl = canvas.getContext('webgl2', { antialias: true });
        if (this.gl)
            this.initGl();
        new ResizeObserver(() => this.draw()).observe(canvas);
    }

    initGl() {
        const gl = this.gl, program = this.program = gl.createProgram();
        for (const [type, source] of [[gl.VERTEX_SHADER, VERTEX_SHADER], [gl.FRAGMENT_SHADER, FRAGMENT_SHADER]]) {
            const shader = gl.createShader(type);
            gl.shaderSource(shader, source);
            gl.compileShader(shader);
            gl.attachShader(program, shader);
        }
        gl.linkProgram(program);
        gl.bindVertexArray(gl.createVertexArray());
        gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
        for (const [name, size, offset] of [['pos', 3, 0], ['uv', 2, 12], ['rgba', 4, 20]]) {
            const at = gl.getAttribLocation(program, name);
            gl.enableVertexAttribArray(at);
            gl.vertexAttribPointer(at, size, gl.FLOAT, false, 36, offset);
        }
        this.glTexture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.glTexture);
        for (const p of [gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER])
            gl.texParameteri(gl.TEXTURE_2D, p, gl.NEAREST);
        for (const p of [gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T])
            gl.texParameteri(gl.TEXTURE_2D, p, gl.CLAMP_TO_EDGE);
        this.uploadTexture();
    }

    // image: { width, height, pixels, palette } (decodePcx in editor.js), or null
    setTexture(image) {
        this.texture = image;
        if (this.gl)
            this.uploadTexture();
        this.rebuild();
    }

    uploadTexture() {
        const gl = this.gl, image = this.texture, w = image?.width || 1, h = image?.height || 1;
        const rgba = new Uint8Array(w * h * 4).fill(150);
        if (image)
            image.pixels.forEach((c, i) => rgba.set([...this.colour(c), 255], i * 4));
        gl.bindTexture(gl.TEXTURE_2D, this.glTexture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
        this.texSize = [w, h];
    }

    // A colour number, in the texture's palette; without one, a grey that can be seen.
    colour(c) {
        const p = this.texture?.palette;
        if (p)
            return [p[c * 3], p[c * 3 + 1], p[c * 3 + 2]];
        const g = 70 + c * 0.7;
        return [g, g, g];
    }

    setDoc(doc) {
        this.doc = doc;
        this.rebuild();
    }

    // Frames the whole car.
    frame() {
        const v = this.doc?.vertices;
        if (!v?.length)
            return;
        const low = [0, 1, 2].map((k) => Math.min(...v.map((p) => xyz(p)[k])));
        const high = [0, 1, 2].map((k) => Math.max(...v.map((p) => xyz(p)[k])));
        this.camera.target = low.map((l, k) => (l + high[k]) / 2);
        this.camera.dist = Math.max(0.5, Math.hypot(...sub(high, low)) * 1.5);
        this.draw();
    }

    // After the car changed: the triangles again.
    rebuild() {
        this.polys = [];
        const opaque = [], glass = [], wheels = [];
        const doc = this.doc;
        const corner = (out, p, uv, rgba) => out.push(p.x, p.y, p.z, ...(uv || [-1, -1]), ...rgba);
        doc?.polygons.forEach((poly, index) => {
            const pts = poly.corners.slice(0, -1).map((c) => doc.position(c));
            if (pts.length < 3 || !pts.every(Boolean))
                return;
            this.polys.push({ index, pts });
            const n = [0, 0, 0];
            pts.forEach((a, i) => {
                const b = pts[(i + 1) % pts.length];
                n[0] += (a.y - b.y) * (a.z + b.z);
                n[1] += (a.z - b.z) * (a.x + b.x);
                n[2] += (a.x - b.x) * (a.y + b.y);
            });
            const light = 0.7 + 0.3 * Math.abs(dot(norm(n), norm([0.3, -0.4, 0.87])));
            const isGlass = poly.op === 'flat' && poly.colour >= 0xf0f0;
            const rgb = poly.op === 'textured' ? [255, 255, 255]
                : isGlass ? [150, 200, 230] : this.colour(poly.colour & 0xff);
            const rgba = [...rgb.map((c) => c / 255 * light), isGlass ? 0.35 : 1];
            const out = isGlass ? glass : opaque;
            const uv = poly.op === 'textured' ? poly.corners.map((c) => c.uv) : null;
            for (let i = 1; i + 1 < pts.length; i++)
                for (const k of [0, i, i + 1])
                    corner(out, pts[k], uv && uv[k], rgba);
        });
        for (const w of doc?.wheels || []) {
            const at = doc.position(w.at), other = doc.position(w.axle);
            if (!at)
                continue;
            const uv = [0, 8].map((v) => w.views[v] || [[0, 0], [0, 0], [0, 0], [0, 0]]);
            const wheel = { at, axle: other && other !== at ? [other.x - at.x, other.y - at.y, other.z - at.z] : [1, 0, 0], uv };
            for (const t of wheelTriangles(wheel, at.radius || 0.08))
                for (let i = 0; i < 3; i++)
                    corner(wheels, t.points[i], t.uv && t.uv[i], [...(t.rgb || [255, 255, 255]).map((c) => c / 255), 1]);
        }
        this.ranges = [opaque.length / 9, wheels.length / 9, glass.length / 9];
        if (this.gl)
            this.gl.bufferData(this.gl.ARRAY_BUFFER, new Float32Array([...opaque, ...wheels, ...glass]), this.gl.DYNAMIC_DRAW);
        this.draw();
    }

    // The camera's frame: eye, and right, up and forward as unit vectors.
    basis() {
        const { yaw, pitch, dist, target } = this.camera;
        const toEye = [Math.sin(yaw) * Math.cos(pitch), -Math.cos(yaw) * Math.cos(pitch), Math.sin(pitch)];
        const f = scale(toEye, -1), r = norm(cross(f, [0, 0, 1])), u = cross(r, f);
        return { eye: add(target, scale(toEye, dist)), r, u, f };
    }

    size() {
        return [this.overlay.clientWidth || 1, this.overlay.clientHeight || 1];
    }

    // World to pixels of the canvas (CSS): { x, y, depth }, null behind the eye.
    project(p) {
        const { eye, r, u, f } = this.basis(), [w, h] = this.size(), v = sub(p, eye), a = w / h;
        const zc = dot(v, f), t = Math.tan(FOV / 2);
        let nx, ny;
        if (this.camera.ortho) {
            const half = this.camera.dist * t;
            nx = dot(v, r) / (half * a);
            ny = dot(v, u) / half;
        } else {
            if (zc <= 0.01)
                return null;
            nx = dot(v, r) / (zc * t * a);
            ny = dot(v, u) / (zc * t);
        }
        return { x: (nx + 1) / 2 * w, y: (1 - ny) / 2 * h, depth: zc };
    }

    // The ray under a pixel: { o, d }, d a unit vector.
    ray(x, y) {
        const { eye, r, u, f } = this.basis(), [w, h] = this.size(), a = w / h, t = Math.tan(FOV / 2);
        const nx = x / w * 2 - 1, ny = 1 - y / h * 2;
        if (this.camera.ortho) {
            const half = this.camera.dist * t;
            return { o: add(eye, add(scale(r, nx * half * a), scale(u, ny * half))), d: f };
        }
        return { o: eye, d: norm(add(f, add(scale(r, nx * t * a), scale(u, ny * t)))) };
    }

    // Where the pointer falls on the plane through p that faces the camera.
    onPlane(x, y, p) {
        const { o, d } = this.ray(x, y), { f } = this.basis();
        return add(o, scale(d, dot(sub(p, o), f) / dot(d, f)));
    }

    // The point of the line through p along axis (unit) nearest to the
    // pointer's ray; null when the line runs along the ray.
    onAxis(x, y, p, axis) {
        const { o, d } = this.ray(x, y), w = sub(o, p), k = dot(d, axis), den = 1 - k * k;
        if (den < 1e-4)
            return null;
        return add(p, scale(axis, (dot(w, axis) - k * dot(w, d)) / den));
    }

    orbit(dx, dy) {
        const c = this.camera;
        c.yaw -= dx * 0.01;
        c.pitch = Math.max(-1.55, Math.min(1.55, c.pitch + dy * 0.01));
        this.draw();
    }

    pan(dx, dy) {
        const c = this.camera, { r, u } = this.basis(), k = 2 * c.dist * Math.tan(FOV / 2) / this.size()[1];
        c.target = add(c.target, add(scale(r, -dx * k), scale(u, dy * k)));
        this.draw();
    }

    zoom(factor) {
        this.camera.dist = Math.max(0.1, Math.min(50, this.camera.dist * factor));
        this.draw();
    }

    pickVertex(x, y, radius = 10) {
        let best = null, bestD = radius;
        this.doc?.vertices.forEach((v, i) => {
            const p = this.project(xyz(v));
            if (!p)
                return;
            const d = Math.hypot(p.x - x, p.y - y);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        });
        return best;
    }

    pickSpring(x, y, radius = 6) {
        let best = null, bestD = radius;
        this.doc?.springs.forEach((s, i) => {
            const a = this.project(xyz(this.doc.vertices[s.a])), b = this.project(xyz(this.doc.vertices[s.b]));
            if (!a || !b)
                return;
            const dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy || 1;
            const t = Math.max(0, Math.min(1, ((x - a.x) * dx + (y - a.y) * dy) / len2));
            const d = Math.hypot(a.x + t * dx - x, a.y + t * dy - y);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        });
        return best;
    }

    // The nearest polygon drawn under the pointer (index in doc.polygons).
    pickPolygon(x, y) {
        const { o, d } = this.ray(x, y);
        let best = null, bestT = Infinity;
        for (const { index, pts } of this.polys)
            for (let i = 1; i + 1 < pts.length; i++) {
                const a = xyz(pts[0]), e1 = sub(xyz(pts[i]), a), e2 = sub(xyz(pts[i + 1]), a);
                if (!this.show.backs && dot(cross(e1, e2), d) > 0)
                    continue;
                const p = cross(d, e2), det = dot(e1, p);
                if (Math.abs(det) < 1e-12)
                    continue;
                const s = sub(o, a), u = dot(s, p) / det, q = cross(s, e1), v = dot(d, q) / det, t = dot(e2, q) / det;
                if (u >= 0 && v >= 0 && u + v <= 1 && t > 0 && t < bestT) {
                    bestT = t;
                    best = index;
                }
            }
        return best;
    }

    // The vertices inside a rectangle of pixels.
    verticesIn(x0, y0, x1, y1) {
        const [l, r] = [Math.min(x0, x1), Math.max(x0, x1)], [t, b] = [Math.min(y0, y1), Math.max(y0, y1)];
        const inside = [];
        this.doc?.vertices.forEach((v, i) => {
            const p = this.project(xyz(v));
            if (p && p.x >= l && p.x <= r && p.y >= t && p.y <= b)
                inside.push(i);
        });
        return inside;
    }

    draw() {
        if (this.queued)
            return;
        this.queued = true;
        requestAnimationFrame(() => {
            this.queued = false;
            this.drawNow();
        });
    }

    drawNow() {
        const dpr = window.devicePixelRatio || 1, [w, h] = this.size();
        for (const c of [this.canvas, this.overlay]) {
            const pw = Math.round(w * dpr), ph = Math.round(h * dpr);
            if (c.width !== pw || c.height !== ph) {
                c.width = pw;
                c.height = ph;
            }
        }
        if (this.gl)
            this.drawSolid(w / h);
        this.drawOverlay(dpr, w, h);
    }

    drawSolid(aspect) {
        const gl = this.gl, { eye, r, u, f } = this.basis(), c = this.camera, t = Math.tan(FOV / 2);
        gl.viewport(0, 0, this.canvas.width, this.canvas.height);
        gl.clearColor(0.09, 0.1, 0.12, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        if (!this.doc || !this.show.solid)
            return;
        const near = c.dist * 0.02, far = c.dist * 30;
        const ex = -dot(r, eye), ey = -dot(u, eye), ez = -dot(f, eye);
        let rows;
        if (c.ortho) {
            const half = c.dist * t, k = 2 / (far - near), z0 = -(far + near) / (far - near);
            rows = [[...r.map((v) => v / (half * aspect)), ex / (half * aspect)], [...u.map((v) => v / half), ey / half],
                [...f.map((v) => v * k), ez * k + z0], [0, 0, 0, 1]];
        } else {
            const A = (far + near) / (far - near), B = -2 * far * near / (far - near);
            rows = [[...r.map((v) => v / (t * aspect)), ex / (t * aspect)], [...u.map((v) => v / t), ey / t],
                [...f.map((v) => v * A), ez * A + B], [...f, ez]];
        }
        const matrix = new Float32Array(16);
        rows.forEach((row, y) => row.forEach((v, x) => { matrix[x * 4 + y] = v; }));

        gl.useProgram(this.program);
        const uniform = (name) => gl.getUniformLocation(this.program, name);
        gl.uniformMatrix4fv(uniform('viewProj'), false, matrix);
        gl.uniform2fv(uniform('texSize'), this.texSize);
        gl.enable(gl.DEPTH_TEST);
        gl.disable(gl.BLEND);
        gl.depthMask(true);
        const [body, wheels, glass] = this.ranges;
        const backs = this.show.backs ? 1 : 0;
        gl.uniform1i(uniform('backs'), backs);
        gl.drawArrays(gl.TRIANGLES, 0, body);
        gl.uniform1i(uniform('backs'), 2);      // the wheels are drawn whichever way they turn
        gl.drawArrays(gl.TRIANGLES, body, wheels);
        gl.uniform1i(uniform('backs'), backs);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.depthMask(false);
        gl.drawArrays(gl.TRIANGLES, body + wheels, glass);
        gl.depthMask(true);
    }

    drawOverlay(dpr, w, h) {
        const ctx = this.overlay.getContext('2d'), doc = this.doc;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, w, h);
        if (!doc)
            return;
        const line = (a, b) => {
            if (a && b) {
                ctx.moveTo(a.x, a.y);
                ctx.lineTo(b.x, b.y);
            }
        };

        // the ground, under the lowest wheel, and which way is the front
        const ground = doc.vertices.length ? Math.min(...doc.vertices.map((v) => v.z - v.radius)) : 0;
        ctx.strokeStyle = 'rgba(255,255,255,0.1)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let k = -6; k <= 6; k++) {
            line(this.project([k / 4, -1.5, ground]), this.project([k / 4, 1.5, ground]));
            line(this.project([-1.5, k / 4, ground]), this.project([1.5, k / 4, ground]));
        }
        ctx.stroke();
        const front = this.project([0, -1.65, ground]);
        if (front) {
            ctx.fillStyle = 'rgba(255,255,255,0.45)';
            ctx.font = '12px system-ui, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText('front', front.x, front.y);
        }

        const at = (i) => this.project(xyz(doc.vertices[i]));
        if (this.show.springs) {
            doc.springs.forEach((s, i) => {
                const hot = this.sel.spring === i || (this.hover?.type === 'spring' && this.hover.index === i);
                ctx.strokeStyle = hot ? '#fff' : SPRING_COLOURS[springKind(s)];
                ctx.globalAlpha = hot ? 1 : 0.55;
                ctx.lineWidth = hot ? 2.5 : 1;
                ctx.beginPath();
                line(at(s.a), at(s.b));
                ctx.stroke();
            });
            ctx.globalAlpha = 1;
        }

        const outline = (index, fill, stroke) => {
            const poly = this.polys.find((p) => p.index === index);
            if (!poly)
                return;
            ctx.beginPath();
            poly.pts.forEach((p) => {
                const s = this.project(xyz(p));
                if (s)
                    ctx.lineTo(s.x, s.y);
            });
            ctx.closePath();
            ctx.fillStyle = fill;
            ctx.fill();
            ctx.strokeStyle = stroke;
            ctx.lineWidth = 2;
            ctx.stroke();
        };
        if (this.hover?.type === 'polygon')
            outline(this.hover.index, 'rgba(255,255,255,0.15)', '#ccc');
        if (this.sel.polygon !== null)
            outline(this.sel.polygon, 'rgba(255,220,80,0.25)', '#ffdc50');

        ctx.textAlign = 'left';
        ctx.font = '11px ui-monospace, monospace';
        doc.vertices.forEach((v, i) => {
            const p = at(i);
            if (!p)
                return;
            const order = this.sel.vertices.indexOf(i), hot = this.hover?.type === 'vertex' && this.hover.index === i;
            ctx.beginPath();
            ctx.arc(p.x, p.y, order >= 0 ? 5.5 : 4, 0, 2 * Math.PI);
            ctx.fillStyle = order >= 0 ? '#ffdc50' : VERTEX_COLOURS[v.kind] || VERTEX_COLOURS['wheel front'];
            ctx.fill();
            ctx.strokeStyle = hot ? '#fff' : 'rgba(0,0,0,0.7)';
            ctx.lineWidth = hot ? 2 : 1;
            ctx.stroke();
            if (this.show.labels || hot || order >= 0) {
                const label = order >= 0 && this.sel.vertices.length > 1 ? `${i} (${order + 1})` : String(i);
                ctx.fillStyle = '#000';
                ctx.fillText(label, p.x + 8, p.y - 5);
                ctx.fillStyle = '#fff';
                ctx.fillText(label, p.x + 7, p.y - 6);
            }
        });

        if (this.box) {
            const [x0, y0, x1, y1] = this.box;
            ctx.strokeStyle = '#ffdc50';
            ctx.lineWidth = 1;
            ctx.strokeRect(x0, y0, x1 - x0, y1 - y0);
        }
    }
}
