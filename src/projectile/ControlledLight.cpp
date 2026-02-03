#include "ControlledLight.h"

#if !defined(TEST_ENVIRONMENT)
#include <RE/B/BSShaderManager.h>
#include <RE/S/ShadowSceneNode.h>
#endif

#include "../log.h"

namespace Projectile {

ControlledLight::~ControlledLight() {
    Destroy();
}

void ControlledLight::Initialize() {
#if !defined(TEST_ENVIRONMENT)
    if (m_initialized) {
        spdlog::warn("ControlledLight::Initialize - Already initialized");
        return;
    }

    // Create NiPointLight
    m_niLight.reset(RE::NiPointLight::Create());
    if (!m_niLight) {
        spdlog::error("ControlledLight::Initialize - Failed to create NiPointLight");
        return;
    }

    m_niLight->name = "ControlledLight";

    // Apply initial properties
    ApplyLightProperties();

    // Register with ShadowSceneNode (no scene graph attachment needed)
    auto& shaderState = RE::BSShaderManager::State::GetSingleton();
    if (!shaderState.shadowSceneNode[0]) {
        spdlog::warn("ControlledLight::Initialize - shadowSceneNode[0] is null, light may not render");
        m_initialized = true;  // Still mark as initialized so we don't retry
        return;
    }
    auto* shadowSceneNode = shaderState.shadowSceneNode[0];
    if (shadowSceneNode) {
        // Check if light is already registered (safety check like LightPlacer)
        auto* existingBsLight = shadowSceneNode->GetPointLight(m_niLight.get());
        if (existingBsLight) {
            spdlog::warn("ControlledLight::Initialize - Light already registered with ShadowSceneNode, reusing");
            m_bsLight.reset(existingBsLight);
        } else {
            RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
            params.dynamic = true;
            params.shadowLight = false;
            params.portalStrict = false;
            params.affectLand = true;
            params.affectWater = true;
            params.neverFades = true;
            params.fov = 0.0f;
            params.falloff = 1.0f;
            params.nearDistance = 5.0f;
            params.depthBias = 1.0f;
            params.sceneGraphIndex = 0;
            params.restrictedNode = nullptr;
            params.lensFlareData = nullptr;

            m_bsLight.reset(shadowSceneNode->AddLight(m_niLight.get(), params));
            if (m_bsLight) {
                spdlog::info("ControlledLight::Initialize - Light registered with ShadowSceneNode");
            } else {
                spdlog::warn("ControlledLight::Initialize - AddLight returned nullptr, light may not render correctly");
            }
        }
    } else {
        spdlog::warn("ControlledLight::Initialize - No ShadowSceneNode available");
    }

    m_initialized = true;
    spdlog::info("ControlledLight::Initialize - Light created");
#endif
}

void ControlledLight::Destroy() {
#if !defined(TEST_ENVIRONMENT)
    if (!m_initialized) {
        return;
    }

    // Unregister from ShadowSceneNode
    if (m_bsLight) {
        auto& shaderState = RE::BSShaderManager::State::GetSingleton();
        if (shaderState.shadowSceneNode[0]) {
            shaderState.shadowSceneNode[0]->RemoveLight(m_bsLight);
        }
        m_bsLight = nullptr;
    }

    // Release the light
    m_niLight = nullptr;

    m_initialized = false;
    spdlog::info("ControlledLight::Destroy - Light destroyed");
#endif
}

void ControlledLight::Update(float /*deltaTime*/) {
#if !defined(TEST_ENVIRONMENT)
    if (!m_initialized || !m_niLight || !m_localVisible) {
        return;
    }

    // Set light position directly from scene graph hierarchy
    m_niLight->world.translate = GetWorldPosition();
#endif
}

void ControlledLight::SetVisible(bool visible) {
    IPositionable::SetVisible(visible);

#if !defined(TEST_ENVIRONMENT)
    if (m_niLight) {
        m_niLight->SetAppCulled(!visible);
    }
#endif
}

void ControlledLight::SetDiffuse(const RE::NiColor& color) {
    m_diffuse = color;
    ApplyLightProperties();
}

void ControlledLight::SetDiffuse(float r, float g, float b) {
    m_diffuse = { r, g, b };
    ApplyLightProperties();
}

void ControlledLight::SetRadius(float radius) {
    m_radius = radius;
    ApplyLightProperties();
}

void ControlledLight::SetFade(float fade) {
    m_fade = fade;
    ApplyLightProperties();
}

void ControlledLight::ApplyLightProperties() {
#if !defined(TEST_ENVIRONMENT)
    if (!m_niLight) {
        return;
    }

    auto& lightData = m_niLight->GetLightRuntimeData();
    lightData.ambient = { 0.0f, 0.0f, 0.0f };
    lightData.diffuse = m_diffuse;
    lightData.fade = m_fade;
    lightData.radius = { m_radius, m_radius, m_radius };
    m_niLight->SetLightAttenuation(m_radius);
#endif
}

} // namespace Projectile
