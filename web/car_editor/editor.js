// The car editor's page: the 3D view (carview.js) and the panel, working on
// a car's text (cardoc.js), which the Text tab shows as it is.

import { decode, encode } from './cartool.js';
import { CarDoc, snap, POSITION_UNIT, RADIUS_UNIT, LENGTH_UNIT, MAX_FILE_SIZE, FIRST_FREE_VERTEX } from './cardoc.js';
import { CarView, springKind } from './carview.js';
import { BASE_CAR } from './basecar.js';
import { parseObj, fitMesh } from './objimport.js';

const $ = (id) => document.getElementById(id);
const view = new CarView($('solid'), $('overlay'));

let doc = null, name = 'car1';
let mode = 'vertex', axis = null;
let history = [], place = -1;           // the texts to undo to, and where in them we are
let newColour = 0x0707;
let obj = null, objName = '', objPlace = -1;     // the last .obj, and the step of undo it made
const objOptions = { up: 'y', turn: 0, length: 0, replace: true };

function say(text, ok) {
    $('message').textContent = text;
    $('message').className = ok ? 'ok' : 'bad';
}

// ------------------------------------------------------------------------
// The car, and undo

function setDoc(text) {
    doc = new CarDoc(text);
    view.setDoc(doc);
}

function start(text) {
    setDoc(text);
    history = [doc.text()];
    place = 0;
    clearSelection();
    view.frame();
    $('empty').hidden = true;
    refresh();
}

// Keeps the text as it is now as a step of undo.
function commit() {
    const text = doc.text();
    if (text !== history[place]) {
        history.length = ++place;
        history.push(text);
        if (history.length > 200) {
            history.shift();
            place--;
        }
    }
    view.rebuild();
    refresh();
}

function travel(by) {
    if (!doc || place + by < 0 || place + by >= history.length)
        return;
    place += by;
    setDoc(history[place]);
    clearSelection();
    refresh();
}

// Runs an edit; what it refuses goes to the message.
function attempt(edit) {
    try {
        edit();
        return true;
    } catch (e) {
        say(e.message);
        return false;
    }
}

function clearSelection() {
    view.sel = { vertices: [], spring: null, polygon: null };
    view.hover = null;
}

// ------------------------------------------------------------------------
// Moving vertices

// The vertex opposite i across the car's middle: i itself on the middle, -1 if none.
function partner(i) {
    const v = doc.vertices[i], near = (a, b) => Math.abs(a - b) < 1 / 1024;
    if (!v)
        return -1;
    if (near(v.x, 0))
        return i;
    return doc.vertices.findIndex((p, j) => j !== i && near(p.x, -v.x) && near(p.y, v.y) && near(p.z, v.z));
}

const symmetric = () => $('symmetric').checked;

// With the symmetric mode on, the index of the spring on the other side of
// the car (-1: none, or it is its own).
function mirrorSpring(i) {
    if (!symmetric())
        return -1;
    const s = doc.springs[i], a = partner(s.a), b = partner(s.b);
    if (a < 0 || b < 0)
        return -1;
    return doc.springs.findIndex((o, j) => j !== i && ((o.a === a && o.b === b) || (o.a === b && o.b === a)));
}

// The same for a polygon.
function mirrorPolygon(i) {
    if (!symmetric())
        return -1;
    const opposite = (c) => {
        if (c.kind === 'axle')
            return `axle${1 - c.i}`;
        const p = c.kind === 'point' ? -1 : partner(c.i);
        return p < 0 ? null : `${c.kind}${p}`;
    };
    const f = doc.polygons[i], wanted = f.corners.slice(0, -1).map(opposite);
    if (wanted.includes(null))
        return -1;
    const set = wanted.sort().join();
    return doc.polygons.findIndex((o, j) => j !== i && o.op === f.op && o.corners.slice(0, -1).map((c) => `${c.kind}${c.i}`).sort().join() === set);
}

// An index and the one opposite it (if any), the highest first: as removing needs.
const withMirror = (i, m) => m >= 0 ? [Math.max(i, m), Math.min(i, m)] : [i];

