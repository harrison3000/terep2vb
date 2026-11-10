// Wavefront .obj files for the editor: their vertices and faces, put in the
// place and at the size of a car and reduced to what a car can hold.

import { snap, RADIUS_UNIT } from './cardoc.js';

const MAX_CORNERS = 8;                  // of a polygon: more become a fan of triangles

// lowest and highest of a coordinate (a mesh can be too big to spread into Math.min)
const bounds = (points, k) => points.reduce(([lo, hi], p) => [Math.min(lo, p[k]), Math.max(hi, p[k])], [Infinity, -Infinity]);

// { points: [[x, y, z]...], faces: [[index...]...] }; materials and texture
// coordinates are left out.
export function parseObj(text) {
    const points = [], faces = [];
    for (const raw of text.split(/\r?\n/)) {
        const t = raw.trim().split(/\s+/);
        if (t[0] === 'v' && t.length >= 4) {
            const p = [+t[1], +t[2], +t[3]];
            if (p.some(Number.isNaN))
                throw new Error(`bad vertex: ${raw.trim()}`);
            points.push(p);
        } else if (t[0] === 'f' && t.length >= 4) {
            const face = t.slice(1).map((w) => {
                const k = parseInt(w);
                return k < 0 ? points.length + k : k - 1;
            });
            if (face.some((k) => Number.isNaN(k) || k < 0 || k >= points.length))
                throw new Error(`bad face: ${raw.trim()}`);
            faces.push(face);
        }
    }
    if (!faces.length)
        throw new Error('no faces in it');
    return { points, faces };
}

// The mesh in the car's axes (x across, y along with the front at -y, z up),
// always a rotation, so that its faces keep facing out; up is the file's 'y'
// (its front at -z, as Blender exports) or 'z' (its front at -y).  turn: more
// quarter turns around the vertical.  Then length long, in the middle of the
// car (centreY along it), its lowest point at bottom.
function place(points, { up, turn, length, centreY, bottom }) {
    const out = points.map(([x, y, z]) => {
        let p = up === 'y' ? [-x, z, y] : [x, y, z];
        for (let k = 0; k < turn; k++)
            p = [p[1], -p[0], p[2]];
        return p;
    });
    const [low, high] = [0, 1].map((end) => [0, 1, 2].map((k) => bounds(out, k)[end]));
    const s = length / (high[1] - low[1] || 1);
    const at = [-(low[0] + high[0]) / 2 * s, centreY - (low[1] + high[1]) / 2 * s, bottom - low[2] * s];
    return out.map((p) => p.map((c, k) => snap(c * s + at[k], RADIUS_UNIT)));
}

// The vertices of a grid of cells joined: for each point, its group, and how many.
function cluster(points, cell) {
    const groups = new Map();
    const of = points.map((p) => {
        const key = p.map((c) => Math.floor(c / cell)).join();
        if (!groups.has(key))
            groups.set(key, groups.size);
        return groups.get(key);
    });
    return { of, count: groups.size };
}

// The smallest grid that leaves at most budget vertices.
function reduce(points, budget) {
    const extent = Math.max(...[0, 1, 2].map((k) => bounds(points, k)[1] - bounds(points, k)[0]));
    let best = cluster(points, RADIUS_UNIT);
    if (best.count <= budget)
        return best;
    let lo = RADIUS_UNIT, hi = extent * 2;
    best = cluster(points, hi);
    for (let i = 0; i < 30; i++) {
        const mid = (lo + hi) / 2, c = cluster(points, mid);
        if (c.count <= budget) {
            hi = mid;
            if (c.count >= best.count)
                best = c;
        } else
            lo = mid;
    }
    return best;
}

function area(points, face) {
    const n = [0, 0, 0];
    face.forEach((i, k) => {
        const a = points[i], b = points[face[(k + 1) % face.length]];
        n[0] += (a[1] - b[1]) * (a[2] + b[2]);
        n[1] += (a[2] - b[2]) * (a[0] + b[0]);
        n[2] += (a[0] - b[0]) * (a[1] + b[1]);
    });
    return Math.hypot(...n);
}

// The obj as a car can have it, at most budget vertices: { points, faces },
// the faces from the lowest up, since the game draws them in order.
export function fitMesh(obj, options, budget) {
    const used = [...new Set(obj.faces.flat())];
    const index = new Map(used.map((v, i) => [v, i]));
    const placed = place(used.map((v) => obj.points[v]), options);
    const { of, count } = reduce(placed, budget);

    const sum = Array.from({ length: count }, () => [0, 0, 0, 0]);
    placed.forEach((p, i) => {
        const s = sum[of[i]];
        p.forEach((c, k) => { s[k] += c; });
        s[3]++;
    });
    const merged = sum.map((s) => [0, 1, 2].map((k) => snap(s[k] / s[3], RADIUS_UNIT)));

    const seen = new Set(), faces = [];
    for (const face of obj.faces) {
        const f = face.map((v) => of[index.get(v)]).filter((v, k, a) => v !== a[(k + 1) % a.length]);
        if (f.length < 3 || new Set(f).size < 3 || area(merged, f) < 1e-6)
            continue;
        const key = [...f].sort((a, b) => a - b).join();
        if (seen.has(key))
            continue;
        seen.add(key);
        if (f.length <= MAX_CORNERS)
            faces.push(f);
        else
            for (let k = 1; k + 1 < f.length; k++)
                faces.push([f[0], f[k], f[k + 1]]);
    }
    if (!faces.length)
        throw new Error('nothing of it is left at the size a car can hold');

    // only the vertices that still have a face
    const kept = [...new Set(faces.flat())], number = new Map(kept.map((v, i) => [v, i]));
    const height = (f) => f.reduce((s, v) => s + merged[v][2], 0) / f.length;
    faces.sort((a, b) => height(a) - height(b));
    return { points: kept.map((v) => merged[v]), faces: faces.map((f) => f.map((v) => number.get(v))) };
}
