#include "WrapperTypes.h"
#include "../projectile/ProjectileCorrections.h"
#include "../log.h"

namespace P3DUI {

// =============================================================================
// ElementWrapper Implementation
// =============================================================================
// Wraps a ControlledProjectile to implement the Element interface.
// Supports three visual modes:
// 1. Texture mode: flat icon using texturePath
// 2. Model mode: 3D model using modelPath
// 3. Form mode: auto-derived model from formID with automatic corrections

ElementWrapper::ElementWrapper(const ElementConfig& config)
    : m_id(config.id ? config.id : "")
    , m_impl(std::make_shared<Projectile::ControlledProjectile>())
    , m_destroyed(false)
{
    m_impl->SetID(m_id);

    // Apply visual config - texture takes priority over model
    if (config.texturePath && *config.texturePath) {
        m_impl->SetTexturePath(config.texturePath);
    } else if (config.modelPath && *config.modelPath) {
        m_impl->SetModelPath(config.modelPath);
    }

    // Internal scale multiplier: user scale 1.0 = effective 0.25 (VR-friendly size)
    m_impl->SetBaseScale(config.scale * 0.25f);

    // Apply corrections: formID-based auto-corrections OR manual rotation
    if (config.formID != 0) {
        // Auto-corrections based on form type (overrides manual rotation)
        ApplyFormCorrections(config.formID);
    } else {
        // Manual rotation correction
        m_impl->SetRotationCorrection({config.rotationPitch, config.rotationRoll, config.rotationYaw});
    }

    m_impl->SetBillboardMode(ToBillboardMode(config.facingMode));

    if (config.tooltip && *config.tooltip) {
        m_impl->SetText(config.tooltip);
    }

    // Anchor handle behavior - element acts as grab handle for menu positioning
    if (config.isAnchorHandle) {
        m_impl->SetIsAnchorHandle(true);
    }

    // Per-element hover threshold override
    if (config.hoverThreshold > 0.0f) {
        m_impl->SetHoverThresholdOverride(config.hoverThreshold);
    }

    // Transform smoothing
    m_impl->SetSmoothingSpeed(config.smoothingFactor);

    // Register mapping for GetParent() and event handling
    WrapperRegistry::Get().RegisterMapping(m_impl.get(), this);
}

ElementWrapper::~ElementWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_impl.get());
}

// =============================================================================
// Form-based Corrections
// =============================================================================
// When an element is created with a formID, we look up the form and apply
// rotation/scale corrections automatically. This handles the fact that
// different item types (weapons, armor, potions) have different default
// orientations in their NIFs.

void ElementWrapper::ApplyFormCorrections(uint32_t formID) {
    auto* form = RE::TESForm::LookupByID(formID);
    if (!form) {
        spdlog::warn("ElementWrapper: FormID {:08X} not found, skipping corrections", formID);
        return;
    }

    // Apply form-based rotation correction (handles all item categories)
    Projectile::ProjectileCorrections::ApplyRotationCorrectionFor(m_impl.get(), form);

    // Apply scale correction if we can get bound data
    if (auto* boundObj = form->As<RE::TESBoundObject>()) {
        Projectile::ProjectileCorrections::ApplyScaleCorrectionFor(m_impl.get(), boundObj);
    }
}

// =============================================================================
// Positionable Interface
// =============================================================================

const char* ElementWrapper::GetID() {
    return m_id.c_str();
}

void ElementWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLocalPosition({x, y, z});
}

void ElementWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void ElementWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void ElementWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetVisible(visible);
}

bool ElementWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_impl->IsVisible();
}

Positionable* ElementWrapper::GetParent() {
    if (m_destroyed) return nullptr;
    auto* parent = m_impl->GetParent();
    return parent ? WrapperRegistry::Get().FindWrapper(parent) : nullptr;
}

// =============================================================================
// Element Interface - Visual Configuration
// =============================================================================

void ElementWrapper::SetModel(const char* nifPath) {
    if (m_destroyed) return;
    if (nifPath) m_impl->SetModelPath(nifPath);
}

