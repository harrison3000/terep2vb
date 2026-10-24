// The web host's page side (see web_main.cpp): picks a map folder (read
// locally, nothing is uploaded), runs the game in terep2.wasm and draws it.
'use strict';

const $ = (id) => document.getElementById(id);

// KeyboardEvent.code -> PC scancode (set 1), as the SDL host's table.
const SCANCODES = {
    Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06, Digit6: 0x07, Digit7: 0x08,
    Digit8: 0x09, Digit9: 0x0a, Digit0: 0x0b, Minus: 0x0c, Equal: 0x0d, Backspace: 0x0e, Tab: 0x0f,
    KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14, KeyY: 0x15, KeyU: 0x16, KeyI: 0x17,
    KeyO: 0x18, KeyP: 0x19, BracketLeft: 0x1a, BracketRight: 0x1b, Enter: 0x1c, ControlLeft: 0x1d,
    ControlRight: 0x1d, KeyA: 0x1e, KeyS: 0x1f, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23,
    KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29, ShiftLeft: 0x2a,
    Backslash: 0x2b, KeyZ: 0x2c, KeyX: 0x2d, KeyC: 0x2e, KeyV: 0x2f, KeyB: 0x30, KeyN: 0x31, KeyM: 0x32,
    Comma: 0x33, Period: 0x34, Slash: 0x35, NumpadDivide: 0x35, ShiftRight: 0x36, NumpadMultiply: 0x37,
    AltLeft: 0x38, Space: 0x39, F1: 0x3b, F2: 0x3c, F3: 0x3d, F4: 0x3e, F5: 0x3f, F6: 0x40, F7: 0x41,
    F8: 0x42, F9: 0x43, F10: 0x44, Numpad7: 0x47, Home: 0x47, Numpad8: 0x48, ArrowUp: 0x48,
    Numpad9: 0x49, PageUp: 0x49, NumpadSubtract: 0x4a, Numpad4: 0x4b, ArrowLeft: 0x4b, Numpad5: 0x4c,
    Numpad6: 0x4d, ArrowRight: 0x4d, NumpadAdd: 0x4e, Numpad1: 0x4f, End: 0x4f, Numpad2: 0x50,
    ArrowDown: 0x50, Numpad3: 0x51, PageDown: 0x51, Numpad0: 0x52, Insert: 0x52, NumpadDecimal: 0x53,
    Delete: 0x53,
};

// The little of WASI the module uses: printing (to the console), nothing else.
function wasiImports(getMemory) {
    const lines = { 1: '', 2: '' };
    return {
        fd_write(fd, iovs, n, written) {
            const mem = getMemory(), view = new DataView(mem.buffer);
            let total = 0;
            for (let i = 0; i < n; i++) {
                const p = view.getUint32(iovs + 8 * i, true), len = view.getUint32(iovs + 8 * i + 4, true);
                lines[fd] = (lines[fd] || '') + new TextDecoder().decode(new Uint8Array(mem.buffer, p, len));
                total += len;
            }
            const parts = lines[fd].split('\n');
            lines[fd] = parts.pop();
            for (const l of parts)
                (fd === 2 ? console.warn : console.log)(l);
            view.setUint32(written, total, true);
            return 0;
        },
        fd_close: () => 0,
        fd_seek: () => 70,          // ESPIPE: no files here
    };
}

let wasm = null;                    // the module's exports
let maps = new Map();               // folder path -> its File objects
let playing = false, last = 0;

async function load() {
    let memory = null;
    const { instance } = await WebAssembly.instantiateStreaming(fetch('terep2.wasm'),
        { wasi_snapshot_preview1: wasiImports(() => memory) });
    memory = instance.exports.memory;
    instance.exports._initialize();
    wasm = instance.exports;
}

