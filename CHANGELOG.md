# Changelog

## 0.1.0 – Milestone 1: Foundations (30 Oct 2026)

- Slave Engine core: deterministic fixed-point maths, integer CORDIC atan2,
  seeded PCG random numbers, entity/component storage with generation-checked
  handles, logging.
- Simulation at a fixed 15 ticks per second: spawn / move / stop / remove
  commands, 16-step visible turning, group moves in formation, state hash.
- Renderer: SDL3 window, OpenGL 3.3 core, instanced sprite batching, team-colour
  shader, chunked terrain, pixel-snapped camera with 0.5× / 1× / 2× zoom.
- Voxel baker: renders voxel models into isometric sprites with three-tone
  shading, lit edges, outline and drop shadow, in 16 facings.
- Placeholder art for Germany and Russia (medium tank, scout car, rifleman),
  trees, pines, bushes, rocks and procedural grass, meadow and dirt tiles.
- Random countryside maps up to 1024 × 1024 tiles.
- Controls: click and box select, double-click select-by-type, right-click move,
  control groups, side switching, pause, game speed, fullscreen, screenshots.
- Debug panel with spawn tools (up to 10,000 units at once).
- 13 unit tests, including a determinism test with 1,000 moving units.
- GitHub Actions: Windows build and tests on every push.
