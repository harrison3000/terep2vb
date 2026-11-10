// A car's text (cartool.js) as something to edit: its vertices, springs and
// polygons as objects, each tied to its line, so that what is not edited
// stays as it was written and a file opened and saved unchanged comes out
// the same.  Moving things only changes the objects (marked dirty, written
// to their lines by text()); adding and removing changes the lines and
// reads them again.

import { encode, CarTextError } from './cartool.js';

const NUM = String.raw`-?(?:\d+(?:\.\d*)?|\.\d+)`;
const HEXNUM = String.raw`(?:0x[0-9a-f]+|-?\d+)`;
const POINT = String.raw`(vertex|shadow|axle|point)\[(\d+)\]`;
const UV = String.raw`\(\s*(${NUM})\s*,\s*(${NUM})\s*\)`;

const VERTEX_RE = new RegExp(String.raw`^vertex\[\d+\]\s*=\s*\(\s*(${NUM})\s*,\s*(${NUM})\s*,\s*(${NUM})\s*\)` +
    String.raw`\s+radius\s+(${NUM})\s+(body|reference|wheel\s+(?:front|back|\d+))(?:\s+velocity\s*(\(.*\)))?$`, 'i');
const SPRING_RE = new RegExp(String.raw`^spring\s+vertex\[(\d+)\]\s*-\s*vertex\[(\d+)\]\s*:\s*type\s+(\d+)\s+shift\s+(\d+)` +
    String.raw`\s+rest\s+(${NUM})\s+last\s+(${NUM})\s+min\s+(${NUM})\s+max\s+(${NUM})$`, 'i');
const FLAT_RE = new RegExp(`^flat\\s+color\\s+(${HEXNUM})\\s*:\\s*(.+)$`, 'i');
const SHADED_TAB_RE = new RegExp(`^shaded_tab\\s+base\\s+(${NUM})\\s*:\\s*(.+)$`, 'i');

// what the file can hold exactly: positions 8.24, radii 8.8, spring lengths 1/16384
export const POSITION_UNIT = 1 / (1 << 24), RADIUS_UNIT = 1 / 256, LENGTH_UNIT = 1 / 16384;
export const MAX_FILE_SIZE = 10000;     // the game reads at most this much
export const FIRST_FREE_VERTEX = 8;     // 0..3 are the wheels and 4..7 the body's axes, by number

export const snap = (v, unit) => Math.round(v / unit) * unit;

