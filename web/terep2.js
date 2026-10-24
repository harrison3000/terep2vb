// The web host's page side, an ES module (see host_web/web_main.cpp): picks a map folder (read
// locally, nothing is uploaded), runs the game in terep2.wasm and draws it: the game's own
// picture on a 2D canvas, or the OpenGL renderer's with WebGL 2 (webgl.js) and its text over it.
import { webglImports } from './webgl.js';

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

// The little of WASI the module uses: printing (to the console), nothing else
// (no directories: the files are in the module's memory, see host_web/web_main.cpp).
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
        fd_fdstat_get(fd, p) {      // 1 and 2 are terminals (line by line), nothing else is open
            if (fd !== 1 && fd !== 2)
                return 8;           // EBADF
            const view = new DataView(getMemory().buffer);
            for (let i = 0; i < 24; i += 4)
                view.setUint32(p + i, 0, true);
            view.setUint8(p, 2);    // character device
            return 0;
        },
    };
}

// The sound: the AudioWorklet in opl_player.js plays the samples the module
// makes as the time advances.
let audio = null, player = null;    // AudioContext, its AudioWorkletNode

// Within the click that starts a game (browsers allow sound only then).
function startAudio() {
    if (!audio) {
        audio = new AudioContext();
        audio.audioWorklet.addModule('opl_player.js').then(() => {
            player = new AudioWorkletNode(audio, 'opl-player', { outputChannelCount: [2] });
            player.connect(audio.destination);
        }).catch((e) => console.warn('no sound:', e));
    }
    audio.resume();
}

function playSamples() {
    const n = wasm.web_audio_frames();
    if (!n || !player)
        return;
    const s = new Int16Array(wasm.memory.buffer, wasm.web_audio_samples(), n * 2);
    const f = new Float32Array(n * 2);
    for (let i = 0; i < f.length; i++)
        f[i] = s[i] / 32768;
    player.port.postMessage(f, [f.buffer]);
}

// The minimap, in the bottom right corner of the window: the map, and a
// rectangle per car turned the way it points, a dark dot at its front, its
// number next to it; the followed car on top.
const CAR_COLOURS = ['#ff3c3c', '#ffe628', '#3cdcff', '#ff50ff', '#50ff5a', '#ff961e', '#ffffff', '#7878ff'];
const mapImage = document.createElement('canvas');
mapImage.width = mapImage.height = 256;

function prepareMinimap() {
    const p = wasm.web_minimap();
    const pixels = new Uint8ClampedArray(wasm.memory.buffer, p, 256 * 256 * 4);
    mapImage.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(pixels), 256, 256), 0, 0);
}

function drawMinimap() {
    const mini = $('minimap');
    if (!$('minimapOn').checked) {
        mini.hidden = true;
        return;
    }
    mini.hidden = false;
    const size = Math.round(Math.min(innerWidth, innerHeight) * 0.3);
    if (mini.width !== size) {
        mini.width = mini.height = size;
    }
    const ctx = mini.getContext('2d'), k = size / 256;
    ctx.clearRect(0, 0, size, size);
    ctx.globalAlpha = 0.6;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(mapImage, 0, 0, size, size);
    ctx.globalAlpha = 1;
    const n = wasm.web_car_count(), followed = wasm.web_car_followed();
    const half = Math.max(size / 80, 1.5);
    ctx.font = `${Math.max(10, Math.round(size / 18))}px monospace`;
    for (let j = 1; j <= n; j++) {
        const i = (followed + j) % n;           // the followed one last
        const x = wasm.web_car_x(i) * k, y = wasm.web_car_y(i) * k;
        const a = wasm.web_car_heading(i) * Math.PI / 16;
        ctx.save();
        ctx.translate(x, y);
        ctx.rotate(a);                          // 0: up the map, clockwise
        ctx.fillStyle = '#000';
        ctx.fillRect(-half - 1, -2 * half - 1, 2 * half + 2, 4 * half + 2);
        ctx.fillStyle = CAR_COLOURS[i % CAR_COLOURS.length];
        ctx.fillRect(-half, -2 * half, 2 * half, 4 * half);
        ctx.fillStyle = '#000';
        ctx.beginPath();
        ctx.arc(0, -1.2 * half, half * 0.5, 0, 2 * Math.PI);
        ctx.fill();
        ctx.restore();
        ctx.fillStyle = CAR_COLOURS[i % CAR_COLOURS.length];
        ctx.fillText(String(i + 1), x + 2.5 * half, y);
    }
}

let wasm = null;                    // the module's exports
let maps = new Map();               // folder path -> its File objects
let playing = false, last = 0;
let webgl = false;                  // this game is drawn by the OpenGL renderer

async function load() {
    let memory = null;
    const { instance } = await WebAssembly.instantiateStreaming(fetch('terep2.wasm'), {
        wasi_snapshot_preview1: wasiImports(() => memory),
        gl: webglImports($('gl'), () => memory),
    });
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
    if ($('sound').checked)
        startAudio();
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
    wasm.web_audio_start($('sound').checked && audio ? audio.sampleRate : 0);
    webgl = $('renderer').value === 'webgl';
    $('screen').hidden = webgl;
    $('gl').hidden = $('overlay').hidden = !webgl;
    $('chooser').hidden = true;
    $('game').hidden = false;           // before sizing the canvas
    if (webgl) {
        sizeCanvases();
        wasm.web_draw_distance(+$('distance').value);
    }
    if (!wasm.web_start(+$('scale').value, $('fixice').checked, $('banner').checked, webgl)) {
        $('game').hidden = true;
        $('chooser').hidden = false;
        alert(webgl ? 'This map cannot be played, or WebGL 2 is not available.' : 'This map cannot be played.');
        return;
    }
    prepareMinimap();
    camera = 3;                         // a new game starts on the chase camera
    playing = true;
    last = performance.now();
    requestAnimationFrame(frame);
}