// What moves with these vertices: { i: sign of its x movement }.  On the
// middle a vertex stays there, unless typed.
function moveSet(indices, typed) {
    const moves = new Map();
    for (const i of indices) {
        if (moves.has(i))
            continue;
        moves.set(i, 1);
        if (symmetric()) {
            const p = partner(i);
            if (p === i && !typed)
                moves.set(i, 0);
            else if (p >= 0 && p !== i && !moves.has(p))
                moves.set(p, -1);
        }
    }
    return moves;
}

function shift(moves, starts, dx, dy, dz, unit) {
    for (const [i, sign] of moves) {
        const v = doc.vertices[i], s = starts.get(i);
        v.x = snap(s.x + sign * dx, unit);
        v.y = snap(s.y + dy, unit);
        v.z = snap(s.z + dz, unit);
        v.dirty = true;
    }
}

const startsOf = (moves) => new Map([...moves.keys()].map((i) => [i, { x: doc.vertices[i].x, y: doc.vertices[i].y, z: doc.vertices[i].z }]));

function finishMove(moves) {
    if ($('follow').checked)
        doc.refitSprings(new Set(moves.keys()));
    commit();
}

// A coordinate typed in the panel.
function moveTyped(i, key, value) {
    const moves = moveSet([i], true), d = { x: 0, y: 0, z: 0 };
    d[key] = value - doc.vertices[i][key];
    shift(moves, startsOf(moves), d.x, d.y, d.z, POSITION_UNIT);
    finishMove(moves);
}

// ------------------------------------------------------------------------
// The pointer on the view

const overlay = $('overlay');
let drag = null;

const pos = (e) => {
    const r = overlay.getBoundingClientRect();
    return [e.clientX - r.left, e.clientY - r.top];
};

function underPointer(x, y) {
    let index;
    if (mode === 'vertex')
        return (index = view.pickVertex(x, y)) !== null ? { type: 'vertex', index } : null;
    if (mode === 'spring')
        return (index = view.pickSpring(x, y)) !== null ? { type: 'spring', index } : null;
    return (index = view.pickPolygon(x, y)) !== null ? { type: 'polygon', index } : null;
}

overlay.addEventListener('contextmenu', (e) => e.preventDefault());
overlay.addEventListener('pointerdown', (e) => {
    if (!doc)
        return;
    overlay.setPointerCapture(e.pointerId);
    const [x, y] = pos(e);
    drag = { x, y, lastX: x, lastY: y, moved: false, shiftKey: e.shiftKey, hit: null };
    if (e.button !== 0) {
        drag.kind = 'pan';
        return;
    }
    const hit = underPointer(x, y);
    drag.hit = hit;
    if (hit?.type === 'vertex' && !e.shiftKey) {
        if (!view.sel.vertices.includes(hit.index)) {
            view.sel.vertices = [hit.index];
            refresh();
        }
        drag.kind = 'move';
    } else if (!hit && e.shiftKey && mode === 'vertex')
        drag.kind = 'box';
    else
        drag.kind = hit ? 'click' : 'orbit';
});

overlay.addEventListener('pointermove', (e) => {
    const [x, y] = pos(e);
    if (!drag) {
        const hit = doc ? underPointer(x, y) : null;
        if (hit?.type !== view.hover?.type || hit?.index !== view.hover?.index) {
            view.hover = hit;
            view.draw();
        }
        return;
    }
    if (!drag.moved && Math.hypot(x - drag.x, y - drag.y) < 3)
        return;
    const first = !drag.moved;
    drag.moved = true;
    switch (drag.kind) {
    case 'pan':
        view.pan(x - drag.lastX, y - drag.lastY);
        break;
    case 'orbit':
    case 'click':
        view.orbit(x - drag.lastX, y - drag.lastY);
        break;
    case 'box':
        view.box = [drag.x, drag.y, x, y];
        view.draw();
        break;
    case 'move': {
        if (first) {
            drag.moves = moveSet(view.sel.vertices, false);
            drag.starts = startsOf(drag.moves);
            drag.grab = ['x', 'y', 'z'].map((k) => doc.vertices[drag.hit.index][k]);
        }
        const target = axis ? view.onAxis(x, y, drag.grab, { x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] }[axis])
            : view.onPlane(x, y, drag.grab);
        if (target) {
            shift(drag.moves, drag.starts, target[0] - drag.grab[0], target[1] - drag.grab[1], target[2] - drag.grab[2],
                $('snap').checked ? RADIUS_UNIT : POSITION_UNIT);
            view.rebuild();
            refreshFields();
        }
        break;
    }
    }
    drag.lastX = x;
    drag.lastY = y;
});

