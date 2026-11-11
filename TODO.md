# TODO

- [x] Stop loading `data.bin`: put the constants straight in the code and initialise the
      rest dynamically.
- [x] Stop loading the `carN.dat` straight into memory: parse the original file and
      load it sensibly, so that the structs no longer need to be `packed`.
- [x] `Angle` (0x10000 = 360°) and `Q15` (sine/cosine, matrices) types, as `Pos`/`Vec3`, in
      place of loose `uint16_t`/`int16_t` (`build_matrix`, cameras, wheels).
- [x] Break `DataSeg` into structures by subject: `world`, `camera`, `render`, `map_tables`,
      `contact` (terep.hpp, state.cpp); what was only scratch became local or a parameter.
- [ ] A fuller `make test`: more Win16 recordings, a quick test
      at 4x with ASan/UBSan, builds with g++ and clang; CI on GitHub Actions.
- [x] Fix the jump bug that corrupts the title (the car looks for the lowest point as
      in a new squeeze). The renderer's bugs stay as they are.
- [x] A free camera for the player in WebGL: F6 (the checkpoint editor's controls; down to
      0.1 tile over the ground; leaving it with F3 keeps the camera there).

Out of scope here: widescreen (only in the new renderer).
