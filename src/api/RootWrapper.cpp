#include "WrapperTypes.h"
#include "../projectile/InteractionController.h"
#include "../util/VRNodes.h"
#include "../log.h"

#include <cstring>

namespace P3DUI {

// =============================================================================
// RootWrapper Implementation
// =============================================================================
// Wraps a RootDriver to implement the Root interface.
// Root is the top-level container that:
// - Manages VR interaction (hover, grab, activate via InteractionController)
// - Controls facing behavior (how the menu orients toward the player)
// - Provides VR anchoring (attach to HMD, hand controllers)
// - Dispatches events to consumer callbacks
//
// A typical usage flow:
// 1. CreateRoot() with eventCallback
// 2. Add children (elements, containers)
// 3. SetVRAnchor() and SetFacingMode()
// 4. SetVisible(true) to show and start interaction

RootWrapper::RootWrapper(const RootConfig& config)
    : m_id(config.id ? config.id : "")
    , m_modId(config.modId ? config.modId : "")
    , m_driver(std::make_unique<Projectile::RootDriver>())
    , m_eventCallback(config.eventCallback)
    , m_destroyed(false)
{
    m_driver->SetID(m_id);

    // Set up interaction if enabled
    if (config.interactive) {
        auto interaction = std::make_unique<Widget::InteractionController>();
        if (config.activationButtonMask != 0) {
            interaction->SetActivationButtons(config.activationButtonMask);
        }
        if (config.grabButtonMask != 0) {
            interaction->SetGrabButtons(config.grabButtonMask);
        }
        if (config.hoverThreshold > 0.0f) {
            interaction->SetHoverThreshold(config.hoverThreshold);
        }
        m_driver->SetInteractionController(std::move(interaction));
    }

    // Set up event bridging - convert internal events to public API events
    if (m_eventCallback) {
        m_driver->SetOnEvent([this](const Projectile::InputEvent& event) {
            Event apiEvent{};
            apiEvent.structSize = sizeof(Event);
            apiEvent.type = ToEventType(event.type);
            apiEvent.source = WrapperRegistry::Get().FindWrapper(event.source);
            apiEvent.sourceID = event.source ? event.source->GetID().c_str() : nullptr;
            apiEvent.handNode = event.handNode;
            apiEvent.isLeftHand = event.isLeftHand;

            // Exception guard - prevent consumer exceptions from crashing Skyrim
            try {
                m_eventCallback(&apiEvent);
            } catch (const std::exception& e) {
                spdlog::error("P3DUI: EventCallback threw exception: {}", e.what());
            } catch (...) {
                spdlog::error("P3DUI: EventCallback threw unknown exception");
            }
        });
    }

    // Default facing strategy - full 3D facing toward HMD
    m_driver->SetFacingStrategy(&Projectile::FullFacingStrategy::Instance());

    // Start hidden - SetVisible(true) will initialize and register with DriverUpdateManager

    WrapperRegistry::Get().RegisterMapping(m_driver.get(), this);
}

RootWrapper::~RootWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_driver.get());
}

// =============================================================================
// Positionable Interface
// =============================================================================

const char* RootWrapper::GetID() {
    return m_id.c_str();
}

void RootWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_driver->SetLocalPosition({x, y, z});
}

void RootWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_driver->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void RootWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_driver->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void RootWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    bool wasVisible = m_driver->IsVisible();
    m_driver->SetVisible(visible);
    if (visible && !wasVisible) {
        spdlog::info("[{}] Menu '{}' shown", m_modId, m_id);
    } else if (!visible && wasVisible) {
        spdlog::info("[{}] Menu '{}' hidden", m_modId, m_id);
    }
}

bool RootWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_driver->IsVisible();
}

Positionable* RootWrapper::GetParent() {
    return nullptr;  // Root has no parent
}

// =============================================================================
// Container Interface
// =============================================================================

