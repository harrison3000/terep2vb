// Car files (carN.dat) to text and back.  The text format is not stable
// yet: it may change at any time (the page says so).  It has four sections:
//
//   section header     drive, the unknown word at 0x06, the game's state bytes (if not zero)
//   section vertices   vertex[i] = (x, y, z) radius r, body | reference | wheel front|back [velocity (x, y, z)]
//   section springs    spring vertex[a] - vertex[b]: type t shift s rest r last l min m max n
//   section program    the drawing program, one instruction a line, BASIC-like (below)
//
// Positions, velocities, radii and spring lengths are in tiles (a map cell,
// 256 map units: the OpenGL renderer's scale), texture coordinates in texels
// and shades in colours, all decimals; the file's fixed point (8.24, 8.8 and
// the springs' 1/16384 of a tile) is exact in them.
// The program's points are vertex[i], shadow[i] (the ground under it),
// axle[0] and axle[1] (vertices 0 and 1 moved along the steered front axle),
// or point[k] for any other index.  Jumps go to labels ("L123:", named after
// the byte offset they had, any name works); what cannot be shown as an
// instruction is kept as "data" bytes, so decoding and encoding again gives
// the same file.
//
//   pixel P color c                         a pixel
//   nearer P, P[, x]                        hidden = the first is nearer
//   facing P, P, P                          hidden = the triangle faces away
//   flat color c: P, P, ..., P              flat polygon (the last point closes it)
//   shaded: P s, P s, ...                   Gouraud polygon, shade per corner
//   shaded_tab base b: P i, P i, ...        Gouraud, shades from a table
//   textured: P (u, v), P (u, v), ...       textured polygon
//   sprite P size s: (u, v) (u, v) (u, v) (u, v)
//   wheel P, P[, x]                         then nine "view w0 w1: (u, v) x 4" lines
//   sort2 La, Lb                            both, nearest first
//   goto L / gosub L / if visible goto L / if hidden goto L
//   nop / end / data 0x.., 0x..

const HEADER = 0x84, VERTEX = 0x1c, SPRING = 0x0e;
const DRIVES = ['rear', 'front', 'all'];
const WHEELS = { 1: 'back', 2: 'front' };      // CarVertex::wheel (the game: 1 rear, 2 front)

const OP = {
    END: 0, PIXEL: 1, NEARER: 2, FACING: 3, FLAT: 4, NOP: 5, SHADED: 6, SHADED_TAB: 7, TEXTURED: 8,
    SPRITE: 9, WHEEL: 10, SORT2: 16, JUMP: 17, CALL: 18, JUMP_VISIBLE: 19, JUMP_HIDDEN: 20,
};
const JUMPS = { [OP.JUMP]: 'goto', [OP.CALL]: 'gosub', [OP.JUMP_VISIBLE]: 'if visible goto',
    [OP.JUMP_HIDDEN]: 'if hidden goto' };

// exact decimals of 16.16 and 8.8 values
const TILE = 1 << 24;                   // a vertex's position or velocity: 8.24 tiles
const RADIUS = 256;                     // its radius: map units
const LENGTH = 16384;                   // a spring's lengths
const tiles = (raw, unit = TILE) => String(raw / unit);
const fix8 = (raw) => String(raw / 256);

// ------------------------------------------------------------------------
// Decoding