overlay.addEventListener('pointerup', () => {
    if (!drag)
        return;
    const d = drag;
    drag = null;
    if (d.kind === 'move' && d.moved) {
        finishMove(d.moves);
        return;
    }
    if (d.kind === 'box' && d.moved) {
        view.sel.vertices = [...new Set([...view.sel.vertices, ...view.verticesIn(...view.box)])];
        view.box = null;
        refresh();
        return;
    }
    if (!d.moved && d.kind !== 'pan')
        clickOn(d.hit, d.shiftKey);
});

overlay.addEventListener('pointercancel', () => {
    if (drag?.kind === 'move' && drag.moved)
        finishMove(drag.moves);
    drag = null;
    view.box = null;
    view.draw();
});

function clickOn(hit, shiftKey) {
    const sel = view.sel;
    if (!hit)
        clearSelection();
    else if (hit.type === 'vertex') {
        if (shiftKey) {
            const at = sel.vertices.indexOf(hit.index);
            if (at >= 0)
                sel.vertices.splice(at, 1);
            else
                sel.vertices.push(hit.index);
        } else
            sel.vertices = [hit.index];
    } else {
        clearSelection();
        view.sel[hit.type] = hit.index;
    }
    refresh();
}

overlay.addEventListener('wheel', (e) => {
    e.preventDefault();
    view.zoom(Math.exp(e.deltaY * 0.001));
}, { passive: false });

// ------------------------------------------------------------------------
// The panel

const el = (tag, props = {}, ...children) => {
    const e = Object.assign(document.createElement(tag), props);
    e.append(...children);
    return e;
};

function button(label, onclick, title = '') {
    return el('button', { textContent: label, onclick, title });
}

// A number field; apply gets the value when it is changed.
function field(label, value, step, apply, extra = {}) {
    const input = el('input', { type: 'number', step, value: String(value), ...extra });
    input.onchange = () => {
        const v = parseFloat(input.value);
        if (Number.isNaN(v))
            refresh();
        else
            apply(v);
    };
    return el('label', { className: 'field' }, el('span', { textContent: label }), input);
}

function note(text, className = 'small') {
    return el('p', { textContent: text, className });
}

function heading(text) {
    return el('h2', { textContent: text });
}

function vertexPanel(i) {
    const v = doc.vertices[i], p = $('panel');
    p.append(heading(`vertex[${i}]`));
    for (const key of ['x', 'y', 'z'])
        p.append(field(key, v[key], RADIUS_UNIT, (value) => moveTyped(i, key, value), { id: `field-${key}` }));
    p.append(field('radius', v.radius, RADIUS_UNIT, (value) => {
        const set = [i];
        if (symmetric() && partner(i) >= 0)
            set.push(partner(i));
        for (const k of set) {
            doc.vertices[k].radius = snap(value, RADIUS_UNIT);
            doc.vertices[k].dirty = true;
        }
        commit();
    }, { min: 0 }));
    if (i >= FIRST_FREE_VERTEX) {
        const kind = el('select', {}, ...['body', 'reference', 'wheel front', 'wheel back'].map((k) => el('option', { value: k, textContent: k })));
        kind.value = v.kind;
        kind.onchange = () => {
            const set = [i];
            if (symmetric() && partner(i) >= 0)
                set.push(partner(i));
            for (const k of set) {
                doc.vertices[k].kind = kind.value;
                doc.vertices[k].dirty = true;
            }
            commit();
        };
        p.append(el('label', { className: 'field' }, el('span', { textContent: 'kind' }), kind));
    } else
        p.append(note(`Vertices 0 to 3 are the wheels and 4 to 7 give the car its axes: the game uses them by number, so they cannot be removed. Kind: ${v.kind}.`));
    const n = doc.springs.filter((s) => s.a === i || s.b === i).length;
    const faces = doc.polygons.filter((f) => f.uses.has(i)).length;
    p.append(note(`${n} spring${n === 1 ? '' : 's'}, ${faces} polygon${faces === 1 ? '' : 's'} (shadows included).`));
    const actions = el('div', { className: 'actions' });
    if (partner(i) < 0)
        actions.append(button('Mirror copy', () => attempt(() => {
            view.sel.vertices = [doc.addVertex(-v.x, v.y, v.z, v.radius, v.kind === 'reference' ? 'body' : v.kind)];
            commit();
        }), 'A new vertex on the other side of the car'));
    actions.append(button('Remove', removeSelected));
    p.append(actions);
}

