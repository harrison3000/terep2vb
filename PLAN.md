# Terep2 → readable C: analysis and plan (historical; the current state is in README.md)

## What is in the project

| Path | What it is |
|---|---|
| `reasm/maincode.asm` | Disassembly (NASM, 16-bit real mode + 32-bit regs), ~9000 lines, 124 functions with Ghidra's names (`FUN_1000_xxxx`, some already marked PHYSICS/INIT/SHARED). |
| `memdumps/data.bin` | Dump of the data segment (62 KB): strings, file names, palette, font, tables (sine etc.) and all the global variables. The code reaches everything by a fixed offset (`[0x5bba]`, `[0xdb10]`...). |
| `common/*.asm` | Macros and new variables (temporaries made to stop using BP as a register). |
| `win16/` | Win16 host (Watcom): window, 120 Hz (physics) and 60 Hz (render) timers, keyboard → scancode, StretchDIBits of the framebuffer. |
| `previous_effort/` | A mechanical 1:1 translation of the asm to C++ (a `cpu_ctx` struct with the registers, `MEM_WORD(0x...)`, gotos). It compiles, but the host was missing. |
| `track_file_samples/Asphalt` | Sample track: `map.pcx` (height), `col.pcx` (colour + palette), `textures.pcx`, `maptex.pcx`, `fog.dat`, `smoke.dat`, `car1..5.dat` (each car's model/physics), `car*.pcx` (textures). |

## The game's architecture (what could be understood so far)

- **Init** (`f_init`): reads `sim.cfg` (optional), allocates 4 segments of 64 KB (height map, terrain colour, textures, map texture), loads the PCXs, `fog.dat` and `smoke.dat`, then loads up to 5 cars. Each `carN.dat` is copied *whole* into the data segment from 0x5bd0 on; the table at 0x5bbc keeps each car's offset. The car's file **is** the car's structure: a header of 0x84 bytes (physics state, camera, texture pointer) + a list of vertices of 0x1c bytes (16.16 position + velocity, it is a mass-spring soft body, hence the "DEFORMERS") + springs + polygons.
- **Physics** (`FUN_timer_5680`, 120 Hz): for each car it calls `0d2a`, `1004` and `48d0`, then `500b`, `0bb5`, `0a3b` (a guess not confirmed yet: keyboard input, springs/integration, contact with the terrain, car-car collision).
- **Render** (`FUN_main_render`): 5 cameras (F1–F5), split screen (F9), terrain from the heightmap, cars as textured polygons with backface culling, text with a bitmap font (details of the terrain and of the smoke still to be confirmed).
- **Sound**: writes to AdLib/OPL2 registers (port 0x388), a stub for now.

## How to make sure nothing broke

I set up a headless Linux host for `previous_effort` (it implements the DOS services used: open/read/seek/close/allocate). It runs the sample track, injects keys from a script and records the hash of the framebuffer and of the memory at each frame (and PPM if wanted). That becomes the **oracle**: each rewritten function has to produce exactly the same hashes.

## Conversion plan

1. A base in plain C (no `cpu_ctx`, no assembly gotos), keeping the original memory layout for a bit-for-bit comparison.
2. Rewrite function by function, bottom up (maths → physics → rasterisers → render → init), swapping registers for parameters and returns and offsets for names.
3. Recover the structures: `Car` (header + vertices + springs + faces), `Camera`, `Viewport`, PCX header, global tables, font.
4. At the end, swap the data segment's "blob" for typed globals and extracted constant tables, keeping the framebuffer hash test.
5. Hosts: a headless test (Linux) + an optional SDL2 host. The target is portable C99 (32/64-bit); I will **not** keep compatibility with 16-bit Win16, unless you ask for it.
