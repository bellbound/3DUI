#pragma once

#include "IPositionable.h"

#if !defined(TEST_ENVIRONMENT)
#include <RE/Skyrim.h>
#include <RE/N/NiPointLight.h>
#endif

namespace Projectile {

// A light that participates in the scene graph hierarchy.
// Similar to ControlledProjectile but for lights instead of projectiles.
// Computes world position from parent chain and applies to NiPointLight.
class ControlledLight : public IPositionable {
public:
    ControlledLight() = default;
    ~ControlledLight() override;

    // Non-copyable
    ControlledLight(const ControlledLight&) = delete;
    ControlledLight& operator=(const ControlledLight&) = delete;

    // IPositionable override - creates NiPointLight and registers with ShadowSceneNode
    void Initialize() override;

    // Destroy the light - unregisters and releases
    void Destroy();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // IPositionable override - updates light position from scene graph
    void Update(float deltaTime) override;

    // IPositionable override - controls light visibility
    void SetVisible(bool visible) override;

    // === Light Properties ===
    void SetDiffuse(const RE::NiColor& color);
    void SetDiffuse(float r, float g, float b);
    RE::NiColor GetDiffuse() const { return m_diffuse; }

    void SetRadius(float radius);
    float GetRadius() const { return m_radius; }

    void SetFade(float fade);
    float GetFade() const { return m_fade; }

private:
    void ApplyLightProperties();

#if !defined(TEST_ENVIRONMENT)
    RE::NiPointer<RE::NiPointLight> m_niLight;
    RE::NiPointer<RE::BSLight> m_bsLight;
#endif

    RE::NiColor m_diffuse = { 1.0f, 1.0f, 1.0f };
    float m_radius = 200.0f;
    float m_fade = 1.0f;
    bool m_initialized = false;
};

using ControlledLightPtr = std::shared_ptr<ControlledLight>;

} // namespace Projectile
