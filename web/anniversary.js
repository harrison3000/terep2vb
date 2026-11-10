// The Terep2 30th Anniversary Edition's zip, read here (no library): the
// tracks to pick from (each a zip of its PCX images, in the zip), the car
// models (.dat) and their textures (.pcx), and the files every track shares
// (fog and translucency tables).  A file is { path, read }: read() gives its
// contents, an ArrayBuffer.

// Where in the zip, folder -> its name in the lists.
export const CAR_DIRS = {
    'Terep - 30th Anniversary Edition/TEREP/AUTOK/': 'Terep 2',
    'Terep - 30th Anniversary Edition/Extras/Car Models/': 'Extras',
    'Terep - 30th Anniversary Edition/Extras/Car Models/More/': 'Extras (more)',
};
export const CAR_TEXTURE_DIRS = {
    'Terep - 30th Anniversary Edition/TEREP/CARTEX/': 'Terep 2',
    'Terep - 30th Anniversary Edition/Extras/Car Textures/': 'Extras',
};
export const TRACK_DIRS = {
    'Terep - 30th Anniversary Edition/TEREP/PALYAK/': 'Terep 2',
    'Terep - 30th Anniversary Edition/Extras/Tracks/': 'Extras',
};
// The files of every track, in SHARED_DIR (a track's own one goes first).
export const SHARED_DIR = 'Terep - 30th Anniversary Edition/TEREP/';
export const SHARED_FILES = ['fog.dat', 'smoke.dat', 'textures.pcx', 'sim.cfg'];

export const baseName = (path) => path.slice(path.lastIndexOf('/') + 1);

// The files of a zip: its central directory, at the end, lists each file's
// name and where its data is, stored or deflated (which the browser's
// DecompressionStream undoes).
export function readZip(buffer) {
    const buf = new Uint8Array(buffer), dv = new DataView(buffer);
    let end = buf.length - 22;          // the end of central directory record, after a comment of up to 64K
    while (end >= 0 && dv.getUint32(end, true) !== 0x06054b50)
        end--;
    if (end < 0)
        throw new Error('not a zip');
    const decoder = new TextDecoder(), files = [];
    for (let n = dv.getUint16(end + 10, true), p = dv.getUint32(end + 16, true); n--;) {
        const method = dv.getUint16(p + 10, true), size = dv.getUint32(p + 20, true);
        const nameLen = dv.getUint16(p + 28, true), local = dv.getUint32(p + 42, true);
        const name = decoder.decode(buf.subarray(p + 46, p + 46 + nameLen)).replaceAll('\\', '/');
        p += 46 + nameLen + dv.getUint16(p + 30, true) + dv.getUint16(p + 32, true);
        const start = local + 30 + dv.getUint16(local + 26, true) + dv.getUint16(local + 28, true);
        const data = buf.subarray(start, start + size);
        if (name.endsWith('/'))
            continue;
        files.push({ path: name, read: method === 0 ? async () => data.slice().buffer
            : method === 8 ? () => new Response(new Blob([data]).stream()
                .pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer()
            : () => Promise.reject(new Error(name + ': unknown zip compression ' + method)) });
    }
    return files;
}

// The files right in one of the folders of dirs (not in their subfolders)
// whose names match ext, as { group, name, file }, in the order of dirs.
function choices(files, dirs, ext) {
    const found = [];
    for (const [dir, group] of Object.entries(dirs)) {
        const d = dir.toLowerCase(), inDir = [];
        for (const file of files) {
            const path = file.path.toLowerCase();
            if (path.startsWith(d) && !path.slice(d.length).includes('/') && ext.test(path))
                inDir.push({ group, name: baseName(file.path), file });
        }
        found.push(...inDir.sort((a, b) => a.name.localeCompare(b.name)));
    }
    return found;
}

// A track's own name, the second line of its inst.inf (if it has one): the
// zip's or folder's name is DOS's, 8 letters at most.  An old DOS text, not
// always UTF-8: then each byte a letter (Latin-1).
export async function trackTitle(files) {
    const inf = files.find((f) => baseName(f.path).toLowerCase() === 'inst.inf');
    if (!inf)
        return null;
    const bytes = await inf.read();
    let text;
    try {
        text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    } catch {
        text = new TextDecoder('latin1').decode(bytes);
    }
    return text.split(/\r?\n/)[1]?.trim() || null;
}

// What the zip (a File) has: { tracks, cars, textures, shared }.  A track
// has its files (its zip read) and its title, by which they are sorted.
export async function openAnniversary(zip) {
    const files = readZip(await zip.arrayBuffer());
    const shared = files.filter((f) => f.path.toLowerCase() === SHARED_DIR.toLowerCase() + baseName(f.path).toLowerCase()
        && SHARED_FILES.includes(baseName(f.path).toLowerCase()));
    const tracks = choices(files, TRACK_DIRS, /\.zip$/);
    await Promise.all(tracks.map(async (t) => {
        t.files = readZip(await t.file.read());
        t.title = await trackTitle(t.files) || t.name.replace(/\.[^.]*$/, '');
    }));
    const groups = Object.values(TRACK_DIRS);
    tracks.sort((a, b) => groups.indexOf(a.group) - groups.indexOf(b.group) || a.title.localeCompare(b.title));
    return {
        tracks,
        cars: choices(files, CAR_DIRS, /\.dat$/),
        textures: choices(files, CAR_TEXTURE_DIRS, /\.pcx$/),
        shared,
    };
}

// The files of a track (one of tracks), from its zip.
export async function trackFiles(track) {
    return track.files;
}