function verticesPanel() {
    const sel = view.sel.vertices, p = $('panel');
    p.append(heading(`${sel.length} vertices`));
    p.append(note(`In order: ${sel.join(', ')}. Shift-click to add or take out one; the order is the polygon's.`));
    const actions = el('div', { className: 'actions' });
    if (sel.length === 2)
        actions.append(button('Add spring between them', () => attempt(() => {
            const [a, b] = [partner(sel[0]), partner(sel[1])];
            const spring = doc.addSpring(sel[0], sel[1]);
            // the other side's, unless the spring crosses the middle and is its own
            if (symmetric() && a >= 0 && b >= 0 && !(a === sel[1] && b === sel[0]) && !(a === sel[0] && b === sel[1])
                && !doc.springs.some((s) => (s.a === a && s.b === b) || (s.a === b && s.b === a)))
                doc.addSpring(a, b);
            view.sel.vertices = [];
            commit();
            say(`Spring added: vertex[${doc.springs[spring].a}] - vertex[${doc.springs[spring].b}]`, true);
        })));
    if (sel.length >= 3)
        actions.append(button('Add polygon', () => attempt(() => {
            const other = sel.map(partner);
            doc.addFlat(sel, newColour);
            if (symmetric() && !other.includes(-1) && other.some((p) => !sel.includes(p)))
                doc.addFlat(other, newColour);
            clearSelection();
            commit();
            say('Polygon added; flip it in Faces if it shows its back', true);
        })));
    actions.append(button('Remove all', removeSelected));
    p.append(actions);
}

function springPanel(i) {
    const s = doc.springs[i], p = $('panel');
    const both = [s];
    if (mirrorSpring(i) >= 0)
        both.push(doc.springs[mirrorSpring(i)]);
    const change = (edit) => {
        for (const spring of both) {
            edit(spring);
            spring.dirty = true;
        }
        commit();
    };
    const set = (key) => (value) => change((spring) => { spring[key] = value; });
    p.append(heading(`spring ${s.a} - ${s.b}`));
    p.append(note(`${springKind(s)}: type 0 is a rope (only acts outside min..max), 1 a spring that bends or breaks, more a damper (weaker the higher).`));
    p.append(field('type', s.type, 1, set('type'), { min: 0, max: 255 }));
    p.append(field('shift', s.shift, 1, set('shift'), { min: 0, max: 31 }));
    for (const key of ['rest', 'min', 'max'])
        p.append(field(key, s[key], LENGTH_UNIT, (value) => change((spring) => {
            spring[key] = snap(value, LENGTH_UNIT);
            if (key === 'rest')
                spring.last = spring.rest;
        })));
    p.append(el('div', { className: 'actions' },
        button('Rest = current length', () => change((spring) => doc.refit(spring))),
        button('Remove', removeSelected)));
}

function palettePicker(apply) {
    const palette = view.texture?.palette;
    if (!palette)
        return note('Open the car\'s .pcx to pick from its palette.');
    const grid = el('div', { className: 'palette' });
    for (let c = 0; c < 256; c++) {
        const swatch = el('button', { title: String(c), onclick: () => apply(c * 0x101) });
        swatch.style.background = `rgb(${palette[c * 3]},${palette[c * 3 + 1]},${palette[c * 3 + 2]})`;
        grid.append(swatch);
    }
    return grid;
}