void RootWrapper::AddChild(Positionable* child) {
    if (m_destroyed) return;
    if (!child) return;

    // Handle Element children
    if (auto* elem = dynamic_cast<ElementWrapper*>(child)) {
        if (elem->IsDestroyed()) {
            spdlog::warn("P3DUI::Root::AddChild: Element '{}' was destroyed", elem->GetID());
            return;
        }
        if (elem->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::Root::AddChild: Element '{}' already has a parent", elem->GetID());
            return;
        }
        m_driver->AddChild(elem->GetImpl());
        m_children.push_back(child);
        spdlog::trace("[{}] Root '{}' AddChild: Element '{}' (total: {})",
            m_modId, m_id, elem->GetID(), m_children.size());
        return;
    }

    // Handle Text children
    if (auto* text = dynamic_cast<TextWrapper*>(child)) {
        if (text->IsDestroyed()) {
            spdlog::warn("P3DUI::Root::AddChild: Text '{}' was destroyed", text->GetID());
            return;
        }
        if (text->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::Root::AddChild: Text '{}' already has a parent", text->GetID());
            return;
        }
        m_driver->AddChild(text->GetImpl());
        m_children.push_back(child);
        spdlog::trace("[{}] Root '{}' AddChild: Text '{}' (total: {})",
            m_modId, m_id, text->GetID(), m_children.size());
        return;
    }

    // Handle ScrollWheel children
    if (auto* scrollWheel = dynamic_cast<ScrollWheelWrapper*>(child)) {
        if (scrollWheel->IsDestroyed()) {
            spdlog::warn("P3DUI::Root::AddChild: ScrollWheel '{}' was destroyed", scrollWheel->GetID());
            return;
        }
        if (scrollWheel->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::Root::AddChild: ScrollWheel '{}' already has a parent", scrollWheel->GetID());
            return;
        }
        m_driver->AddChild(scrollWheel->GetImpl());
        m_children.push_back(child);
        spdlog::trace("[{}] Root '{}' AddChild: ScrollWheel '{}' (total: {})",
            m_modId, m_id, scrollWheel->GetID(), m_children.size());
        return;
    }

    // Handle Wheel children
    if (auto* wheel = dynamic_cast<WheelWrapper*>(child)) {
        if (wheel->IsDestroyed()) {
            spdlog::warn("P3DUI::Root::AddChild: Wheel '{}' was destroyed", wheel->GetID());
            return;
        }
        if (wheel->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::Root::AddChild: Wheel '{}' already has a parent", wheel->GetID());
            return;
        }
        m_driver->AddChild(wheel->GetImpl());
        m_children.push_back(child);
        spdlog::trace("[{}] Root '{}' AddChild: Wheel '{}' (total: {})",
            m_modId, m_id, wheel->GetID(), m_children.size());
        return;
    }

    // Handle ColumnGrid children
    if (auto* colGrid = dynamic_cast<ColumnGridWrapper*>(child)) {
        if (colGrid->IsDestroyed()) {
            spdlog::warn("P3DUI::Root::AddChild: ColumnGrid '{}' was destroyed", colGrid->GetID());
            return;
        }
        if (colGrid->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::Root::AddChild: ColumnGrid '{}' already has a parent", colGrid->GetID());
            return;
        }
        m_driver->AddChild(colGrid->GetImpl());
        m_children.push_back(child);
        spdlog::trace("[{}] Root '{}' AddChild: ColumnGrid '{}' (total: {})",
            m_modId, m_id, colGrid->GetID(), m_children.size());
        return;
    }

    // Handle RowGrid children
    if (auto* rowGrid = dynamic_cast<RowGridWrapper*>(child)) {
        if (rowGrid->IsDestroyed()) {
            spdlog::warn("P3DUI::Root::AddChild: RowGrid '{}' was destroyed", rowGrid->GetID());
            return;
        }
        if (rowGrid->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::Root::AddChild: RowGrid '{}' already has a parent", rowGrid->GetID());
            return;
        }
        m_driver->AddChild(rowGrid->GetImpl());
        m_children.push_back(child);
        spdlog::trace("[{}] Root '{}' AddChild: RowGrid '{}' (total: {})",
            m_modId, m_id, rowGrid->GetID(), m_children.size());
        return;
    }
}

void RootWrapper::SetChildren(Positionable** children, uint32_t count) {
    if (m_destroyed) return;
    if (count > 0 && !children) {
        spdlog::warn("P3DUI::Root::SetChildren: null array with count {}", count);
        return;
    }
    spdlog::trace("[{}] Root '{}' SetChildren: count={}", m_modId, m_id, count);
    Clear();
    for (uint32_t i = 0; i < count; ++i) {
        AddChild(children[i]);
    }
}

void RootWrapper::Clear() {
    if (m_destroyed) return;
    spdlog::trace("[{}] Root '{}' Clear: removing {} children", m_modId, m_id, m_children.size());

    // Mark children as destroyed (tombstone pattern) then remove from registry
    for (auto* child : m_children) {
        if (auto* elem = dynamic_cast<ElementWrapper*>(child)) {
            elem->MarkDestroyed();
        } else if (auto* text = dynamic_cast<TextWrapper*>(child)) {
            text->MarkDestroyed();
        } else if (auto* scrollWheel = dynamic_cast<ScrollWheelWrapper*>(child)) {
            scrollWheel->MarkDestroyed();
        } else if (auto* wheel = dynamic_cast<WheelWrapper*>(child)) {
            wheel->MarkDestroyed();
        } else if (auto* colGrid = dynamic_cast<ColumnGridWrapper*>(child)) {
            colGrid->MarkDestroyed();
        } else if (auto* rowGrid = dynamic_cast<RowGridWrapper*>(child)) {
            rowGrid->MarkDestroyed();
        }
        WrapperRegistry::Get().Destroy(child);
    }

    m_driver->Clear();
    m_children.clear();
}