export function decode(buffer) {
    const bytes = new Uint8Array(buffer), dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const size = bytes.length;
    const u16 = (at) => at + 2 <= size ? dv.getUint16(at, true) : 0;
    const i16 = (at) => at + 2 <= size ? dv.getInt16(at, true) : 0;
    const i32 = (at) => at + 4 <= size ? dv.getInt32(at, true) : 0;
    const out = [];

    const vo = u16(0), so = u16(2), mo = u16(4), drive = u16(8);
    out.push('section header');
    out.push(`drive ${DRIVES[drive] || drive}`);
    out.push(`unknown06 0x${u16(6).toString(16)}`);
    const state = bytes.subarray(0x0a, Math.min(HEADER, size));
    if (state.some((b) => b))
        out.push('state ' + [...state].map((b) => b.toString(16).padStart(2, '0')).join(' '));

    out.push('', 'section vertices');
    const nv = u16(vo);
    for (let i = 0, at = vo + 2; i < nv && at + VERTEX <= size; i++, at += VERTEX) {
        const wheel = u16(at + 26);
        let line = `vertex[${i}] = (${tiles(i32(at))}, ${tiles(i32(at + 4))}, ${tiles(i32(at + 8))})` +
            ` radius ${tiles(i16(at + 24), RADIUS)} ` + (wheel === 0 ? 'body' : wheel === 0xffff ? 'reference' : `wheel ${WHEELS[wheel] || wheel}`);
        if (i32(at + 12) || i32(at + 16) || i32(at + 20))
            line += ` velocity (${tiles(i32(at + 12))}, ${tiles(i32(at + 16))}, ${tiles(i32(at + 20))})`;
        out.push(line);
    }

    out.push('', 'section springs');
    const ns = u16(so);
    for (let i = 0, at = so + 2; i < ns && at + SPRING <= size; i++, at += SPRING)
        out.push(`spring vertex[${u16(at)}] - vertex[${u16(at + 2)}]: type ${bytes[at + 8]} shift ${bytes[at + 9]}` +
            ` rest ${tiles(i16(at + 4), LENGTH)} last ${tiles(i16(at + 6), LENGTH)}` +
            ` min ${tiles(u16(at + 10), LENGTH)} max ${tiles(u16(at + 12), LENGTH)}`);

    out.push('', 'section program');
    out.push(...decodeProgram(mo < size ? bytes.subarray(mo) : new Uint8Array(0), nv));
    return out.join('\n') + '\n';
}

// The instruction at pc of code: its length, or 0 if it is not one (a byte
// with the high bits set, an opcode the game does not know, cut short).
function instructionLength(code, pc) {
    const raw = code[pc], op = raw & 0x1f, n = code[pc + 1];
    if (raw & 0xe0)
        return 0;
    const len = {
        [OP.END]: 1, [OP.NOP]: 1, [OP.PIXEL]: 5, [OP.NEARER]: 7, [OP.FACING]: 7,
        [OP.FLAT]: 2 + (n + 1) * 2 + 2, [OP.SHADED]: 2 + (n + 1) * 4, [OP.TEXTURED]: 2 + (n + 1) * 6,
        [OP.SHADED_TAB]: 4 + (n + 1) * 4, [OP.SPRITE]: 21, [OP.WHEEL]: 1 + 0xba,
        [OP.SORT2]: 5, [OP.JUMP]: 3, [OP.CALL]: 3, [OP.JUMP_VISIBLE]: 3, [OP.JUMP_HIDDEN]: 3,
    }[op];
    return len && pc + len <= code.length ? len : 0;
}

function targets(code, pc) {
    const op = code[pc] & 0x1f, rd16 = (at) => code[at] | code[at + 1] << 8;
    if (op === OP.SORT2)
        return [(pc + 1 + rd16(pc + 1)) & 0xffff, (pc + 1 + rd16(pc + 3)) & 0xffff];
    if (JUMPS[op])
        return [(pc + 3 + rd16(pc + 1)) & 0xffff];
    return [];
}

