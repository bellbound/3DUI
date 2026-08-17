Unreleased
**No API change** - interface version stays 0.10.1.0, consumers do not need a rebuild.
- Mesh preflight: every model path is inspected before it goes on a form and reaches
  the game's model loader. A mesh that would crash the loader is swapped for
  `meshes\3DUI\orb.nif` and the reason is logged; the element keeps its place in the
  layout instead of taking the game down.
  - Rejected: a `*DynamicTriShape` with no skin data anywhere in the file (the loader
    walks its dynamic vertex buffer through a null pointer - typically a hair mesh
    converted with the wrong NIF Optimizer setting), a block table that does not fit
    inside the file, a header that does not parse, and meshes from other games
    (Fallout 3/NV/4, Starfield, big-endian console builds).
  - Deliberately allowed: Skyrim LE meshes (BS version 83), junk bytes after the last
    block, and models the resource system cannot find - none of those crash, and
    thousands of them load fine every day.
  - Verdicts are cached per path, so a mesh is read once per session. Only the header
    is read, never the geometry.
  - Log lines are tagged `[MeshPreflight]`: `REJECTED <path>: <reason> - <detail>` at
    error level when a mesh fails, plus a warning naming the substitution each time
    that path is requested.

0.9.2
**Breaking changes**
- Replaced `CreateRoot()` with `GetOrCreateRoot()` method
  - Returns existing root if ID already registered, otherwise creates new root
  - Idempotent: safe to call multiple times with the same ID
  - Migration: Replace `api->CreateRoot(config)` with `api->GetOrCreateRoot(config)`
- Removed `GetBuildNumber()`. Use `GetInterfaceVersion()` instead

0.9.0-alpha
- Initial alpha release