const char* ElementWrapper::GetModel() {
    if (m_destroyed) return "";
    return m_impl->GetModelPath().c_str();
}

void ElementWrapper::SetTexture(const char* ddsPath) {
    if (m_destroyed) return;
    if (ddsPath) m_impl->SetTexturePath(ddsPath);
}

const char* ElementWrapper::GetTexture() {
    if (m_destroyed) return "";
    return m_impl->GetTexturePath().c_str();
}

void ElementWrapper::SetTooltip(const wchar_t* text) {
    if (m_destroyed) return;
    if (text) m_impl->SetText(text);
}

const wchar_t* ElementWrapper::GetTooltip() {
    if (m_destroyed) return L"";
    return m_impl->GetText().c_str();
}

void ElementWrapper::SetScale(float scale) {
    if (m_destroyed) return;
    m_impl->SetBaseScale(scale);
}

float ElementWrapper::GetScale() {
    if (m_destroyed) return 1.0f;
    return m_impl->GetBaseScale();
}

void ElementWrapper::SetFacingMode(FacingMode mode) {
    if (m_destroyed) return;
    m_impl->SetBillboardMode(ToBillboardMode(mode));
}

FacingMode ElementWrapper::GetFacingMode() {
    if (m_destroyed) return FacingMode::None;
    return FromBillboardMode(m_impl->GetBillboardMode());
}

// =============================================================================
// Element Interface - Interaction
// =============================================================================

void ElementWrapper::SetUseHapticFeedback(bool enabled) {
    if (m_destroyed) return;
    m_impl->SetUseHapticFeedback(enabled);
}

bool ElementWrapper::GetUseHapticFeedback() {
    if (m_destroyed) return true;
    return m_impl->GetUseHapticFeedback();
}

void ElementWrapper::SetActivateable(bool activateable) {
    if (m_destroyed) return;
    m_impl->SetActivateable(activateable);
}

bool ElementWrapper::IsActivateable() {
    if (m_destroyed) return true;
    return m_impl->IsActivateable();
}

// =============================================================================
// Element Interface - Background Projectile
// =============================================================================
// Optional secondary visual rendered at the same position.
// Useful for glow effects, selection highlights, or backing panels.

void ElementWrapper::SetBackgroundModel(const char* nifPath) {
    if (m_destroyed) return;
    m_impl->SetBackgroundModelPath(nifPath ? nifPath : "");
}

void ElementWrapper::SetBackgroundScale(float scale) {
    if (m_destroyed) return;
    m_impl->SetBackgroundScale(scale);
}

void ElementWrapper::ClearBackground() {
    if (m_destroyed) return;
    m_impl->ClearBackground();
}

// =============================================================================
// Element Interface - Label Text
// =============================================================================
// Optional text rendered below the element for item names, descriptions, etc.
// Delegates to ControlledProjectile's label text system which uses TextDriver.

void ElementWrapper::SetLabelText(const wchar_t* text) {
    if (m_destroyed) return;
    if (text) {
        m_impl->SetLabelText(text);
    } else {
        m_impl->ClearLabelText();
    }
}

const wchar_t* ElementWrapper::GetLabelText() {
    if (m_destroyed) return L"";
    return m_impl->GetLabelText().c_str();
}

void ElementWrapper::SetLabelTextScale(float scale) {
    if (m_destroyed) return;
    m_impl->SetLabelTextScale(scale);
}

float ElementWrapper::GetLabelTextScale() {
    if (m_destroyed) return 1.0f;
    return m_impl->GetLabelTextScale();
}

void ElementWrapper::SetLabelTextVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetLabelTextVisible(visible);
}

bool ElementWrapper::IsLabelTextVisible() {
    if (m_destroyed) return true;
    return m_impl->IsLabelTextVisible();
}

void ElementWrapper::SetLabelOffset(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLabelOffset({x, y, z});
}

void ElementWrapper::ClearLabelText() {
    if (m_destroyed) return;
    m_impl->ClearLabelText();
}

} // namespace P3DUI
