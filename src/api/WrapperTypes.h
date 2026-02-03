#pragma once

// =============================================================================
// WrapperTypes.h - Internal header for 3DUI API implementation
// =============================================================================
// This header is NOT part of the public API. It contains:
// - Forward declarations for all wrapper classes
// - Helper conversion functions
// - WrapperRegistry singleton for object lifecycle management
//
// Each wrapper class implements a public interface by delegating to internal
// Projectile::* implementations. The registry maintains ownership and enables
// bidirectional lookup between implementation pointers and API wrappers.
// =============================================================================

#include "../ThreeDUIInterface001.h"
#include "../projectile/IPositionable.h"
#include "../projectile/ControlledProjectile.h"
#include "../projectile/ProjectileDriver.h"
#include "../projectile/FacingStrategy.h"
#include "../projectile/InteractionController.h"
#include "../projectile/Drivers/RootDriver.h"
#include "../projectile/Drivers/HalfWheelProjectileDriver.h"
#include "../projectile/Drivers/RadialProjectileDriver.h"
#include "../projectile/Drivers/ColumnGridProjectileDriver.h"
#include "../projectile/Drivers/RowGridProjectileDriver.h"
#include "../projectile/Drivers/TextDriver.h"

#include <unordered_map>
#include <memory>
#include <string>

namespace P3DUI {

// =============================================================================
// Forward Declarations - Wrapper Classes
// =============================================================================

class ElementWrapper;
class TextWrapper;
class ScrollWheelWrapper;
class WheelWrapper;
class ColumnGridWrapper;
class RowGridWrapper;
class RootWrapper;

// =============================================================================
// Helper Conversion Functions
// =============================================================================

inline Projectile::BillboardMode ToBillboardMode(FacingMode mode) {
    switch (mode) {
        case FacingMode::None:    return Projectile::BillboardMode::None;
        case FacingMode::Full:    return Projectile::BillboardMode::FaceHMD;
        case FacingMode::YawOnly: return Projectile::BillboardMode::YawOnly;
        default:                  return Projectile::BillboardMode::YawOnly;
    }
}

inline FacingMode FromBillboardMode(Projectile::BillboardMode mode) {
    switch (mode) {
        case Projectile::BillboardMode::None:       return FacingMode::None;
        case Projectile::BillboardMode::FacePlayer:
        case Projectile::BillboardMode::FaceHMD:    return FacingMode::Full;
        case Projectile::BillboardMode::YawOnly:    return FacingMode::YawOnly;
        default:                                    return FacingMode::YawOnly;
    }
}

inline EventType ToEventType(Projectile::InputEventType type) {
    switch (type) {
        case Projectile::InputEventType::HoverEnter:   return EventType::HoverEnter;
        case Projectile::InputEventType::HoverExit:    return EventType::HoverExit;
        case Projectile::InputEventType::GrabStart:    return EventType::GrabStart;
        case Projectile::InputEventType::GrabEnd:      return EventType::GrabEnd;
        case Projectile::InputEventType::ActivateDown: return EventType::ActivateDown;
        case Projectile::InputEventType::ActivateUp:   return EventType::ActivateUp;
        default:                                       return EventType::_Unknown;
    }
}

// =============================================================================
// WrapperRegistry - Central storage for all API objects
// =============================================================================
// The registry serves two purposes:
// 1. Owns all wrapper objects via unique_ptr (deterministic cleanup)
// 2. Maintains impl->wrapper mapping for GetParent() and event handling
//
// Thread Safety: All access must be from the game's main thread.

class WrapperRegistry {
public:
    static WrapperRegistry& Get() {
        static WrapperRegistry instance;
        return instance;
    }

    // Storage - unique_ptr ensures proper cleanup
    std::unordered_map<std::string, std::unique_ptr<RootWrapper>> roots;
    std::unordered_map<std::string, std::unique_ptr<ElementWrapper>> elements;
    std::unordered_map<std::string, std::unique_ptr<TextWrapper>> texts;
    std::unordered_map<std::string, std::unique_ptr<ScrollWheelWrapper>> scrollWheels;
    std::unordered_map<std::string, std::unique_ptr<WheelWrapper>> wheels;
    std::unordered_map<std::string, std::unique_ptr<ColumnGridWrapper>> columnGrids;
    std::unordered_map<std::string, std::unique_ptr<RowGridWrapper>> rowGrids;

    // Lookup table: IPositionable* -> Positionable* (for GetParent, event handling)
    std::unordered_map<Projectile::IPositionable*, Positionable*> implToWrapper;

