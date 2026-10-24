# terep2 in C++

> **This is vibe-coded.** The code and these docs were written by an AI (Claude), steered and
> reviewed by the repository's author, not written line by line by hand. It is checked
> against the original program frame by frame (see [Verification](#verification)), but read it
> with that in mind.

A readable C++ rewrite ("C with classes" style) of **Terep2** (Nagymáthé Dénes, 1996–97,
"-DEFORMERS- Atomic Vehicle Simulation System"), made from the disassembly in
`terep2isolado/reasm/maincode.asm` and the Win16 version. Its behaviour is identical to the
original frame by frame: the framebuffer and the game state match the original asm byte for byte
(see [Verification](#verification)). The exceptions are a bug fixed on purpose (the jump bug
that corrupts the title) and a crash of the original that the port does not reproduce (see
[Things found along the way](#things-found-along-the-way)).

## Building and running

```sh
git submodule update --init   # Dear ImGui and Nuked-OPL3 (host/imgui, host/Nuked-OPL3), for terep2vibed
make            # terep2-headless: test host, no dependencies
make sdl        # terep2vibed: playable SDL2 + OpenGL 3.3 version (needs libsdl2-dev, OpenGL and the submodules)
make test       # replays the Win16 recordings in tests/ and compares the frames
make -j8 all sdl  # in parallel: each source is its own object in build/, rebuilt when its headers change
make coverage  # which lines of src/ the tests run (clang/LLVM): summary, and build/cov/html
make coverage REPLAY="rec/dir data/Map"  # the same for one recording only
make wasm-test  # the test host built for WebAssembly (wasm32-wasip1: clang + wasi-libc/libc++, no Emscripten), replays run with node
./terep2vibed              # opens the map chooser where the last map was chosen
./terep2vibed data/Asphalt # opens it with that map selected
```

C++17 with GNU extensions (`__attribute__((packed))` for the PCX header, `-fwrapv` and `#embed`), so it builds with
g++ 15+ or clang++ 19+. Tested on Linux x86-64 with both. The code does not depend on pointer
size, but it has not been tested on 32 bits. Physics runs at 120 Hz and the display at 60 Hz, as
in the Win16 port. The game's window opens at the size of the picture at the chosen scale,
enlarged to at least 960x600 and reduced by a whole factor if it does not fit on the screen.
Esc pauses the game and opens a small menu: the render scale (the
window follows it) and the minimap. Esc again resumes; the menu's button goes back to the
chooser, where another map (or other options) can be picked. Each game starts from scratch: `game_init` resets
the global state (`state_reset`) and everything the game allocated (`game_free`).

Window: the SDL host owns one OpenGL 3.3 core context (`host/gui.cpp`). The map chooser, the
game's picture (the software renderer's frame, as a texture, scaled to the window keeping its
shape) and the minimap all go through Dear ImGui on it. The SDL host draws only the game's own
picture; the OpenGL renderer is the web page's (below).

Sound: the game writes the AdLib (OPL2) registers as the original did to ports 388h/389h
(`src/sound.cpp`: an engine voice per view and the crash effect); the SDL host plays them through
[Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) (LGPL 2.1, a git submodule), an emulator of
the OPL3 chip that starts in OPL2 mode, from an SDL audio callback at 48 kHz
(`host/opl_sound.cpp`). "Disable sound" in the map chooser leaves the
audio device and the emulator alone. The test host does not play anything.

Minimap ("Minimap" in the map chooser or the Esc menu, off by default): the SDL host draws
the map semi-transparent in the bottom right corner, each cell in its colour and darker where it
is low, with a square in a colour of its own and the number of each car (in the game's font) where
its reference point is; x is to the right and y down, so a camera turned to yaw 0 looks up the
minimap. It is drawn over the game's picture through ImGui (`host/minimap.cpp`), so the
game's picture and the tests do not change.

`terep2vibed` opens a map chooser (Dear ImGui): the directories on the left, starting where
the last map was chosen, or at the map directory given on the command line (its only argument),
with that map selected (`[map]` marks those with a `map.pcx` and a
`col.pcx`); on the right a preview of the selected map, its terrain turning, drawn with the
Comanche "voxel space" technique from `map.pcx` (heights) and `col.pcx` (colour of each cell
through its palette); below it the render scale and the options (the first time, the scale
is 3 and `fix-ice-bug` is ticked). A click selects a directory, a double click enters it or
plays the map, like the Play button (which needs a `car1.dat` too). The chooser
lives in `host/` (`launcher.cpp`, `voxel_preview.cpp`, `pcx.cpp`, with its own PCX reader) and
does not touch the game code.

The chooser's choices (scale, minimap, sound, extra options and the last map) are
kept in `prefs.txt`, one `key: value` per line, in SDL's preference directory
(`~/.local/share/terep2vibed` on Linux, `%APPDATA%\terep2vibed` on Windows). They are read when the
chooser opens and written when a game starts, only if something changed.

Extra options are fixes of bugs of the original, or other changes to what it does; without
them the game behaves like the original (the test host turns them on with `-E name`,
repeatable, `all` for every one). The options are listed in `extra_option_list` (`game.cpp`)
and kept in `extra_options`, which the map chooser shows as checkboxes:

| Name | What it does |
|---|---|
| `fix-ice-bug` | broken ice no longer wraps around to the highest terrain (see below) |
| `hide-banner` | hides the "GAMBIARRA FOREVER!" line that the Win16 port draws at the bottom |

### In a web page

`make web` builds the game as WebAssembly (`web/terep2.wasm`, about 300 KB: clang for
wasm32-wasip1 with the WASI libc and libc++, no Emscripten) with its own small host,
`web/web_main.cpp`. The page (`web/index.html`, `web/terep2.js`) asks for a folder, finds the maps
in it and reads their files right there in the browser (nothing is uploaded), then runs the game
on a canvas with the keyboard, physics at 120 Hz and the picture at 60 Hz as in the SDL host. The
page needs to be served, not opened as a file:

    make web && python3 -m http.server -d web

and then http://localhost:8000. The sound is the same AdLib emulation (Nuked-OPL3, in the module
too, played through an AudioWorklet), and there is the same minimap, in the bottom right corner
of the window. Both renderers: the software one, and the OpenGL one, drawn with WebGL 2:
`web/gl_renderer.cpp` (the game's side) and `web/rz`, the renderer itself (from Harrison's
[renderizeitor](https://github.com/harrison3000/renderizeitor): GLSL ES 3.00 shaders, PCX files
from memory, the page's canvas), whose WebGL calls are imports of the module done one for one by
`web/webgl.js`. The page draws the text over it; the draw distance (where the fog starts, 1 to
110 tiles) is a slider by the map list. No map preview; Esc goes back to the list of maps. `make wasm-test` checks the game itself on 32-bit WebAssembly:
the replays give the same frames as the Win16 port there too.

### Higher resolution

The render scale N (the chooser, the Esc menu; `-s N` in the test host) renders at 320·N x 200·N. Scale 1 is the original, byte for byte. At
larger scales only the drawing changes; the game state is identical at any scale:

- the screen, the clipping and the scanline tables grow with it (`render_scale`, `screen_w()`,
  `screen_h()`);
- projection uses the full 16.16 position, relative to the eye, instead of the original's
  whole units (`view_transform_fine` in `scene.cpp`). With whole units a car close to the camera
  jumps several pixels at a time, which at 4x becomes a very visible jitter. Integer camera
  space is still used for the near plane and the facing tests;
- text, sprites, wheels and the pixels of the car model scale by the same factor.

The rest (affine texturing, Gouraud, dithering) is the original rasteriser, just finer.
![`docs/alta_resolucao.gif`](docs/alta_resolucao.gif)
Compares 1x upscaled and 4x.

### Controls

| Key | Action |
|---|---|
| arrows + keypad 5 | car 1 (accelerate, brake, left, right, handbrake) |
| W Z A D S | car 2 |
| I M J L K | car 3 |
| T V F H G | car 4 |
| keypad 7 9 1 3 . | car 5 |
| 1 / 2 | "jump" of the car in view 1 / view 2 |
| F1–F5 | camera: on the car, on the car smoothed, fixed, chase, all cars |
| F6 | cycles the camera of view 2 |
| F9 | split screen |
| F10 | near/far fog |
| Tab / Q | switches the car followed by view 1 / view 2 |
| [ ] | increases / decreases gravity |
| + − / * (keypad) | height and distance of the chase camera |

## Layout

```
src/            the game (no OS or library dependencies)
  terep.hpp     value types (Vec3, Coord, Pos), structures (Car, CarVertex, CarSpring,
                Particle, ChaseCam, ProjVertex, PcxHeader...) and prototypes
  fixed.hpp     fixed point: Angle (0x10000 = 360°), Q15 (sine/cosine, matrices), multiplications
  state.cpp     the global state (world, camera, render, map_tables, contact) and its initial values
  font.pbm      the font (plain PBM, 5x5 glyphs one below the other), embedded with #embed
  math.cpp      table-driven sine/cosine, atan2, vector length, rotation matrix
  terrain.cpp   terrain height (2 triangles per heightmap cell)
  physics.cpp   soft body (Car methods): controls, wheels, springs, integration, collision, particles
  camera.cpp    the 5 cameras
  raster.cpp    polygon clipping, near plane, flat/Gouraud/textured filling
  scene.cpp     visible area, terrain with the painter's algorithm, sprites and particles
  car_draw.cpp  car vertex transform and the model interpreter (bytecode)
  text.cpp      bitmap font
  sound.cpp     AdLib registers (the host decides what to do with them)
  car_file.cpp  the carN.dat format: car_read
  load.cpp      PCX, map, fog/translucency tables, cars, game_init
  game.cpp      one frame (views, text, keys) and the keyboard
host/
  host.hpp      what the hosts share
  common.cpp    starting and freeing a game, time step, case-insensitive file opening
  sdl_main.cpp  SDL2 host
  gui.cpp       its window: one OpenGL 3.3 core context, with ImGui (OpenGL3 backend) on it
  renderer_sw.cpp the game's picture, as a texture scaled to the window
  launcher.cpp  map chooser (Dear ImGui) shown when no map is given
  prefs.cpp     the chooser's preferences file (prefs.txt, "key: value" per line)
  voxel_preview.cpp  the chooser's preview: the map's terrain in voxel space
  pcx.cpp       PCX reader for the preview (separate from the game's)
  opl_sound.cpp the game's AdLib writes played through Nuked-OPL3 and SDL audio
  minimap.cpp   the optional minimap drawn over the game's picture
  imgui/        Dear ImGui (git submodule)
  Nuked-OPL3/   OPL3 emulator (git submodule, LGPL 2.1)
web/            the game in a web page: web_main.cpp (its host, WebAssembly), index.html, terep2.js,
                gl_renderer.cpp, rz/ and webgl.js (the OpenGL renderer with WebGL 2)
host_test/      the test host (terep2-headless)
  main.cpp      replays a recording of the Win16 port and compares the frames
data/           the maps (not in the repository)
tests/          run_tests.sh and the Win16 recordings (one directory each: record.txt, frames)
tools/          synthetic and face car makers (test maps)
```

### C++ style

C++ only where it makes the code clearer; the rest stays in the classic style (free functions,
`printf`, `memcpy`; from the standard library only `std::swap` and `std::vector` (in the cars);
no exceptions, no inheritance):

- **Value types for fixed point.** `Vec3` (signed 16.16: velocities, forces, normals) with
  `+ - += -= >>`; `Coord` (one position coordinate, unsigned 8.24 in cells, with `cell()`,
  `units()`, `Coord - Coord` = signed distance) and `Pos` (three `Coord`s, with `Pos + Vec3` and
  `Pos - Pos` = `Vec3`). Adding one position to another does not compile, which alone would
  catch the kind of int32/uint32 mix-up that used to happen. `Angle` (0x10000 = 360°, wraps
  around by itself, compares and shifts as signed like the original; `DEG45`, `DEG90`, `DEG180`)
  and `Q15` (sine/cosine and matrices; `Q15 * Q15`, `int16_t * Q15`, `neg_sat()`, `q31()`):
  `fx_sin`, `fx_cos` and `fx_atan2` use these types, as do `build_matrix`, `rotate`, the cameras
  (`Angles` with `roll`, `pitch`, `yaw`) and the steering (`Car::steer`). `.raw` gives the raw
  value where the original does arithmetic on it (shifts, 32-bit products).
- **Methods on the car.** `Car` is the `carN.dat` header with `vertices()`, `springs()` and the
  physics as methods (`car.update_springs()`, `car.jump(...)`); `CarSpring::type`/`softness()`,
  `CarVertex::is_wheel()`, `Particle::ICE_CHIP` etc. The car's model (a small bytecode program)
  is decoded at load time into a list of typed instructions, `ModelOp`, with jump and call
  targets as indices; `draw_car` runs it with a local `Program`.
- **References, `bool`, `enum`, lambdas** instead of output pointers, int flags, `#define`s and
  local macros; no `goto`.
- `#pragma once` in every header; the state structs are trivially copyable, with no
  constructors.

The structures that lived in the data segment used to be `packed`, to keep the original layout;
they are ordinary structs now (only `PcxHeader`, a file format, is still packed). Some code
still uses a copy and writes it back, or a pointer, where a reference would do now.

### Global state

The original keeps everything in one 64 KB data segment, addressed by offsets. Here the state is
a few global structures, split by subject (`terep.hpp`, each field with its original offset in a
comment): `world` (gravity, cars, players, particles), `camera`, `render` (clipping, polygons,
visible terrain, scanline tables), `map_tables` (palette, fog and translucency tables) and
`contact` (the last ground contact). `state_reset()` (`state.cpp`) gives them their initial
values. Only what really carries state from one call to the next is there: the asm's scratch
variables, and what the original passed from one function to the next through the segment (the
polygon colour, the scanline range, the terrain rows, the collision planes, the ground probe,
the camera's axes), are locals and parameters now. Some of what stayed is less obvious than it
looks: the particles of a car-vs-car push take their velocity from the last ground contact, and
the visible terrain is kept from the last frame when the view hexagon is clipped away. Besides
them: the cars, the keyboard
(`keys`), and `segments` (heightmap, colour map, textures, screen: 64 KB vectors indexed with
16-bit offsets that wrap like the original's; each car's skin is in its `Car`).

### Cars

The original copies the whole `carN.dat` into the segment and uses the file as the car structure
itself: the header holds the state (steering, throttle, wheels...), the points and springs are
read by offset and the model is a program whose counter is an offset in the segment. Here
`car_read` (`src/car_file.cpp`) parses the file into a `Car` with `std::vector`s of points and
springs and the model decoded (`model_decode`: one `ModelOp` per instruction, the program
counter becomes an index into that list). The game keeps nothing else of the file. The title bug
was fixed in `Car::jump`.

What the original read from `data.bin` is now written by code. The constants live with the code
that uses them: the texts in `game.cpp`, the file names in `terep.hpp`/`load.cpp`, the font in
`text.cpp` (read from `src/font.pbm`, embedded in the executable with `#embed`), the surface, key
and hull tables in `physics.cpp`, the sound patches in `sound.cpp`, the smoke quads in
`scene.cpp`, and the sine and atan tables computed in `math.cpp` (the atan table with a slightly
wrong π, 3.141688765, which gives exactly the original's). Until the data segment was split, it
came out byte for byte identical to the original,
which the old tests compared with the asm (see Verification).

## Verification

`make test` (`tests/run_tests.sh`) replays the recordings made with the Win16 port, one
directory per recording in `tests/` (the map is the one of its `#selected path:` line, under
`data/`), and compares the frames: every frame's hash and a few full frames.

Before that, while the data segment still had the original layout, the tests compared a CRC of
the frame buffer and of the whole data segment, per frame, with the original asm (assembled with
nasm and run in a 16-bit x86 emulator, unicorn) and with an older mechanical C++ translation:
a 600-frame scenario through every key and a dozen random scripts of 3000 frames matched,
except where the original hits the title bug, fixed here (below). That setup is in the git
history (`tools/asm_oracle`, `tests/ref`, `host_test/car_image.cpp`).

Recordings made with the Win16 port are replayed with `terep2-headless -R recdir mapdir`
(`-C` instead of `-R` only compares, writing nothing). `recdir/record.txt` has one call per line,
told apart by its first letter and made exactly as recorded at 1x: `init`, `P` (physics), `R`
(render), `K: 0xcd` (key), `frame: name.pgm` and `frame_fnv1a_64: 0x...`.

- Empty lines and lines starting with `#` are skipped. Before the first `init` nothing else is
  allowed (`... before init`, an error); after it, other lines print `unhandled line: ...`.
- `frame:` saves the frame buffer as `recdir/name_new.pgm` (P5, like the port's dumps) and
  prints how it compares with `recdir/name.pgm`: the number of differing pixels and the
  rectangle around them.
- `frame_fnv1a_64:` compares the FNV-1a hash of the frame buffer; only mismatches are printed.

The last line sums it up, and the exit code is 1 if any frame differs or has no original.

`-s N` replays at a higher scale. The recorded hashes are of 1x frames, so there they are
replaced by the port's own: `-R` writes them to `recdir/hashes_Nx.txt` (one per
`frame_fnv1a_64` line) and `-C` compares with that file, as a regression test of the higher
scales. The `frame:` images are compared by similarity instead: the average colour of each 8 x 8
block (in 1x pixels), whose mean difference must stay under 15 (the finer projection moves
edges and texels by less than a pixel; the same picture gives 1 to 10, another one over 30).
`make test` also replays every recording at each scale it has a `hashes_Nx.txt` for.

## Things found along the way

Bugs and quirks of the original that were **kept** (they are commented in the code):

- **Jump**: the search for the lowest vertex uses AX, which is the low half of the kick, so the
  low half of the kick ends up being the height of the last vertex (`physics.cpp`, `Car::jump`).
- Polygon clipping: the left/top edges skip the intersection when the previous vertex is on the
  edge; right/bottom do not. Texture interpolation in the clipper uses half the precision.
- Projection of the points cut by the near plane uses the fixed centre (160,100), even with the
  screen split.
- `find_ref_vertex` takes its iteration count from the fractional part of the first vertex's x.
- The PCX header is barely validated.

Differences that only show with unusual data: at most 5 cars are loaded (the original's name
tables hold 5, and a sixth car took its name from the bytes after them), and text characters
>= 128 use the glyph of `c & 0x7f` (the original's offsets wrapped around the data segment; only
its jump bug put such characters in the title).

Bug of the original that was **fixed**:

- **Title that gets corrupted**: holding 1/2 while switching cars (Tab/Q) kicks the
  `lowest_vertex` of a car that never jumped, which is 0 (an offset in the segment). The kick is
  added at offset 0x14 of the segment, which is the "H" of "VEHICLE" in the title, and the letter
  turns into garbage. Since letters ≥ 128 read the font table out of place, sometimes even past
  the end of `data.bin`, the corrupted title was not even the same between builds. Now such a
  car looks for its lowest point as on a fresh press, and the jump works.

Bug of the original that can be **fixed on request** (the `fix-ice-bug` option):

- **Ice pillars**: an ice chip that lands lowers the heightmap cell under it by one
  (`particles_update`). The height is a byte, so at 0 it wraps around to 255, the highest
  terrain there is (4080 units): a thin pillar appears out of the ice, keeps throwing the cars
  up, and every chip landing on it lowers it by one again. With the fix the cell stays at 0.
  The recorded tests never chip ice at height 0, so they pass with and without it.

Crash of the original that the port **does not reproduce** (left as it is):

- **Divide overflow when flying high**: projection (`project`, orig 1000:2760) divides with
  16-bit `IDIV`, so the screen coordinate has to fit in 16 bits. A point that the near-plane
  clipper puts at depth `near_clip` (30) overflows when it is more than about 3840 units
  (some 15 cells) above or below the eye. With a car kicked high up (1+2), the chase camera
  follows it and terrain triangles crossing the near plane have corners thousands of units
  below; the quotient does not fit and the CPU raises #DE, which ends the original ("divide by
  zero in 16-bit code" under Wine, with BX = 0x1e = `near_clip`). The port divides in 32 bits
  and keeps the low 16 bits, so the coordinate wraps around instead: the point lands on the
  other side of the screen and that polygon is drawn stretched across it for the frames it
  lasts. It goes back to normal once the car comes down. The same happens at any scale, since
  `near_point` projects before scaling. The tests never get there.

Difference between the Win16 port and `previous_effort`: the spring length (`sqrt`) is rounded
(CVTSS2SI) in the binary, while previous_effort truncated it. That is why it drifted from the
original after a few hundred frames.

## Not done

- There is no Win16/DOS build; the target is portable C++.