function polygonPanel(i) {
    const f = doc.polygons[i], p = $('panel'), m = mirrorPolygon(i);
    p.append(heading(`${f.op} polygon`));
    p.append(note(`Corners: ${f.corners.slice(0, -1).map((c) => `${c.kind}[${c.i}]`).join(', ')}.`));
    if (f.op === 'flat') {
        const setColour = (colour) => attempt(() => {
            doc.setFlatColour(i, colour);
            if (m >= 0)
                doc.setFlatColour(m, colour);
            newColour = colour;
            commit();
        });
        const hex = el('input', { type: 'text', value: '0x' + f.colour.toString(16).padStart(4, '0'), id: 'field-colour' });
        hex.onchange = () => {
            const v = parseInt(hex.value);
            if (Number.isNaN(v) || v < 0 || v > 0xffff) {
                say('A colour is 0x0000 to 0xffff');
                refresh();
            } else
                setColour(v);
        };
        p.append(el('label', { className: 'field' }, el('span', { textContent: 'colour' }), hex));
        p.append(note('Two palette colours in a dither, 0xf0f0 and up: glass.'));
        p.append(palettePicker(setColour));
    } else if (f.op === 'textured')
        p.append(note('The texture coordinates are edited in the Text tab.'));
    p.append(el('div', { className: 'actions' },
        button('Flip', () => attempt(() => {
            doc.flipPolygon(i);
            if (m >= 0)
                doc.flipPolygon(m);
            commit();
        }), 'Make it face the other side'),
        button('Remove', removeSelected)));
}

function carPanel() {
    const p = $('panel');
    p.append(heading(name));
    const drive = el('select', {}, ...['rear', 'front', 'all'].map((k) => el('option', { value: k, textContent: k })));
    drive.value = doc.drive;
    drive.onchange = () => attempt(() => {
        doc.setDrive(drive.value);
        commit();
    });
    p.append(el('label', { className: 'field' }, el('span', { textContent: 'drive' }), drive));
    p.append(note(`${doc.vertices.length} vertices, ${doc.springs.length} springs, ${doc.polygons.length} polygons.`));
    if (doc.vertices.length >= doc.maxVertices)
        p.append(note(`At the limit: a car can have at most ${doc.maxVertices} vertices.`, 'warn'));
    p.append(el('div', { className: 'actions' }, button('Add vertex', () => attempt(() => {
        let [x, y, z] = view.camera.target.map((c) => snap(c, RADIUS_UNIT));
        if (symmetric()) {                  // a pair, apart so that each can be moved
            x = Math.abs(x) < 1 / 64 ? 1 / 8 : x;
            view.sel.vertices = [doc.addVertex(x, y, z)];
            doc.addVertex(-x, y, z);
        } else
            view.sel.vertices = [doc.addVertex(x, y, z)];
        setMode('vertex');
        commit();
    }), 'New vertex in the middle of the view; in the symmetric mode, a pair')));
    objPanel(p);
    p.append(el('ul', { className: 'small help' },
        ...['Drag a vertex to move it; X, Y or Z (or the buttons) keep it on that axis.',
            'Shift-click picks several, in order; Shift-drag a box. Two make a spring, three or more a polygon.',
            'Left-drag orbits, right-drag pans, the wheel zooms, F frames the car.',
            'Delete removes, Ctrl+Z undoes, Esc lets go.',
            'Springs: orange rope, green spring, blue damper. Vertices: cyan front wheel, orange back wheel, pink the in-car camera.'].map((t) => el('li', { textContent: t }))));
}

