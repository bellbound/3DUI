#include "WrapperTypes.h"
#include "../log.h"

namespace P3DUI {

// =============================================================================
// TextWrapper Implementation
// =============================================================================
// Wraps a TextDriver to implement the Text interface.
// Renders floating 3D text in the scene, commonly used for labels or tooltips.

TextWrapper::TextWrapper(const TextConfig& config)
    : m_id(config.id ? config.id : "")
    , m_impl(std::make_shared<Projectile::TextDriver>())
    , m_destroyed(false)
{
    m_impl->SetID(m_id);

    if (config.text && *config.text) {
        m_impl->SetText(std::wstring(config.text));
    }
    // Internal scale multiplier: user scale 1.0 = effective 1.25 (VR-readable size)
    m_impl->SetTextScale(config.scale * 1.25f);

    // Match smoothing behavior of other UI elements (icons, buttons)
    // This ensures text moves smoothly when its parent container animates
    m_impl->SetTransitionMode(Projectile::TransitionMode::Lerp);
    m_impl->SetSmoothingSpeed(13.0f);

    // Register mapping for GetParent() lookups
    WrapperRegistry::Get().RegisterMapping(m_impl.get(), this);
}

TextWrapper::~TextWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_impl.get());
}

// =============================================================================
// Positionable Interface
// =============================================================================

const char* TextWrapper::GetID() {
    return m_id.c_str();
}

void TextWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLocalPosition({x, y, z});
}

void TextWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void TextWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void TextWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetVisible(visible);
}

bool TextWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_impl->IsVisible();
}

Positionable* TextWrapper::GetParent() {
    if (m_destroyed) return nullptr;
    auto* parent = m_impl->GetParent();
    return parent ? WrapperRegistry::Get().FindWrapper(parent) : nullptr;
}

// =============================================================================
// Text Interface
// =============================================================================

void TextWrapper::SetText(const wchar_t* text) {
    if (m_destroyed) return;
    if (text) m_impl->SetText(std::wstring(text));
}

const wchar_t* TextWrapper::GetText() {
    if (m_destroyed) return L"";
    return m_impl->GetText().c_str();
}

void TextWrapper::SetScale(float scale) {
    if (m_destroyed) return;
    m_impl->SetTextScale(scale);
}

void TextWrapper::SetFacingMode(FacingMode /*mode*/) {
    if (m_destroyed) return;
    // TextDriver uses billboard mode internally via its projectile
    // The facing mode is managed by the driver itself
}

} // namespace P3DUI