function stop() {
    playing = false;
    wasm.web_stop();
    if (audio)
        audio.suspend();
    $('game').hidden = true;
    $('chooser').hidden = false;
}

const picture = document.createElement('canvas');

// The OpenGL renderer's canvas and the text's over it: the whole window, in
// device pixels.  When a game starts and when the window's size (or its
// pixel ratio, another screen or a zoom) changes.
function sizeCanvases() {
    const w = Math.max(1, Math.floor(innerWidth * devicePixelRatio));
    const h = Math.max(1, Math.floor(innerHeight * devicePixelRatio));
    for (const c of [$('gl'), $('overlay')])
        if (c.width !== w || c.height !== h) {
            c.width = w;
            c.height = h;
        }
    wasm.web_canvas_size(w, h);
}

addEventListener('resize', () => { if (playing && webgl) sizeCanvases(); });

// The OpenGL renderer's text (game_frame's), over its picture.
function drawTexts() {
    const overlay = $('overlay'), ctx = overlay.getContext('2d');
    ctx.clearRect(0, 0, overlay.width, overlay.height);
    ctx.textBaseline = 'top';
    for (let i = 0; i < wasm.web_text_count(); i++) {
        const size = wasm.web_text_size(i), x = wasm.web_text_x(i), y = wasm.web_text_y(i);
        ctx.font = `${size}px monospace`;
        ctx.fillStyle = '#' + wasm.web_text_rgb(i).toString(16).padStart(6, '0');
        const p = wasm.web_text_str(i), bytes = new Uint8Array(wasm.memory.buffer, p);
        const text = new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
        text.split('\n').forEach((line, k) => ctx.fillText(line, x, y + k * size));
    }
}

function frame(now) {
    if (!playing)
        return;
    const drawn = wasm.web_advance(now - last);
    last = now;
    playSamples();
    if (drawn && webgl) {
        drawTexts();
        drawMinimap();
    } else if (drawn) {
        const w = wasm.web_width(), h = wasm.web_height();
        if (picture.width !== w || picture.height !== h) {
            picture.width = w;
            picture.height = h;
        }
        const pixels = new Uint8ClampedArray(wasm.memory.buffer, wasm.web_picture(), w * h * 4);
        picture.getContext('2d').putImageData(new ImageData(pixels, w, h), 0, 0);
        // as large as the window allows, keeping the shape, pixels left sharp
        const screen = $('screen'), k = Math.min(innerWidth / w, innerHeight / h);
        screen.width = Math.floor(w * k);
        screen.height = Math.floor(h * k);
        const ctx = screen.getContext('2d');
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(picture, 0, 0, screen.width, screen.height);
        drawMinimap();
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
        localStorage.setItem('terep2web', JSON.stringify({
            scale: $('scale').value, fixice: $('fixice').checked, banner: $('banner').checked,
            sound: $('sound').checked, minimap: $('minimapOn').checked, renderer: $('renderer').value,
            distance: $('distance').value }));
    } catch (e) { /* private mode: not kept */ }
}

function loadPrefs() {
    try {
        const p = JSON.parse(localStorage.getItem('terep2web') || '{}');
        if (p.scale) $('scale').value = p.scale;
        if (p.fixice !== undefined) $('fixice').checked = p.fixice;
        if (p.banner !== undefined) $('banner').checked = p.banner;
        if (p.sound !== undefined) $('sound').checked = p.sound;
        if (p.minimap !== undefined) $('minimapOn').checked = p.minimap;
        if (p.renderer) $('renderer').value = p.renderer;
        if (p.distance) $('distance').value = p.distance;
    } catch (e) { /* nothing saved */ }
}

// On a touch screen: buttons for car 1 (the arrows' and keypad 5's scancodes,
// and 1 / 2, which jump to turn a car over), one that goes through the
// cameras F1..F5, and one that goes back to the maps (there is no Esc).
const touchScreen = matchMedia('(pointer: coarse)').matches || navigator.maxTouchPoints > 0;
let camera = 3;                     // the F key of the camera last asked for, 0 = F1 (F4: chase, the default)

function holdButton(b, press, release) {
    b.addEventListener('pointerdown', (e) => {
        e.preventDefault();
        b.setPointerCapture(e.pointerId);
        b.classList.add('down');
        if (playing)
            press();
    });
    const up = () => {
        if (!b.classList.contains('down'))
            return;
        b.classList.remove('down');
        if (playing)
            release();
    };
    b.addEventListener('pointerup', up);
    b.addEventListener('pointercancel', up);
    b.addEventListener('contextmenu', (e) => e.preventDefault());   // long press
}

if (touchScreen) {
    document.body.classList.add('touch');
    $('touch').hidden = false;
    for (const b of document.querySelectorAll('#touch [data-sc]')) {
        const sc = +b.dataset.sc;
        holdButton(b, () => wasm.web_key(sc), () => wasm.web_key(sc | 0x80));
    }
    holdButton($('camera'), () => {
        camera = (camera + 1) % 5;
        wasm.web_key(0x3b + camera);
    }, () => wasm.web_key((0x3b + camera) | 0x80));
    $('back').addEventListener('click', () => { if (playing) stop(); });
}

addEventListener('keydown', (e) => key(e, false));
addEventListener('keyup', (e) => key(e, true));
$('folder').onchange = () => {
    maps = findMaps($('folder').files);
    showMaps();
};
$('distance').oninput = () => { $('distanceText').textContent = $('distance').value + ' tiles'; };
loadPrefs();
$('distance').oninput();
load().catch((e) => { $('status').textContent = 'Could not load terep2.wasm: ' + e; });