function objPanel(p) {
    const select = (label, key, options, read = (v) => v) => {
        const s = el('select', {}, ...options.map(([value, textContent]) => el('option', { value, textContent })));
        s.value = String(objOptions[key]);
        s.onchange = () => { objOptions[key] = read(s.value); };
        return el('label', { className: 'field' }, el('span', { textContent: label }), s);
    };
    const replace = el('input', { type: 'checkbox', checked: objOptions.replace });
    replace.onchange = () => { objOptions.replace = replace.checked; };
    p.append(heading('Import .obj'));
    p.append(select('up', 'up', [['y', 'Y (front -Z)'], ['z', 'Z (front -Y)']]));
    p.append(select('turn', 'turn', [['0', '0'], ['1', '90'], ['2', '180'], ['3', '270']], Number));
    p.append(field('length', objOptions.length, 1 / 16, (value) => { objOptions.length = Math.max(0, value); }, { min: 0, title: 'In tiles; 0: as long as the car' }));
    p.append(el('label', { className: 'check' }, replace, 'Replace the car\'s faces'));
    const actions = el('div', { className: 'actions' }, button('Import .obj', () => $('importObj').click()));
    if (obj)
        actions.append(button('Apply again', () => attempt(() => runImport(true)), 'The same file with these settings, in place of the last import'));
    p.append(actions);
}

// The panel again, for the selection.
function refresh() {
    view.draw();
    if (!doc)
        return;
    const p = $('panel');
    const sel = view.sel;
    p.replaceChildren();
    if (sel.vertices.length === 1)
        vertexPanel(sel.vertices[0]);
    else if (sel.vertices.length > 1)
        verticesPanel();
    else if (sel.spring !== null)
        springPanel(sel.spring);
    else if (sel.polygon !== null)
        polygonPanel(sel.polygon);
    else
        carPanel();
    $('undo').disabled = place <= 0;
    $('redo').disabled = place >= history.length - 1;
}

// While dragging: only the numbers.
function refreshFields() {
    const sel = view.sel.vertices;
    if (sel.length !== 1)
        return;
    for (const key of ['x', 'y', 'z'])
        $(`field-${key}`).value = String(doc.vertices[sel[0]][key]);
}

function removeSelected() {
    const sel = view.sel;
    const done = attempt(() => {
        if (sel.vertices.length) {
            let springs = 0, polygons = 0;
            const all = new Set(sel.vertices);
            if (symmetric())
                sel.vertices.map(partner).filter((p) => p >= 0).forEach((p) => all.add(p));
            for (const i of [...all].sort((a, b) => b - a)) {
                const r = doc.removeVertex(i);
                springs += r.springs;
                polygons += r.polygons;
            }
            say(`Removed, with ${springs} springs and ${polygons} polygons on them`, true);
        } else if (sel.spring !== null)
            withMirror(sel.spring, mirrorSpring(sel.spring)).forEach((i) => doc.removeSpring(i));
        else if (sel.polygon !== null)
            withMirror(sel.polygon, mirrorPolygon(sel.polygon)).forEach((i) => doc.removePolygon(i));
    });
    if (done)
        clearSelection();
    else                                // what was removed before the refusal goes back
        setDoc(history[place]);
    commit();
}

// ------------------------------------------------------------------------
// The toolbar

function setMode(m) {
    mode = m;
    view.hover = null;
    document.querySelectorAll('[data-mode]').forEach((b) => b.classList.toggle('on', b.dataset.mode === m));
    if (m !== 'vertex')
        view.sel.vertices = [];
    if (m !== 'spring')
        view.sel.spring = null;
    if (m !== 'polygon')
        view.sel.polygon = null;
    refresh();
}

function setAxis(a) {
    axis = axis === a ? null : a;
    document.querySelectorAll('[data-axis]').forEach((b) => b.classList.toggle('on', b.dataset.axis === (axis ?? 'free')));
}

function setCamera(yaw, pitch) {
    Object.assign(view.camera, { yaw, pitch });
    view.draw();
}

