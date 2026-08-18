0.10.8
**No API change** - interface version 0.10.8.0. Nothing in the header moved, so consumers
built against any 0.10.x keep working without a rebuild.
- `Element::SetTexture` now takes effect on an element that is already on screen. It set the
  library's own copy of the path and stopped there; only `Initialize()` ever handed the path
  to the bound game projectile, so an element created with one texture went on rendering it
  however many times it was told otherwise, until something destroyed and rebuilt it.
  - Anything that swaps an icon in place saw nothing happen: a stepper walking a list and
    repainting its centre, a toggle button showing which of two states it is in. VR NPC
    Editor's overlay stepper was one - the name under it changed on every press and the
    picture never did.
  - The new path marks the texture pending and lets the existing per-frame apply pick it up
    once the projectile's 3D is there, which is the same route the first texture takes, so
    a swap retries across frames rather than being dropped if the node is not ready yet.
  - Setting the same path twice is a no-op, so a consumer repainting unconditionally on
    every frame costs nothing.

0.10.7
**No API change** - interface version 0.10.7.0. Nothing in the header moved, so consumers
built against any 0.10.x keep working without a rebuild.
- An element's model is now centred on the element. A mesh is supposed to be authored around
  its own origin, and a ground object generally is - but plenty of armour records point their
  world model at one of the armour addon's worn meshes (`..._0.nif`) instead, and a worn mesh
  carries its skeleton-space position. Dress Up VR's Bandolier is one: its world model is
  `armor\Dragten\BAN\1Dr_BAN_BandolierLLm_0.nif`, whose geometry sits 97 units up the Z axis,
  so the preview drew roughly 35cm above the orb it belonged to while every other item in the
  row sat in the middle of its own.
  - The offset is measured once per bind from the loaded geometry's model bounds - which the
    engine fills in when the mesh loads, so nothing walks a vertex buffer - and then taken
    back out of the node's translation. The element's logical position is unchanged, so the
    thing you touch and the thing you see now agree; before, for a mesh like this one, they
    were 35cm apart.
  - **A mesh is only recentred when its geometry misses its own origin** - when the distance
    from the origin to the middle of the model exceeds the model's own radius. There is no
    absolute threshold, because "is this the origin the mesh was authored around" has no
    absolute scale. `orb.nif`'s assorted billboards average out ~3 units off a ~45 unit
    radius and are left exactly where they were; the Bandolier is 97 units off a 37 unit
    radius, 2.6x outside its own sphere, and is moved. The rule errs towards leaving meshes
    alone: recentring one that was authored off-origin on purpose is a regression on a mesh
    nobody complained about, while missing a mildly off one costs a barely visible offset.
  - Particle systems are excluded from the measurement. `NiPSysBoundUpdateModifier` rewrites
    a particle system's model bound as particles spawn and die, so folding one in would bake
    in whatever the emission happened to be doing on the frame the measurement latched - and
    two copies of the same mesh would latch different answers. `orb.nif` carries two.
  - `m_modelCenter` stays exactly zero for a mesh that is not being corrected, and the
    per-frame transform skips the correction on that test, so an unaffected element runs the
    same arithmetic it ran before this release. The measurement itself is one walk of the
    model's node tree per bind, reading one already-computed bound per geometry; a mesh with
    no measurable geometry at all (`empty.nif`) gives up after 10 frames rather than
    re-walking forever.
  - Logged as `[CENTER] '<mesh>' is N units off its origin ...` at info when a mesh is
    corrected, trace when it is left alone. The form's own `boundData` cannot answer this:
    measured over a real load order it is all zeros for essentially every armour record,
    which is why `[SCALE]`/`[ORIENT]` report "All bounds are 0" and skip.
  - **Behaviour change for existing consumers.** Anything whose layout was tuned around a
    mesh that misses its own origin - a deliberate offset cancelling the mesh's own - will
    now be off by that amount in the other direction. No stock 3DUI mesh is affected:
    `icon_template.nif` and `character_template.nif` are centred on their origin exactly,
    and `orb.nif` and the two backdrop spheres are inside the leave-alone rule.

