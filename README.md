# 3DUI API Guide

Welcome to the 3DUI library documentation. This is a library for creating 3D floating UIs for Skyrim VR with automatic layout, VR interaction (hover, grab, scroll), and flexible anchoring.

## Examples

### Player-Attached Menu

Uses the `HalfWheel` container (top arc) combined with manually laid out elements for the buttons below. The tooltip on the hand automatically appears when in interaction range if you've set `tooltip` on the element.

<img src="https://raw.githubusercontent.com/wiki/bellbound/3DUI/images/menu-example.jpg" width="256">

---

### Non-Interactive Elements

You can position projectiles anywhere and use only the mesh rendering and positioning — no hand interactivity required. The red sphere below is a 3DUI element.

<img src="https://raw.githubusercontent.com/wiki/bellbound/3DUI/images/controlled-projectile.gif" width="256">

---

### Scrolling

Renders arbitrary meshes in a `ScrollWheel` container. The user grabs and drags to scroll through items.

<img src="https://raw.githubusercontent.com/wiki/bellbound/3DUI/images/scroll.gif" width="256">

## What can I build with this?

This library is well suited for everything that can be built using a composition of floating icons or meshes.

### ✅ Examples 3DUI is well suited for

| Use Case | Description |
|----------|-------------|
| Wheel menus | Menus attached to the player's hand |
| Skill trees | Placed in the world where the player can choose which skill to level |
| Motion-controlled objects | A flying sword controlled by hand movement |
| NPC indicators | Eg. Giving NPCs a Sims-style plumbob |
| Scrollable lists | Selecting from hundreds of options using the scrolling menu |

### ⚠️ Not Well Suited For

| Use Case | Why |
|----------|-----|
| Complex text layouts | Text is supported and can be positioned like any other element, but precise text-heavy layouts would be difficult |
| 50–100+ simultaneous elements | Not a hard limit, but going higher may push the engine's limits |
| Rendering lines | No line-drawing primitives available |

## Quick Links

- **[Overview](https://github.com/bellbound/3DUI/wiki/Overview)** - Node types and hierarchy basics
- **[Getting Started](https://github.com/bellbound/3DUI/wiki/Getting-Started)** - Initialize the API and create your first UI
- **[Complete Example](https://github.com/bellbound/3DUI/wiki/Complete-Example)** - Full working inventory grid


## Actor Menu

The [Actor Menu](https://github.com/bellbound/3DUI/wiki/Actor-Menu) system provides a shared "grab NPC + press trigger" interaction pattern. Multiple mods can register elements that appear when users interact with NPCs, with automatic disambiguation when multiple mods are eligible.


## How It Works Under The Hood

Heavily inspired by shizof's SpellWheel mod. All rendered 3DUI elements are projectiles (like arrows or fireballs) for which we control the position, speed, and model.

Text is a single projectile rendering a mesh, to which we dynamically add submeshes for each character.  

## Limitations

- **200 unique meshes** max visible at the same time across all mods combined (overflow handled gracefully)
- **50-100 projectiles** recommended limit per scene—more is possible but performance on weaker machines needs testing
- **Scrollable containers** have no practical limit since only visible items are rendered (tested with 500+ items)
- **Possibly incompatible with Projectile Modifying mods** SKSE mods that modify all projectiles on the engine level might be incompatible
- **Texture dimensions need to be powers of 2** If you render textures on elements, make sure their dimensions are powers of two, otherwise you will crash when the texture loads on menu open

## Mods Using This Library

| Mod | Description |
|-----|-------------|
| [OStim VR for NPCs](https://www.nexusmods.com/skyrimspecialedition/mods/170863) | NPC interaction menus using 3D UI elements |
| [VR Editor](https://www.nexusmods.com/skyrimspecialedition/mods/169347) | In-game world editing with VR-native toolbars and panels |
| [Dressup VR](https://www.nexusmods.com/skyrimspecialedition/mods/168573) | Outfit management with scrollable inventory grids |