    void RegisterMapping(Projectile::IPositionable* impl, Positionable* wrapper) {
        if (impl && wrapper) {
            implToWrapper[impl] = wrapper;
        }
    }

    void UnregisterMapping(Projectile::IPositionable* impl) {
        implToWrapper.erase(impl);
    }

    Positionable* FindWrapper(Projectile::IPositionable* impl) {
        auto it = implToWrapper.find(impl);
        return it != implToWrapper.end() ? it->second : nullptr;
    }

    // Destroy a wrapper and remove it from all registry maps
    // Called by containers when they clear their children
    // Implemented in WrapperRegistry.cpp (requires wrapper class definitions)
    void Destroy(Positionable* wrapper);

private:
    WrapperRegistry() = default;
};

// =============================================================================
// Wrapper Class Declarations
// =============================================================================
// Each wrapper implements a public API interface by wrapping internal types.
// Common patterns:
// - m_destroyed flag enables tombstone pattern for safe invalidation
// - GetImpl() provides access to underlying implementation
// - MarkDestroyed() called by parent containers during Clear()

class ElementWrapper : public Element {
public:
    ElementWrapper(const ElementConfig& config);
    ~ElementWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Element ===
    void SetModel(const char* nifPath) override;
    const char* GetModel() override;
    void SetTexture(const char* ddsPath) override;
    const char* GetTexture() override;
    void SetTooltip(const wchar_t* text) override;
    const wchar_t* GetTooltip() override;
    void SetScale(float scale) override;
    float GetScale() override;
    void SetFacingMode(FacingMode mode) override;
    FacingMode GetFacingMode() override;
    void SetUseHapticFeedback(bool enabled) override;
    bool GetUseHapticFeedback() override;
    void SetActivateable(bool activateable) override;
    bool IsActivateable() override;
    void SetBackgroundModel(const char* nifPath) override;
    void SetBackgroundScale(float scale) override;
    void ClearBackground() override;

    // === Label Text ===
    void SetLabelText(const wchar_t* text) override;
    const wchar_t* GetLabelText() override;
    void SetLabelTextScale(float scale) override;
    float GetLabelTextScale() override;
    void SetLabelTextVisible(bool visible) override;
    bool IsLabelTextVisible() override;
    void SetLabelOffset(float x, float y, float z) override;
    void ClearLabelText() override;

    // Internal access
    Projectile::ControlledProjectilePtr GetImpl() { return m_impl; }
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

private:
    void ApplyFormCorrections(uint32_t formID);

    std::string m_id;
    Projectile::ControlledProjectilePtr m_impl;
    bool m_destroyed;
};

class TextWrapper : public Text {
public:
    TextWrapper(const TextConfig& config);
    ~TextWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Text ===
    void SetText(const wchar_t* text) override;
    const wchar_t* GetText() override;
    void SetScale(float scale) override;
    void SetFacingMode(FacingMode mode) override;

    // Internal access
    std::shared_ptr<Projectile::TextDriver> GetImpl() { return m_impl; }
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

private:
    std::string m_id;
    std::shared_ptr<Projectile::TextDriver> m_impl;
    bool m_destroyed;
};

class ScrollWheelWrapper : public Container {
public:
    ScrollWheelWrapper(const ScrollWheelConfig& config);
    ~ScrollWheelWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Container ===
    void AddChild(Positionable* child) override;
    void SetChildren(Positionable** children, uint32_t count) override;
    void Clear() override;
    uint32_t GetChildCount() override;
    Positionable* GetChildAt(uint32_t index) override;
    void SetUseHapticFeedback(bool enabled) override;
    bool GetUseHapticFeedback() override;

    // Internal access
    std::shared_ptr<Projectile::HalfWheelProjectileDriver> GetImpl() { return m_impl; }
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

private:
    std::string m_id;
    std::shared_ptr<Projectile::HalfWheelProjectileDriver> m_impl;
    std::vector<Positionable*> m_children;
    bool m_destroyed;
};

class WheelWrapper : public Container {
public:
    WheelWrapper(const WheelConfig& config);
    ~WheelWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Container ===
    void AddChild(Positionable* child) override;
    void SetChildren(Positionable** children, uint32_t count) override;
    void Clear() override;
    uint32_t GetChildCount() override;
    Positionable* GetChildAt(uint32_t index) override;
    void SetUseHapticFeedback(bool enabled) override;
    bool GetUseHapticFeedback() override;