const comment = (raw) => (/\s*#.*/.exec(raw) || [''])[0];
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
const named = (c) => c.filter((p) => p.kind === 'vertex' || p.kind === 'shadow').map((p) => p.i);

function corners(s, attr) {
    const re = new RegExp(`${POINT}${attr ? String.raw`\s*(${attr})` : ''}`, 'gi');
    return [...s.matchAll(re)].map((m) => ({ kind: m[1].toLowerCase(), i: +m[2], attr: m[3],
        uv: m[4] !== undefined ? [+m[4], +m[5]] : null }));
}

function vertexText(i, v) {
    return `vertex[${i}] = (${v.x}, ${v.y}, ${v.z}) radius ${v.radius} ${v.kind}` +
        (v.velocity ? ` velocity ${v.velocity}` : '');
}

function springText(s) {
    return `spring vertex[${s.a}] - vertex[${s.b}]: type ${s.type} shift ${s.shift}` +
        ` rest ${s.rest} last ${s.last} min ${s.min} max ${s.max}`;
}

// The items of a polygon's list, split at the commas outside parentheses.
function splitCorners(s) {
    const items = [];
    let depth = 0, from = 0;
    for (let i = 0; i < s.length; i++) {
        if (s[i] === '(') depth++;
        else if (s[i] === ')') depth--;
        else if (s[i] === ',' && !depth) {
            items.push(s.slice(from, i).trim());
            from = i + 1;
        }
    }
    items.push(s.slice(from).trim());
    return items;
}

export class CarDoc {
    constructor(text) {
        this.parse(text);
    }

    // Throws cartool's CarTextError (with the line) if the text is not a car.
    parse(text) {
        encode(text);
        this.lines = text.split('\n');
        this.vertices = [];
        this.springs = [];
        this.polygons = [];
        this.wheels = [];
        this.others = [];               // the other instructions that name vertices: { line, uses }
        this.sections = {};
        this.drive = 'all';
        this.driveLine = -1;
        let section = null, wheel = null;
        this.lines.forEach((raw, line) => {
            const l = raw.replace(/#.*/, '').trim();
            if (!l)
                return;
            const s = /^section\s+(\w+)$/i.exec(l);
            let m;
            if (s) {
                section = s[1].toLowerCase();
                this.sections[section] = line;
            } else if (section === 'header') {
                if ((m = /^drive\s+(\w+)$/i.exec(l))) {
                    this.drive = m[1].toLowerCase();
                    this.driveLine = line;
                }
            } else if (section === 'vertices') {
                m = VERTEX_RE.exec(l);
                this.vertices.push({ line, x: +m[1], y: +m[2], z: +m[3], radius: +m[4],
                    kind: m[5].toLowerCase().replace(/\s+/g, ' '), velocity: m[6] || null, dirty: false });
            } else if (section === 'springs') {
                m = SPRING_RE.exec(l);
                this.springs.push({ line, a: +m[1], b: +m[2], type: +m[3], shift: +m[4], rest: +m[5], last: +m[6],
                    min: +m[7], max: +m[8], dirty: false });
            } else if (section === 'program') {
                if (wheel && /^view\s/i.test(l)) {
                    wheel.views.push([...l.matchAll(new RegExp(UV, 'g'))].map((u) => [+u[1], +u[2]]));
                    return;
                }
                wheel = null;
                if ((m = FLAT_RE.exec(l)))
                    this.addPolygon('flat', line, corners(m[2]), parseInt(m[1]));
                else if ((m = /^shaded\s*:\s*(.+)$/i.exec(l))) {
                    const c = corners(m[1], NUM);
                    this.addPolygon('shaded', line, c, +c[0].attr);
                } else if ((m = SHADED_TAB_RE.exec(l)))
                    this.addPolygon('shaded_tab', line, corners(m[2], String.raw`\d+`), +m[1]);
                else if ((m = /^textured\s*:\s*(.+)$/i.exec(l)))
                    this.addPolygon('textured', line, corners(m[1], UV), 0);
                else if ((m = /^wheel\s+(.+)$/i.exec(l))) {
                    const c = corners(m[1]);
                    wheel = { line, at: c[0], axle: c[1], views: [] };
                    this.wheels.push(wheel);
                    this.others.push({ line, uses: new Set(named(c)) });
                } else if (/^(pixel|nearer|facing|sprite)\s/i.test(l))
                    this.others.push({ line, uses: new Set(named(corners(l))) });
            }
        });
        for (const s of this.springs)
            if (s.a >= this.vertices.length || s.b >= this.vertices.length)
                throw new CarTextError(s.line + 1, 'a spring on a vertex that does not exist');
    }

    // The game projects each vertex twice, and the axle's two, into a table of
    // 120 with no check; a shaded_tab polygon reads the last 20 of them.
    get maxVertices() {
        return this.polygons.some((p) => p.op === 'shaded_tab') ? 49 : 59;
    }

    addPolygon(op, line, c, colour) {
        this.polygons.push({ op, line, corners: c, colour, uses: new Set(named(c)) });
    }

    // Where a corner of the program is at rest; null for what has no place
    // here (the shadows, indices past the points).
    position(corner) {
        const v = this.vertices[corner.i];
        if (corner.kind === 'vertex')
            return v || null;
        if (corner.kind === 'axle' && v)
            return { x: v.x + (corner.i ? -0.5 : 0.5), y: v.y, z: v.z, radius: v.radius };
        return null;
    }

    text() {
        this.vertices.forEach((v, i) => {
            if (v.dirty)
                this.lines[v.line] = vertexText(i, v) + comment(this.lines[v.line]);
            v.dirty = false;
        });
        for (const s of this.springs) {
            if (s.dirty)
                this.lines[s.line] = springText(s) + comment(this.lines[s.line]);
            s.dirty = false;
        }
        return this.lines.join('\n');
    }

    reread() {
        this.parse(this.lines.join('\n'));
    }

    // Puts a line after another (by default right after the section's title).
    insert(section, text, after) {
        if (this.sections[section] === undefined) {
            this.lines.push(`section ${section}`);
            this.sections[section] = this.lines.length - 1;
        }
        this.lines.splice((after ?? this.sections[section]) + 1, 0, text);
    }

    // The springs on the given vertices take their length as it is now, with
    // their limits in the same proportion.
    refitSprings(moved) {
        for (const s of this.springs)
            if (moved.has(s.a) || moved.has(s.b))
                this.refit(s);
    }

    refit(s) {
        if (s.rest < 0)
            return;
        const rest = Math.min(Math.floor(distance(this.vertices[s.a], this.vertices[s.b]) / LENGTH_UNIT) * LENGTH_UNIT,
            32767 * LENGTH_UNIT);
        if (s.rest > 0) {
            const scale = rest / s.rest, limit = (v) => Math.min(snap(v * scale, LENGTH_UNIT), 65535 * LENGTH_UNIT);
            s.min = limit(s.min);
            s.max = limit(s.max);
        }
        s.rest = s.last = rest;
        s.dirty = true;
    }

    setDrive(drive) {
        this.text();
        if (this.driveLine < 0)
            this.insert('header', `drive ${drive}`);
        else
            this.lines[this.driveLine] = `drive ${drive}` + comment(this.lines[this.driveLine]);
        this.reread();
    }

    addVertex(x, y, z, radius = 0, kind = 'body') {
        this.text();
        const i = this.vertices.length;
        if (i >= this.maxVertices)
            throw new Error(`a car can have at most ${this.maxVertices} vertices`);
        this.insert('vertices', vertexText(i, { x, y, z, radius, kind }), this.vertices[i - 1]?.line);
        this.reread();
        return i;
    }

    // Removes a vertex, its springs and the polygons on it, and renumbers.
    // Refuses (throws) what the game or the program counts on.
    removeVertex(i) {
        this.text();
        const v = this.vertices[i];
        if (i < FIRST_FREE_VERTEX)
            throw new Error(`vertex[${i}]: the game uses vertices 0 to ${FIRST_FREE_VERTEX - 1} by number`);
        if (v.kind === 'reference')
            throw new Error(`vertex[${i}] is the reference vertex (the in-car camera)`);
        const used = this.others.find((o) => o.uses.has(i));
        if (used)
            throw new Error(`vertex[${i}] is used by: ${this.lines[used.line].trim().slice(0, 50)}`);
        const springs = this.springs.filter((s) => s.a === i || s.b === i);
        const polygons = this.polygons.filter((p) => p.uses.has(i));
        const gone = new Set([v.line, ...springs.map((s) => s.line), ...polygons.map((p) => p.line)]);
        this.lines = this.lines.filter((_, line) => !gone.has(line))
            .map((l) => l.replace(/\b(vertex|shadow)\[(\d+)\]/g, (all, kind, k) => +k > i ? `${kind}[${k - 1}]` : all));
        this.reread();
        return { springs: springs.length, polygons: polygons.length };
    }

    // A spring (type 1) between two vertices as they are now, with the
    // limits the game's cars have: half and one and a half of its length.
    addSpring(a, b) {
        if (a === b || this.springs.some((s) => (s.a === a && s.b === b) || (s.a === b && s.b === a)))
            throw new Error(`there already is a spring vertex[${a}] - vertex[${b}]`);
        this.text();
        const rest = Math.floor(distance(this.vertices[a], this.vertices[b]) / LENGTH_UNIT) * LENGTH_UNIT;
        if (rest >= 2)
            throw new Error('a spring can be at most 2 tiles long');
        this.insert('springs', springText({ a, b, type: 1, shift: 0, rest, last: rest,
            min: snap(rest / 2, LENGTH_UNIT), max: snap(rest * 1.5, LENGTH_UNIT) }), this.springs[this.springs.length - 1]?.line);
        this.reread();
        return this.springs.length - 1;
    }

    removeSpring(i) {
        this.text();
        this.lines.splice(this.springs[i].line, 1);
        this.reread();
    }

    // A flat polygon on the vertices, in their order or its reverse, so that
    // it faces out of the car (the game does not draw a polygon's back).  It
    // goes after the polygon that shares most corners with it: the game draws
    // in program order.
    addFlat(indices, colour) {
        if (new Set(indices).size < 3)
            throw new Error('a polygon needs at least three vertices');
        this.text();
        const v = indices.map((i) => this.vertices[i]);
        const centre = ['x', 'y', 'z'].map((k) => this.vertices.reduce((s, p) => s + p[k], 0) / this.vertices.length);
        const n = [0, 0, 0];
        v.forEach((a, i) => {
            const b = v[(i + 1) % v.length];
            n[0] += (a.y - b.y) * (a.z + b.z);
            n[1] += (a.z - b.z) * (a.x + b.x);
            n[2] += (a.x - b.x) * (a.y + b.y);
        });
        const out = v.reduce((s, p) => s + n[0] * (p.x - centre[0]) + n[1] * (p.y - centre[1]) + n[2] * (p.z - centre[2]), 0);
        const order = out < 0 ? [...indices].reverse() : indices;

        let best = -1, shared = 0;
        for (const p of this.polygons) {
            const count = indices.filter((i) => p.corners.some((c) => c.kind === 'vertex' && c.i === i)).length;
            if (count && count >= shared) {
                shared = count;
                best = p.line;
            }
        }
        if (best < 0) {
            const from = this.sections.program ?? this.lines.length;
            const end = this.lines.findIndex((l, line) => line > from && /^\s*end\s*(#.*)?$/i.test(l));
            best = end < 0 ? undefined : end - 1;
        }
        const list = [...order, order[0]].map((i) => `vertex[${i}]`).join(', ');
        this.insert('program', `    flat color 0x${colour.toString(16).padStart(4, '0')}: ${list}`, best);
        this.reread();
    }

    // A mesh ({ points, faces }: fitMesh) on the car: new body vertices, each
    // on four springs to the vertices of the body that were already there,
    // so that it goes with them; flat polygons, after the shadows.  replace:
    // the car's own polygons go (not the shadows or the wheels).
    addMesh({ points, faces }, colour, replace) {
        this.text();
        if (replace) {
            const gone = new Set(this.polygons.filter((p) => !p.corners.some((c) => c.kind === 'shadow')).map((p) => p.line));
            this.lines = this.lines.filter((_, line) => !gone.has(line));
            this.reread();
        }
        const first = this.vertices.length;
        if (first + points.length > this.maxVertices)
            throw new Error(`a car can have at most ${this.maxVertices} vertices`);
        const anchors = this.vertices.map((v, i) => ({ v, i })).filter(({ v }) => v.kind === 'body');
        if (anchors.length < 4)
            throw new Error('the mesh needs a body to hang on: at least four body vertices');
        this.lines.splice((this.vertices[first - 1]?.line ?? this.sections.vertices) + 1, 0,
            ...points.map(([x, y, z], k) => vertexText(first + k, { x, y, z, radius: 0, kind: 'body' })));
        this.reread();

        const springs = [];
        points.forEach(([x, y, z], k) => {
            const p = { x, y, z };
            const near = anchors.map((a) => ({ ...a, d: distance(a.v, p) })).filter((a) => a.d < 1.99).sort((a, b) => a.d - b.d);
            if (near.length < 4)
                throw new Error('the mesh is too far from the body to hang on it');
            // the three nearest, and the one that makes the widest solid with them
            const [a, b, c] = near, sub = (u, w) => [u.x - w.x, u.y - w.y, u.z - w.z];
            const u = sub(b.v, a.v), w = sub(c.v, a.v), n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
            const height = (o) => Math.abs(n[0] * (o.v.x - a.v.x) + n[1] * (o.v.y - a.v.y) + n[2] * (o.v.z - a.v.z));
            const fourth = near.slice(3).reduce((best, o) => height(o) > height(best) ? o : best);
            for (const anchor of [a, b, c, fourth]) {
                const rest = Math.floor(anchor.d / LENGTH_UNIT) * LENGTH_UNIT;
                springs.push(springText({ a: first + k, b: anchor.i, type: 1, shift: 0, rest, last: rest,
                    min: snap(rest / 2, LENGTH_UNIT), max: snap(rest * 1.5, LENGTH_UNIT) }));
            }
        });
        this.lines.splice((this.springs[this.springs.length - 1]?.line ?? this.sections.springs) + 1, 0, ...springs);
        this.reread();

        // after the first lines of the program that are shadows
        let at = this.sections.program ?? this.lines.length - 1;
        for (let line = at + 1; line < this.lines.length; line++) {
            const l = this.lines[line].replace(/#.*/, '').trim();
            if (l && !/^flat\b.*shadow\[/i.test(l))
                break;
            at = line;
        }
        const list = (f) => [...f, f[0]].map((k) => `vertex[${first + k}]`).join(', ');
        this.lines.splice(at + 1, 0, ...faces.map((f) => `    flat color 0x${colour.toString(16).padStart(4, '0')}: ${list(f)}`));
        this.reread();
    }

    removePolygon(i) {
        this.text();
        this.lines.splice(this.polygons[i].line, 1);
        this.reread();
    }

    // The corners the other way round: the polygon faces the other side.
    flipPolygon(i) {
        this.text();
        const line = this.polygons[i].line, raw = this.lines[line], tail = comment(raw);
        const body = raw.slice(0, raw.length - tail.length), colon = body.indexOf(':');
        const items = splitCorners(body.slice(colon + 1)).slice(0, -1).reverse();
        this.lines[line] = body.slice(0, colon + 1) + ' ' + [...items, items[0]].join(', ') + tail;
        this.reread();
    }

    setFlatColour(i, colour) {
        this.text();
        const line = this.polygons[i].line;
        this.lines[line] = this.lines[line].replace(/color\s+\S+?\s*:/i, `color 0x${colour.toString(16).padStart(4, '0')}:`);
        this.reread();
    }
}