0.10.6
**No API change** - interface version 0.10.6.0. Nothing in the header moved, so consumers
built against any 0.10.x keep working without a rebuild.
- A background model no longer inherits the fit correction an element derives from its own
  model's bounds. An element created with a `formID` is scaled by `30 / largest bound
  dimension` so a ring and a greatsword both come out a sensible size in a row; the
  backdrop behind it was a child of that element, so it was multiplied by the same factor
  and every backdrop in a row came out a different size - up to 16x apart between a capped
  scale-up and a greatsword. `SetBackgroundScale` is now the absolute size of the backdrop:
  one value, one size, whatever is in front of it. Position, rotation and hover growth are
  still inherited.
  - **Behaviour change for existing consumers.** Only Dress Up VR ever combined a `formID`
    with a backdrop, and its gallery is what this fixes. A consumer that had tuned its
    backdrop scale against one particular model's correction will see the backdrop change
    size and should re-tune it against the plain element scale.

0.10.5
**Additive API change** - interface version 0.10.5.0. `SetBackgroundColor` took the
`_element_reserved1` slot, so no vtable index moved: consumers built against any
0.10.x keep working against this DLL without a rebuild.
- `Element::SetBackgroundColor(r, g, b, a, glow)` colours an element's background
  model at runtime, so one backdrop mesh can serve every element in a different
  colour instead of needing one mesh per colour. The colour is remembered and
  re-applied whenever the background respawns, so it survives hide/show and scroll
  recycling.
  - Only a mesh whose material is a `BSEffectShaderProperty` can be coloured - that
    is where the base colour lives. Calling it on a lighting-shader mesh does
    nothing, which is the case for the older `cloud-background-sphere.nif`.
  - `glow` sets the emissive multiplier absolutely; pass 0 to leave the mesh's
    authored strength alone.
- New mesh `meshes\3DUI\gradient-background-sphere.nif`: the tintable backdrop. Same
  inverted-normal sphere trick as the cloud one - you see through the front onto the
  inside of the far hemisphere - but with a vertical gradient from vertex colours and
  an edge glow from the effect shader's falloff instead of a cloud texture. Carries no
  texture of its own.
- **Dress Up VR shows a false "Incompatible 3DUI Version" notification against this
  release until it is rebuilt.** Its check is `provider > expected`, which fires on any
  newer 3DUI, additive or not; nothing is actually wrong and the menu works. VR Editor
  and VR NPC Editor use the patch-lane rule (`major`/`minor` equal, provider `patch` >=
  ours) and accept 0.10.5 without a rebuild. A consumer that wants to warn about a newer
  provider should compare only major and minor.

0.10.4
**Additive API change** - interface version 0.10.4.0. `SetVisibleExtent` took the
`_scrollable_reserved1` slot, so no vtable index moved: consumers built against any
0.10.x keep working against this DLL without a rebuild.
- `ScrollableContainer::SetVisibleExtent(float)` resizes the scroll window of a live
  grid - the height of a RowGrid, the width of a ColumnGrid - so one grid can be
  short for one view and tall for another instead of needing a second grid.
- Fixed `Element::SetScale` and `Text::SetScale` skipping the internal scale factor
  their `ElementConfig::scale` / `TextConfig::scale` counterparts apply (0.25 for
  elements, 1.25 for text). An element created at scale 1.0 and one later given
  `SetScale(1.0f)` were four times apart, so any button that changed size at runtime
  jumped. `GetScale` returns the user-facing value to match.
  - **Behaviour change for existing consumers.** A plugin built before this release
    that calls `SetScale` now draws those elements at a quarter of their former size.
    Multiply its scale constants by 4 to keep the size it had. Plugins that only set
    `config.scale` at creation are unaffected.

0.10.3
**No API change** - interface version 0.10.3.0. Nothing in the header moved, so
consumers built against any 0.10.x keep working without a rebuild.
- Fixed the projectile hook scanning every tracked element to identify each update,
  which made the per-frame cost grow with the square of the number of live elements:
  at ~475 elements the lookup alone ran ~30ms a frame plus mutex waits, and menus of
  a few hundred items froze the game outright. The hook now resolves a projectile
  through a reverse index in constant time, hit or miss.
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