    // Internal access
    std::shared_ptr<Projectile::RadialProjectileDriver> GetImpl() { return m_impl; }
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

private:
    std::string m_id;
    std::shared_ptr<Projectile::RadialProjectileDriver> m_impl;
    std::vector<Positionable*> m_children;
    bool m_destroyed;
};

class ColumnGridWrapper : public ScrollableContainer {
public:
    ColumnGridWrapper(const ColumnGridConfig& config);
    ~ColumnGridWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Container ===
    void AddChild(Positionable* child) override;
    void SetChildren(Positionable** children, uint32_t count) override;
    void Clear() override;
    uint32_t GetChildCount() override;
    Positionable* GetChildAt(uint32_t index) override;
    void SetUseHapticFeedback(bool enabled) override;
    bool GetUseHapticFeedback() override;

    // === ScrollableContainer ===
    float GetScrollPosition() override;
    void SetScrollPosition(float position) override;
    void ScrollToChild(uint32_t index) override;
    void ResetScroll() override;
    void SetFillDirection(VerticalFill verticalFill, HorizontalFill horizontalFill) override;
    void SetOrigin(VerticalOrigin verticalOrigin, HorizontalOrigin horizontalOrigin) override;

    // Internal access
    std::shared_ptr<Projectile::ColumnGridProjectileDriver> GetImpl() { return m_impl; }
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

private:
    std::string m_id;
    std::shared_ptr<Projectile::ColumnGridProjectileDriver> m_impl;
    std::vector<Positionable*> m_children;
    bool m_destroyed;
};

class RowGridWrapper : public ScrollableContainer {
public:
    RowGridWrapper(const RowGridConfig& config);
    ~RowGridWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Container ===
    void AddChild(Positionable* child) override;
    void SetChildren(Positionable** children, uint32_t count) override;
    void Clear() override;
    uint32_t GetChildCount() override;
    Positionable* GetChildAt(uint32_t index) override;
    void SetUseHapticFeedback(bool enabled) override;
    bool GetUseHapticFeedback() override;

    // === ScrollableContainer ===
    float GetScrollPosition() override;
    void SetScrollPosition(float position) override;
    void ScrollToChild(uint32_t index) override;
    void ResetScroll() override;
    void SetFillDirection(VerticalFill verticalFill, HorizontalFill horizontalFill) override;
    void SetOrigin(VerticalOrigin verticalOrigin, HorizontalOrigin horizontalOrigin) override;

    // Internal access
    std::shared_ptr<Projectile::RowGridProjectileDriver> GetImpl() { return m_impl; }
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

private:
    std::string m_id;
    std::shared_ptr<Projectile::RowGridProjectileDriver> m_impl;
    std::vector<Positionable*> m_children;
    bool m_destroyed;
};

class RootWrapper : public Root {
public:
    RootWrapper(const RootConfig& config);
    ~RootWrapper();

    // === Positionable ===
    const char* GetID() override;
    void SetLocalPosition(float x, float y, float z) override;
    void GetLocalPosition(float& x, float& y, float& z) override;
    void GetWorldPosition(float& x, float& y, float& z) override;
    void SetVisible(bool visible) override;
    bool IsVisible() override;
    Positionable* GetParent() override;

    // === Container ===
    void AddChild(Positionable* child) override;
    void SetChildren(Positionable** children, uint32_t count) override;
    void Clear() override;
    uint32_t GetChildCount() override;
    Positionable* GetChildAt(uint32_t index) override;
    void SetUseHapticFeedback(bool enabled) override;
    bool GetUseHapticFeedback() override;

    // === Root ===
    Positionable* Find(const char* id) override;
    void SetFacingMode(FacingMode mode) override;
    void SetVRAnchor(VRAnchorType anchor) override;
    void StartPositioning(bool isLeftHand) override;
    void EndPositioning() override;
    bool IsGrabbing() override;
    void SetTooltipsEnabled(bool enabled) override;
    bool GetTooltipsEnabled() override;

    // Internal access
    void MarkDestroyed() { m_destroyed = true; }
    bool IsDestroyed() const { return m_destroyed; }

    // Query methods used by Interface001
    bool IsHandInteracting(bool isLeft) const;
    Positionable* GetHoveredItem(bool isLeft) const;

private:
    Positionable* FindRecursive(Positionable* node, const char* id);

    std::string m_id;
    std::string m_modId;
    std::unique_ptr<Projectile::RootDriver> m_driver;
    EventCallback m_eventCallback;
    std::vector<Positionable*> m_children;
    bool m_destroyed;
};

} // namespace P3DUI