document.querySelectorAll('[data-mode]').forEach((b) => { b.onclick = () => setMode(b.dataset.mode); });
document.querySelectorAll('[data-axis]').forEach((b) => { b.onclick = () => setAxis(b.dataset.axis === 'free' ? axis : b.dataset.axis); });
$('camPersp').onclick = () => setCamera(0.7, 0.45);
$('camTop').onclick = () => setCamera(0, 1.55);
$('camFront').onclick = () => setCamera(0, 0);
$('camSide').onclick = () => setCamera(Math.PI / 2, 0);
$('camBack').onclick = () => setCamera(Math.PI, 0);
$('camFrame').onclick = () => view.frame();
$('ortho').onchange = () => {
    view.camera.ortho = $('ortho').checked;
    view.draw();
};
for (const key of ['solid', 'springs', 'labels', 'backs'])
    $(`show-${key}`).onchange = () => {
        view.show[key] = $(`show-${key}`).checked;
        view.rebuild();
    };
$('undo').onclick = () => travel(-1);
$('redo').onclick = () => travel(1);

addEventListener('keydown', (e) => {
    if (/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName) && e.target.type !== 'checkbox')
        return;
    if (!$('textTab').hidden)
        return;
    const key = e.key.toLowerCase();
    if ((e.ctrlKey || e.metaKey) && key === 'z') {
        e.preventDefault();
        travel(e.shiftKey ? 1 : -1);
    } else if ((e.ctrlKey || e.metaKey) && key === 'y') {
        e.preventDefault();
        travel(1);
    } else if (e.ctrlKey || e.metaKey || e.altKey) {
        return;
    } else if (key === 'x' || key === 'y' || key === 'z')
        setAxis(key);
    else if (key === 'f')
        view.frame();
    else if (key === 'escape') {
        clearSelection();
        refresh();
    } else if ((key === 'delete' || key === 'backspace') && doc)
        removeSelected();
});

// ------------------------------------------------------------------------
// The tabs, files

function showText(on) {
    $('viewTab').hidden = on;
    $('textTab').hidden = !on;
    $('tabView').classList.toggle('on', !on);
    $('tabText').classList.toggle('on', on);
    if (!on)
        view.draw();
}

// The Text tab into the car; false (and the error shown) if it is not one.
function takeText() {
    const text = $('text').value;
    if (doc && text === doc.text())
        return true;
    try {
        const first = !doc;
        setDoc(text);
        if (first) {
            history = [];
            place = -1;
            $('empty').hidden = true;
            view.frame();
        }
        clearSelection();
        commit();
        return true;
    } catch (e) {
        say(e.message);
        if (e.line)
            showLine(e.line);
        return false;
    }
}

$('tabText').onclick = () => {
    if (doc)
        $('text').value = doc.text();
    showText(true);
};
$('tabView').onclick = () => {
    if (!$('text').value && !doc) {
        showText(false);
        return;
    }
    if (takeText())
        showText(false);
};

// the line of an error, selected in the text
function showLine(line) {
    const t = $('text'), lines = t.value.split('\n');
    const start = lines.slice(0, line - 1).reduce((n, l) => n + l.length + 1, 0);
    t.focus();
    t.setSelectionRange(start, start + (lines[line - 1] || '').length);
}

// An 8-bit PCX: { width, height, pixels, palette (768 bytes, or null) }.
function decodePcx(buffer) {
    const b = new Uint8Array(buffer), dv = new DataView(buffer);
    if (b.length < 128 || b[0] !== 10 || b[3] !== 8 || b[65] !== 1)
        throw new Error('not an 8-bit PCX');
    const width = dv.getUint16(8, true) - dv.getUint16(4, true) + 1, height = dv.getUint16(10, true) - dv.getUint16(6, true) + 1;
    const line = dv.getUint16(66, true), row = new Uint8Array(line), pixels = new Uint8Array(width * height);
    let at = 128;
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < line && at < b.length;) {
            let count = 1, value = b[at++];
            if (value >= 0xc0) {
                count = value & 0x3f;
                value = b[at++];
            }
            while (count-- && x < line)
                row[x++] = value;
        }
        pixels.set(row.subarray(0, width), y * width);
    }
    const palette = b.length >= 769 && b[b.length - 769] === 12 ? b.slice(b.length - 768) : null;
    return { width, height, pixels, palette };
}

