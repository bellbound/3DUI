# 3DUI

A library for creating 3D floating UIs in Skyrim VR with automatic layout, VR interaction (hover, grab, scroll), and flexible anchoring.

## Features 

- **Full scene graph positioning** - Position UI elements in local 3D space
- **Automatic Layout for common use cases** - Containers handle child element positioning
- **VR Interaction** - Built-in hover, grab, and scroll support
- **Flexible Anchoring** - Attach UIs to hands, head, or world positions
- **Event System** - Callbacks for hover, activate, and grab events

## Quick Start

```cpp
#include "ThreeDUIInterface001.h"

// Get the interface during kDataLoaded (or later)
P3DUI::Interface001* ui = nullptr;

void InitializeUI() {
    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            ui = P3DUI::GetInterface001();
        }
    });
}
```

## Containers

| Container | Layout | Scrolls | Best For |
|-----------|--------|---------|----------|
| `Wheel` | Concentric rings (360°) | No | Radial menus |
| `ScrollWheel` | Half-wheel | Yes (radial) | Large radial menus |
| `ColumnGrid` | Columns (fill vertical first) | Horizontal | Inventory grids, toolbars |
| `RowGrid` | Rows (fill horizontal first) | Vertical | Category lists |

## Example: Inventory Grid

```cpp
void CreateInventoryUI(P3DUI::Interface001* ui) {
    // Create root anchored to left hand
    P3DUI::RootConfig rootCfg = P3DUI::RootConfig::Default("inventory-root", "MyMod");
    rootCfg.vrAnchor = P3DUI::VRAnchorType::LeftHand;
    rootCfg.eventCallback = [](P3DUI::EventType type, P3DUI::Positionable* target,
                               bool isLeftHand, void* userData) {
        if (type == P3DUI::EventType::ActivateDown) {
            if (auto* elem = dynamic_cast<P3DUI::Element*>(target)) {
                spdlog::info("Selected: {}", elem->GetID());
            }
        }
    };

    auto* root = ui->CreateRoot(rootCfg);

    // Create 3-column scrollable grid
    P3DUI::RowGridConfig gridCfg = P3DUI::RowGridConfig::Default("inv-grid");
    gridCfg.numColumns = 3;
    gridCfg.columnSpacing = 12.0f;
    gridCfg.rowSpacing = 12.0f;
    gridCfg.visibleHeight = 40.0f;

    auto* grid = ui->CreateRowGrid(gridCfg);
    grid->SetOrigin(P3DUI::VerticalOrigin::Top, P3DUI::HorizontalOrigin::Center);
    root->AddChild(grid);

    // Add items
    for (int i = 0; i < 15; i++) {
        P3DUI::ElementConfig elemCfg = P3DUI::ElementConfig::Default(
            ("item-" + std::to_string(i)).c_str());
        elemCfg.texturePath = "Interface/MyMod/item_icon.dds";
        grid->AddChild(ui->CreateElement(elemCfg));
    }

    grid->SetLocalPosition(0.0f, 20.0f, 5.0f);
    root->SetVisible(true);
}
```

## Coordinate System

| Axis | Direction |
|------|-----------|
| **+X** | Right |
| **+Y** | Forward (away from player) |
| **+Z** | Up |

## Documentation

See the [wiki](../../wiki) for complete documentation:

- [Getting Started](../../wiki/Getting-Started)
- [Container Reference](../../wiki/Container-Reference)
- [ColumnGrid](../../wiki/ColumnGrid) / [RowGrid](../../wiki/RowGrid)
- [Fill Direction and Origin](../../wiki/Fill-Direction-and-Origin)
- [Scrolling](../../wiki/Scrolling)
- [Text Fonts & Language Support](../../wiki/Text-Fonts-and-Language-Support)

## How It Works

Heavily inspired by shizof's SpellWheel mod. The 3D elements are projectiles (like arrows or fireballs) for which we control the position, speed, and model.

## Limitations

- **200 unique meshes** max across all mods combined (overflow handled gracefully)
- **50-100 projectiles** recommended limit per scene—more is possible but performance on weaker machines needs testing
- **Scrollable containers** have no practical limit since only visible items are rendered (tested with 500+ items)

## Mods Using This Library

| Mod | Description |
|-----|-------------|
| [VR Editor](https://www.nexusmods.com/skyrimspecialedition/mods/169347) | In-game world editing tools |
| [Dressup VR](https://www.nexusmods.com/skyrimspecialedition/mods/168573) | Outfit management with inventory grids |
