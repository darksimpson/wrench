# Arduino sketches

The Arduino IDE compiles only the files in a sketch's folder, so each sketch
needs its own copy of the single file build:

1. Download `wrench.h` and `wrench.cpp` from the assets of a release on the
   repository's Releases page, or generate them in the repository root with
   `make dist` (Linux, writes `dist/`) or `wrench release discrete_src dist`.
2. Copy both into the sketch folder (`hello_world/`, `debug/`); git ignores
   them there.

For a sketch that can be debugged from VS Code see `../debug_targets`.
