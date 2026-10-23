// Runs a WASI program (wasm32-wasip1) with node, with the whole file system
// visible:
//   node tools/wasi_run.mjs program.wasm [args...]
// The program's current directory is "/" (WASI has none of its own), so the
// arguments that name existing files or directories are made absolute.
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { WASI } from 'node:wasi';
import { argv, env } from 'node:process';

const args = argv.slice(2).map((a, i) => (i > 0 && existsSync(a) ? resolve(a) : a));
const wasi = new WASI({ version: 'preview1', args, env, preopens: { '/': '/' } });
const module = await WebAssembly.compile(await readFile(argv[2]));
const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
process.exitCode = wasi.start(instance);