// The folders of the picked one that hold a map (map.pcx, col.pcx, car1.dat).
function findMaps(fileList) {
    const dirs = new Map();
    for (const f of fileList) {
        const path = f.webkitRelativePath || f.name;
        const dir = path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : '.';
        if (!dirs.has(dir))
            dirs.set(dir, []);
        dirs.get(dir).push(f);
    }
    const found = new Map();
    for (const [dir, files] of dirs) {
        const names = new Set(files.map((f) => f.name.toLowerCase()));
        if (names.has('map.pcx') && names.has('col.pcx') && names.has('car1.dat'))
            found.set(dir, files);
    }
    return found;
}

function showMaps() {
    const list = $('maps');
    list.innerHTML = '';
    for (const dir of [...maps.keys()].sort()) {
        const b = document.createElement('button');
        b.textContent = dir;
        b.onclick = () => play(dir);
        list.appendChild(b);
    }
    $('none').hidden = maps.size > 0 || !$('folder').files.length;
}

// Copies the map's files into the module and starts the game.
async function play(dir) {
    wasm.web_clear_files();
    const enc = new TextEncoder();
    for (const f of maps.get(dir)) {
        const data = new Uint8Array(await f.arrayBuffer());
        const name = enc.encode(f.name + '\0');
        const pname = wasm.web_alloc(name.length), pdata = wasm.web_alloc(data.length);
        new Uint8Array(wasm.memory.buffer, pname, name.length).set(name);
        new Uint8Array(wasm.memory.buffer, pdata, data.length).set(data);
        wasm.web_add_file(pname, pdata, data.length);       // the name is copied there
        wasm.web_free(pname);
    }
    savePrefs();
    if (!wasm.web_start(+$('scale').value, $('fixice').checked, $('banner').checked)) {
        alert('This map cannot be played.');
        return;
    }
    $('chooser').hidden = true;
    $('game').hidden = false;
    playing = true;
    last = performance.now();
    requestAnimationFrame(frame);
}

function stop() {
    playing = false;
    wasm.web_stop();
    $('game').hidden = true;
    $('chooser').hidden = false;
}

const picture = document.createElement('canvas');

function frame(now) {
    if (!playing)
        return;
    const p = wasm.web_advance(now - last);
    last = now;
    if (p) {
        const w = wasm.web_width(), h = wasm.web_height();
        if (picture.width !== w || picture.height !== h) {
            picture.width = w;
            picture.height = h;
        }
        const pixels = new Uint8ClampedArray(wasm.memory.buffer, p, w * h * 4);
        picture.getContext('2d').putImageData(new ImageData(pixels, w, h), 0, 0);
        // as large as the window allows, keeping the shape, pixels left sharp
        const screen = $('screen'), k = Math.min(innerWidth / w, innerHeight / h);
        screen.width = Math.floor(w * k);
        screen.height = Math.floor(h * k);
        const ctx = screen.getContext('2d');
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(picture, 0, 0, screen.width, screen.height);
    }
    requestAnimationFrame(frame);
}

function key(e, up) {
    if (!playing)
        return;
    if (e.code === 'Escape') {
        if (!up)
            stop();
        e.preventDefault();
        return;
    }
    const sc = SCANCODES[e.code];
    if (sc === undefined)
        return;
    e.preventDefault();             // Tab, F1..F10, the arrows: not the browser's
    wasm.web_key(up ? sc | 0x80 : sc);
}

function savePrefs() {
    try {
        localStorage.setItem('terep2vibed', JSON.stringify({
            scale: $('scale').value, fixice: $('fixice').checked, banner: $('banner').checked }));
    } catch (e) { /* private mode: not kept */ }
}

function loadPrefs() {
    try {
        const p = JSON.parse(localStorage.getItem('terep2vibed') || '{}');
        if (p.scale) $('scale').value = p.scale;
        if (p.fixice !== undefined) $('fixice').checked = p.fixice;
        if (p.banner !== undefined) $('banner').checked = p.banner;
    } catch (e) { /* nothing saved */ }
}

addEventListener('keydown', (e) => key(e, false));
addEventListener('keyup', (e) => key(e, true));
$('folder').onchange = () => {
    maps = findMaps($('folder').files);
    showMaps();
};
loadPrefs();
load().catch((e) => { $('status').textContent = 'Could not load terep2.wasm: ' + e; });