function decodeProgram(code, nv) {
    // the instructions one after the other; jumps that land elsewhere than
    // on one, and what they stand on, become data
    const items = [];
    for (let pc = 0; pc < code.length;) {
        const len = instructionLength(code, pc);
        items.push({ pc, len: len || 1, data: !len });
        pc += len || 1;
    }
    for (let changed = true; changed;) {
        changed = false;
        const starts = new Set(items.filter((it) => !it.data).map((it) => it.pc));
        for (const it of items)
            if (!it.data && targets(code, it.pc).some((t) => !starts.has(t))) {
                it.data = true;
                changed = true;
            }
    }
    const labels = new Set();
    for (const it of items)
        if (!it.data)
            targets(code, it.pc).forEach((t) => labels.add(t));

    const rd16 = (at) => code[at] | code[at + 1] << 8;
    const s16 = (at) => (rd16(at) << 16) >> 16;
    const point = (k) => k < 2 * nv ? (k & 1 ? `shadow[${k >> 1}]` : `vertex[${k >> 1}]`)
        : k === 2 * nv ? 'axle[0]' : k === 2 * nv + 1 ? 'axle[1]' : `point[${k}]`;
    const uv = (at) => `(${fix8(rd16(at))}, ${fix8(rd16(at + 2))})`;
    const lines = [];
    let data = [];
    const flush = () => {
        for (let i = 0; i < data.length; i += 16)
            lines.push('    data ' + data.slice(i, i + 16).map((b) => '0x' + b.toString(16).padStart(2, '0')).join(', '));
        data = [];
    };
    for (const it of items) {
        if (labels.has(it.pc)) {
            flush();
            lines.push(`L${it.pc}:`);
        }
        if (it.data) {
            data.push(...code.subarray(it.pc, it.pc + it.len));
            continue;
        }
        flush();
        const pc = it.pc, op = code[pc] & 0x1f, n = code[pc + 1] + 1;
        const corners = (from, step, attr) => {
            const c = [];
            for (let i = 0, at = from; i < n; i++, at += step)
                c.push(point(rd16(at)) + (attr ? ' ' + attr(at + 2) : ''));
            return c.join(', ');
        };
        let line;
        switch (op) {
        case OP.END: line = 'end'; break;
        case OP.NOP: line = 'nop'; break;
        case OP.PIXEL: line = `pixel ${point(rd16(pc + 1))} color ${rd16(pc + 3)}`; break;
        case OP.NEARER:
        case OP.FACING: {
            const third = rd16(pc + 5);
            line = op === OP.FACING ? `facing ${point(rd16(pc + 1))}, ${point(rd16(pc + 3))}, ${point(third)}`
                : `nearer ${point(rd16(pc + 1))}, ${point(rd16(pc + 3))}` + (third ? `, ${third}` : '');
            break;
        }
        case OP.FLAT:
            line = `flat color 0x${rd16(pc + 2 + n * 2).toString(16).padStart(4, '0')}: ${corners(pc + 2, 2)}`;
            break;
        case OP.SHADED: line = `shaded: ${corners(pc + 2, 4, (at) => fix8(rd16(at)))}`; break;
        case OP.SHADED_TAB:
            line = `shaded_tab base ${fix8(rd16(pc + 2))}: ${corners(pc + 4, 4, (at) => String(rd16(at)))}`;
            break;
        case OP.TEXTURED: line = `textured: ${corners(pc + 2, 6, uv)}`; break;
        case OP.SPRITE:
            line = `sprite ${point(rd16(pc + 1))} size ${s16(pc + 3)}: ` +
                [0, 1, 2, 3].map((i) => uv(pc + 5 + 4 * i)).join(' ');
            break;
        case OP.WHEEL: {
            const third = rd16(pc + 5);
            line = `wheel ${point(rd16(pc + 1))}, ${point(rd16(pc + 3))}` + (third ? `, ${third}` : '');
            for (let v = 0; v < 9; v++) {
                const at = pc + 7 + v * 20;
                line += `\n        view ${s16(at)} ${s16(at + 2)}: ` + [0, 1, 2, 3].map((i) => uv(at + 4 + 4 * i)).join(' ');
            }
            break;
        }
        case OP.SORT2: {
            const [a, b] = targets(code, pc);
            line = `sort2 L${a}, L${b}`;
            break;
        }
        default:
            line = `${JUMPS[op]} L${targets(code, pc)[0]}`;
        }
        lines.push('    ' + line);
    }
    flush();
    return lines;
}

// ------------------------------------------------------------------------
// Encoding

export class CarTextError extends Error {
    constructor(line, message) {
        super(`line ${line}: ${message}`);
        this.line = line;
    }
}

const NUM = String.raw`-?(?:\d+(?:\.\d*)?|\.\d+)`;
const HEXNUM = String.raw`(?:0x[0-9a-f]+|-?\d+)`;
const POINT = String.raw`(vertex|shadow|axle|point)\[(\d+)\]`;
const UV = String.raw`\(\s*(${NUM})\s*,\s*(${NUM})\s*\)`;

const int = (s) => parseInt(s, s.startsWith('0x') || s.startsWith('-0x') ? 16 : 10);