async function open(file) {
    const data = await file.arrayBuffer();
    try {
        if (/\.pcx$/i.test(file.name)) {
            view.setTexture(decodePcx(data));
            refresh();
            say(`${file.name}: texture`, true);
            return;
        }
        name = file.name.replace(/\.[^.]*$/, '');
        const text = /\.txt$/i.test(file.name) ? new TextDecoder().decode(data) : decode(data);
        $('text').value = text;
        start(text);
        say(`${file.name} opened`, true);
    } catch (e) {
        say(`${file.name}: ${e.message}`);
    }
}

function download(bytes, filename, type) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([bytes], { type }));
    a.download = filename;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function currentText() {
    return !$('textTab').hidden || !doc ? $('text').value : doc.text();
}

// The car, then its texture, then a mesh to go on it.
async function openAll(files) {
    const rank = (f) => /\.obj$/i.test(f.name) ? 2 : /\.pcx$/i.test(f.name) ? 1 : 0;
    for (const file of files.sort((a, b) => rank(a) - rank(b)))
        await (rank(file) === 2 ? importFile(file) : open(file));
}

// Puts the last .obj on the car (the base car, if there is none), as many
// vertices as it can take and as still fit in a car's file.
function runImport(again) {
    if (!doc)
        start(BASE_CAR);
    if (again && place === objPlace)
        travel(-1);
    const body = doc.vertices.filter((v) => v.kind === 'body'), ys = body.map((v) => v.y);
    if (!body.length)
        throw new Error('the car has no body vertices');
    const options = { ...objOptions, length: objOptions.length || Math.max(...ys) - Math.min(...ys),
        centreY: (Math.max(...ys) + Math.min(...ys)) / 2, bottom: Math.min(...body.map((v) => v.z)) };
    const total = new Set(obj.faces.flat()).size;
    for (let budget = doc.maxVertices - doc.vertices.length; budget >= 4; budget = Math.floor(budget * 0.8)) {
        const mesh = fitMesh(obj, options, budget), trial = new CarDoc(doc.text());
        trial.addMesh(mesh, newColour, objOptions.replace);
        if (encode(trial.text()).length > MAX_FILE_SIZE)
            continue;
        doc = trial;
        view.setDoc(doc);
        clearSelection();
        commit();
        objPlace = place;
        view.frame();
        refresh();
        say(`${objName}: ${mesh.points.length} vertices${mesh.points.length < total ? ` (of ${total})` : ''}, ${mesh.faces.length} faces`, true);
        return;
    }
    throw new Error(`${objName}: too big for a car, even simplified`);
}

async function importFile(file) {
    try {
        obj = parseObj(await file.text());
        objName = file.name;
        runImport(false);
    } catch (e) {
        say(`${file.name}: ${e.message}`);
    }
}

$('open').onchange = () => openAll([...$('open').files]);
$('importObj').onchange = () => {
    if ($('importObj').files[0])
        importFile($('importObj').files[0]);
    $('importObj').value = '';
};
$('baseCar').onclick = () => {
    if (place > 0 && !confirm('Replace the car being edited with the base car?'))
        return;
    name = 'car1';
    $('text').value = BASE_CAR;
    start(BASE_CAR);
    showText(false);
    say('Base car: a plank on four wheels', true);
};
$('saveDat').onclick = () => {
    try {
        const bytes = encode(currentText());
        download(bytes, name + '.dat', 'application/octet-stream');
        if (bytes.length > MAX_FILE_SIZE)
            say(`${name}.dat: ${bytes.length} bytes, but the game reads only the first ${MAX_FILE_SIZE}`);
        else
            say(`${name}.dat: ${bytes.length} bytes`, true);
    } catch (e) {
        say(e.message);
        if (e.line) {
            showText(true);
            showLine(e.line);
        }
    }
};
$('saveText').onclick = () => download(currentText(), name + '.txt', 'text/plain');
addEventListener('dragover', (e) => e.preventDefault());
addEventListener('drop', (e) => {
    e.preventDefault();
    openAll([...e.dataTransfer.files]);
});

setMode('vertex');
if (!view.gl)
    say('This browser has no WebGL 2: only the wireframe will show');