uint32_t RootWrapper::GetChildCount() {
    if (m_destroyed) return 0;
    return static_cast<uint32_t>(m_children.size());
}

Positionable* RootWrapper::GetChildAt(uint32_t index) {
    if (m_destroyed) return nullptr;
    return index < m_children.size() ? m_children[index] : nullptr;
}

void RootWrapper::SetUseHapticFeedback(bool enabled) {
    if (m_destroyed) return;
    m_driver->SetUseHapticFeedback(enabled);
}

bool RootWrapper::GetUseHapticFeedback() {
    if (m_destroyed) return true;
    return m_driver->GetUseHapticFeedback();
}

// =============================================================================
// Root Interface - Lookup
// =============================================================================

Positionable* RootWrapper::Find(const char* id) {
    if (m_destroyed) return nullptr;
    if (!id) return nullptr;
    return FindRecursive(this, id);
}

Positionable* RootWrapper::FindRecursive(Positionable* node, const char* id) {
    if (!node || !id) return nullptr;

    // Check this node
    if (std::strcmp(node->GetID(), id) == 0) {
        return node;
    }

    // Check children if this is a container
    if (auto* container = dynamic_cast<Container*>(node)) {
        uint32_t count = container->GetChildCount();
        for (uint32_t i = 0; i < count; ++i) {
            if (auto* found = FindRecursive(container->GetChildAt(i), id)) {
                return found;
            }
        }
    }

    return nullptr;
}

// =============================================================================
// Root Interface - Facing & Anchoring
// =============================================================================

void RootWrapper::SetFacingMode(FacingMode mode) {
    if (m_destroyed) return;
    switch (mode) {
        case FacingMode::None:
            m_driver->SetFacingStrategy(nullptr);
            break;
        case FacingMode::Full:
            m_driver->SetFacingStrategy(&Projectile::FullFacingStrategy::Instance());
            break;
        case FacingMode::YawOnly:
            m_driver->SetFacingStrategy(&Projectile::YawOnlyFacingStrategy::Instance());
            break;
    }
}

void RootWrapper::SetVRAnchor(VRAnchorType anchor) {
    if (m_destroyed) return;
    RE::NiAVObject* node = nullptr;

    switch (anchor) {
        case VRAnchorType::HMD:
            node = VRNodes::GetHMD();
            break;
        case VRAnchorType::LeftHand:
            node = VRNodes::GetLeftHand();
            break;
        case VRAnchorType::RightHand:
            node = VRNodes::GetRightHand();
            break;
        case VRAnchorType::None:
        default:
            break;
    }

    // Set both facing anchor and position anchor to the same VR node
    m_driver->SetFacingAnchor(node);
    m_driver->SetAnchor(node);
}

// =============================================================================
// Root Interface - Grab/Positioning
// =============================================================================

void RootWrapper::StartPositioning(bool isLeftHand) {
    if (m_destroyed) return;
    m_driver->StartDriverPositioning(isLeftHand);
}

void RootWrapper::EndPositioning() {
    if (m_destroyed) return;
    m_driver->EndDriverPositioning();
}

bool RootWrapper::IsGrabbing() {
    if (m_destroyed) return false;
    return m_driver->IsGrabbing();
}

void RootWrapper::SetTooltipsEnabled(bool enabled) {
    if (m_destroyed) return;
    auto* interaction = m_driver->GetInteractionController();
    if (interaction) {
        interaction->SetDisplayTooltip(enabled);
    }
}

bool RootWrapper::GetTooltipsEnabled() {
    if (m_destroyed) return true;  // Default is enabled
    auto* interaction = m_driver->GetInteractionController();
    return interaction ? interaction->GetDisplayTooltip() : true;
}

// =============================================================================
// Internal Query Methods (used by Interface001)
// =============================================================================

bool RootWrapper::IsHandInteracting(bool isLeft) const {
    if (m_destroyed || !m_driver) return false;
    if (!m_driver->IsVisible()) return false;  // Hidden roots can't be interacting
    auto* interaction = m_driver->GetInteractionController();
    return interaction && interaction->IsHandInteracting(isLeft);
}

Positionable* RootWrapper::GetHoveredItem(bool isLeft) const {
    if (m_destroyed || !m_driver) return nullptr;
    if (!m_driver->IsVisible()) return nullptr;
    auto* interaction = m_driver->GetInteractionController();
    if (!interaction) return nullptr;
    auto hoveredProjectile = interaction->GetHoveredProjectile(isLeft);
    if (!hoveredProjectile) return nullptr;
    return WrapperRegistry::Get().FindWrapper(hoveredProjectile.get());
}

} // namespace P3DUI