export function encode(text) {
    const sections = { header: [], vertices: [], springs: [], program: [] };
    let current = null;
    text.split('\n').forEach((raw, i) => {
        const line = raw.replace(/#.*/, '').trim();
        if (!line)
            return;
        const m = /^section\s+(\w+)$/i.exec(line);
        if (m) {
            current = m[1].toLowerCase();
            if (!(current in sections))
                throw new CarTextError(i + 1, `unknown section "${m[1]}"`);
            return;
        }
        if (!current)
            throw new CarTextError(i + 1, 'text before the first section');
        sections[current].push({ no: i + 1, text: line });
    });

    // header
    let drive = 2, unknown06 = 0x82, state = new Uint8Array(HEADER - 0x0a);
    for (const { no, text: l } of sections.header) {
        let m;
        if ((m = /^drive\s+(\w+)$/i.exec(l)))
            drive = DRIVES.includes(m[1].toLowerCase()) ? DRIVES.indexOf(m[1].toLowerCase()) : int(m[1]);
        else if ((m = new RegExp(`^unknown06\\s+(${HEXNUM})$`, 'i').exec(l)))
            unknown06 = int(m[1]);
        else if ((m = /^state\s+([0-9a-f\s]+)$/i.exec(l))) {
            const b = m[1].trim().split(/\s+/).map((h) => parseInt(h, 16));
            if (b.length !== state.length)
                throw new CarTextError(no, `state needs ${state.length} bytes`);
            state = Uint8Array.from(b);
        } else
            throw new CarTextError(no, 'expected drive, unknown06 or state');
        if (Number.isNaN(drive) || Number.isNaN(unknown06))
            throw new CarTextError(no, 'bad number');
    }

    // vertices
    const vertexRe = new RegExp(String.raw`^vertex\[(\d+)\]\s*=\s*\(\s*(${NUM})\s*,\s*(${NUM})\s*,\s*(${NUM})\s*\)` +
        String.raw`\s+radius\s+(${NUM})\s+(body|reference|wheel\s+(front|back|\d+))` +
        String.raw`(?:\s+velocity\s*\(\s*(${NUM})\s*,\s*(${NUM})\s*,\s*(${NUM})\s*\))?$`, 'i');
    const vertices = sections.vertices.map(({ no, text: l }, i) => {
        const m = vertexRe.exec(l);
        if (!m)
            throw new CarTextError(no, 'expected vertex[i] = (x, y, z) radius r body|reference|wheel front|back [velocity (x, y, z)]');
        if (+m[1] !== i)
            throw new CarTextError(no, `expected vertex[${i}] (vertices are numbered in order)`);
        const kind = m[6].toLowerCase();
        return { pos: [m[2], m[3], m[4]].map((v) => Math.round(+v * TILE)), radius: Math.round(+m[5] * RADIUS),
            wheel: kind === 'body' ? 0 : kind === 'reference' ? 0xffff
                : m[7].toLowerCase() === 'front' ? 2 : m[7].toLowerCase() === 'back' ? 1 : +m[7],
            vel: m[8] ? [m[8], m[9], m[10]].map((v) => Math.round(+v * TILE)) : [0, 0, 0] };
    });
    const nv = vertices.length;

    // springs
    const springRe = new RegExp(String.raw`^spring\s+vertex\[(\d+)\]\s*-\s*vertex\[(\d+)\]\s*:` +
        String.raw`\s*type\s+(\d+)\s+shift\s+(\d+)\s+rest\s+(${NUM})\s+last\s+(${NUM})\s+min\s+(${NUM})\s+max\s+(${NUM})$`, 'i');
    const springs = sections.springs.map(({ no, text: l }) => {
        const m = springRe.exec(l);
        if (!m)
            throw new CarTextError(no, 'expected spring vertex[a] - vertex[b]: type t shift s rest r last l min m max n');
        const [a, b, type, shift] = m.slice(1, 5).map(Number);
        return [a, b, type, shift, ...m.slice(5).map((v) => Math.round(+v * LENGTH))];
    });

    const program = encodeProgram(sections.program, nv);

    // the file: header, vertices, springs, program
    const vo = HEADER, so = vo + 2 + nv * VERTEX, mo = so + 2 + springs.length * SPRING;
    const bytes = new Uint8Array(mo + program.length), dv = new DataView(bytes.buffer);
    dv.setUint16(0, vo, true);
    dv.setUint16(2, so, true);
    dv.setUint16(4, mo, true);
    dv.setUint16(6, unknown06, true);
    dv.setUint16(8, drive, true);
    bytes.set(state, 0x0a);
    dv.setUint16(vo, nv, true);
    vertices.forEach((v, i) => {
        const at = vo + 2 + i * VERTEX;
        v.pos.forEach((x, k) => dv.setInt32(at + 4 * k, x, true));
        v.vel.forEach((x, k) => dv.setInt32(at + 12 + 4 * k, x, true));
        dv.setInt16(at + 24, v.radius, true);
        dv.setUint16(at + 26, v.wheel, true);
    });
    dv.setUint16(so, springs.length, true);
    springs.forEach(([a, b, type, shift, rest, last, min, max], i) => {
        const at = so + 2 + i * SPRING;
        dv.setUint16(at, a, true);
        dv.setUint16(at + 2, b, true);
        dv.setInt16(at + 4, rest, true);
        dv.setInt16(at + 6, last, true);
        bytes[at + 8] = type;
        bytes[at + 9] = shift;
        dv.setUint16(at + 10, min, true);
        dv.setUint16(at + 12, max, true);
    });
    bytes.set(program, mo);
    return bytes;
}

function encodeProgram(lines, nv) {
    const pointIndex = (no, kind, i) => {
        i = +i;
        switch (kind.toLowerCase()) {
        case 'vertex': return 2 * i;
        case 'shadow': return 2 * i + 1;
        case 'axle':
            if (i > 1)
                throw new CarTextError(no, 'axle[0] or axle[1]');
            return 2 * nv + i;
        default: return i;
        }
    };
    const fixed8 = (s) => Math.round(+s * 256) & 0xffff;

    // each line into bytes, with the labels to fill in (offsets in it)
    const items = [], labels = new Map();
    let pc = 0;
    for (let k = 0; k < lines.length; k++) {
        const { no, text: l } = lines[k];
        let m;
        if ((m = /^(\w+):$/.exec(l))) {
            if (labels.has(m[1]))
                throw new CarTextError(no, `label ${m[1]} twice`);
            labels.set(m[1], pc);
            continue;
        }
        const b = [], fix = [];         // bytes; [offset in b, label, base of the relative offset]
        const w16 = (v) => b.push(v & 0xff, (v >> 8) & 0xff);
        const points = (s, attr) => {
            // P [attr], P [attr], ...: returns [index, attr text]s
            const re = new RegExp(`${POINT}${attr ? `\\s*(${attr})` : ''}\\s*(?:,|$)`, 'gi');
            const out = [];
            let last = 0, r;
            while ((r = re.exec(s)) && r.index === last) {
                out.push([pointIndex(no, r[1], r[2]), r[3]]);
                last = re.lastIndex;
                while (s[last] === ' ') last++;
                re.lastIndex = last;
            }
            if (last !== s.length || !out.length)
                throw new CarTextError(no, 'bad list of points');
            if (out.length > 256)
                throw new CarTextError(no, 'at most 256 corners');
            return out;
        };
        const uvs = (s, count) => {
            const re = new RegExp(UV, 'g'), out = [];
            let r;
            while ((r = re.exec(s)))
                out.push([fixed8(r[1]), fixed8(r[2])]);
            if (out.length !== count)
                throw new CarTextError(no, `expected ${count} (u, v)`);
            return out;
        };
        const P = (s) => {
            const r = new RegExp(`^${POINT}$`, 'i').exec(s.trim());
            if (!r)
                throw new CarTextError(no, `expected a point, not "${s.trim()}"`);
            return pointIndex(no, r[1], r[2]);
        };

        if (/^end$/i.test(l)) b.push(OP.END);
        else if (/^nop$/i.test(l)) b.push(OP.NOP);
        else if ((m = /^data\s+(.+)$/i.exec(l)))
            m[1].split(',').forEach((x) => {
                const v = int(x.trim());
                if (Number.isNaN(v) || v < 0 || v > 255)
                    throw new CarTextError(no, 'data bytes are 0..255');
                b.push(v);
            });
        else if ((m = new RegExp(`^pixel\\s+(.+?)\\s+color\\s+(${HEXNUM})$`, 'i').exec(l))) {
            b.push(OP.PIXEL); w16(P(m[1])); w16(int(m[2]));
        } else if ((m = /^(nearer|facing)\s+(.+)$/i.exec(l))) {
            const parts = m[2].split(',');
            const facing = m[1].toLowerCase() === 'facing';
            if (parts.length !== 3 && !(parts.length === 2 && !facing))
                throw new CarTextError(no, facing ? 'facing P, P, P' : 'nearer P, P[, x]');
            b.push(facing ? OP.FACING : OP.NEARER);
            w16(P(parts[0])); w16(P(parts[1]));
            w16(parts.length < 3 ? 0 : facing ? P(parts[2]) : int(parts[2].trim()));
        } else if ((m = new RegExp(`^flat\\s+color\\s+(${HEXNUM})\\s*:\\s*(.+)$`, 'i').exec(l))) {
            const c = points(m[2]);
            b.push(OP.FLAT, c.length - 1);
            c.forEach(([p]) => w16(p));
            w16(int(m[1]));
        } else if ((m = /^shaded\s*:\s*(.+)$/i.exec(l))) {
            const c = points(m[1], NUM);
            b.push(OP.SHADED, c.length - 1);
            c.forEach(([p, s]) => { w16(p); w16(fixed8(s)); });
        } else if ((m = new RegExp(`^shaded_tab\\s+base\\s+(${NUM})\\s*:\\s*(.+)$`, 'i').exec(l))) {
            const c = points(m[2], String.raw`\d+`);
            b.push(OP.SHADED_TAB, c.length - 1);
            w16(fixed8(m[1]));
            c.forEach(([p, i]) => { w16(p); w16(+i); });
        } else if ((m = /^textured\s*:\s*(.+)$/i.exec(l))) {
            const c = points(m[1], UV);
            b.push(OP.TEXTURED, c.length - 1);
            c.forEach(([p, t]) => { const [[u, v]] = uvs(t, 1); w16(p); w16(u); w16(v); });
        } else if ((m = /^sprite\s+(.+?)\s+size\s+(-?\d+)\s*:\s*(.+)$/i.exec(l))) {
            b.push(OP.SPRITE); w16(P(m[1])); w16(+m[2]);
            uvs(m[3], 4).forEach(([u, v]) => { w16(u); w16(v); });
        } else if ((m = /^wheel\s+(.+)$/i.exec(l))) {
            const parts = m[1].split(',');
            if (parts.length < 2 || parts.length > 3)
                throw new CarTextError(no, 'wheel P, P[, x]');
            b.push(OP.WHEEL); w16(P(parts[0])); w16(P(parts[1])); w16(parts.length > 2 ? int(parts[2].trim()) : 0);
            for (let v = 0; v < 9; v++) {
                const next = lines[++k];
                const r = next && /^view\s+(-?\d+)\s+(-?\d+)\s*:\s*(.+)$/i.exec(next.text);
                if (!r)
                    throw new CarTextError(next ? next.no : no, 'a wheel needs nine "view w0 w1: (u, v) x 4" lines');
                w16(+r[1]); w16(+r[2]);
                uvs(r[3], 4).forEach(([u, vv]) => { w16(u); w16(vv); });
            }
        } else if ((m = /^sort2\s+(\w+)\s*,\s*(\w+)$/i.exec(l))) {
            b.push(OP.SORT2, 0, 0, 0, 0);
            fix.push([1, m[1], pc + 1], [3, m[2], pc + 1]);
        } else if ((m = /^(goto|gosub|if\s+visible\s+goto|if\s+hidden\s+goto)\s+(\w+)$/i.exec(l))) {
            const kind = m[1].toLowerCase().replace(/\s+/g, ' ');
            const op = { goto: OP.JUMP, gosub: OP.CALL, 'if visible goto': OP.JUMP_VISIBLE,
                'if hidden goto': OP.JUMP_HIDDEN }[kind];
            b.push(op, 0, 0);
            fix.push([1, m[2], pc + 3]);
        } else if (/^view\s/i.test(l))
            throw new CarTextError(no, 'a view line outside a wheel');
        else
            throw new CarTextError(no, `unknown instruction "${l}"`);
        items.push({ no, b, fix });
        pc += b.length;
    }

    const code = new Uint8Array(pc);
    let at = 0;
    for (const { no, b, fix } of items) {
        for (const [off, label, base] of fix) {
            if (!labels.has(label))
                throw new CarTextError(no, `no label ${label}`);
            const rel = (labels.get(label) - base) & 0xffff;
            b[off] = rel & 0xff;
            b[off + 1] = rel >> 8;
        }
        code.set(b, at);
        at += b.length;
    }
    return code;
}
