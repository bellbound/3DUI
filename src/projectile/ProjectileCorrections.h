#pragma once

#if !defined(TEST_ENVIRONMENT)
#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>
#define CORRECTIONS_LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define CORRECTIONS_LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#else
#include "TestStubs.h"
#define CORRECTIONS_LOG_INFO(...) ((void)0)
#define CORRECTIONS_LOG_WARN(...) ((void)0)
#endif

#include <cmath>

namespace Projectile {

// Forward declaration
class ControlledProjectile;

// Utility functions to apply rotation and scale corrections to projectiles
// based on form type or bound object dimensions
namespace ProjectileCorrections {

    // Orient the projectile based on form type using SpellWheelVR-style corrections.
    // Different item categories (weapons, armor, potions, etc.) have different
    // default mesh orientations, so we apply category-specific rotation fixes.
    // If form type doesn't match known categories but can be cast to TESBoundObject,
    // falls back to bound-based dimension analysis.
    inline void ApplyRotationCorrectionFor(ControlledProjectile* proj, RE::TESForm* form);

    // Scale the projectile so its largest dimension fits within TARGET_SIZE units
    // Uses boundData from TESBoundObject to determine dimensions
    // Scales down without limit, but scaling up is capped at MAX_SCALE_UP_CORRECTION
    inline void ApplyScaleCorrectionFor(ControlledProjectile* proj, RE::TESBoundObject* boundObj);

} // namespace ProjectileCorrections

} // namespace Projectile

// Include ControlledProjectile.h after the declarations to avoid circular dependency
#include "ControlledProjectile.h"

namespace Projectile {
namespace ProjectileCorrections {

// Internal helper: Apply bound-based rotation correction as fallback
inline void ApplyBoundBasedRotation(ControlledProjectile* proj, RE::TESBoundObject* boundObj, bool flip = true) {
    if (!proj || !boundObj) return;

    auto& bd = boundObj->boundData;

    float sizeX = static_cast<float>(bd.boundMax.x - bd.boundMin.x);
    float sizeY = static_cast<float>(bd.boundMax.y - bd.boundMin.y);
    float sizeZ = static_cast<float>(bd.boundMax.z - bd.boundMin.z);

    CORRECTIONS_LOG_INFO("[ORIENT] Fallback bounds size: X={:.1f}, Y={:.1f}, Z={:.1f}", sizeX, sizeY, sizeZ);

    if (sizeX == 0.0f && sizeY == 0.0f && sizeZ == 0.0f) {
        CORRECTIONS_LOG_INFO("[ORIENT] All bounds are 0, skipping rotation correction");
        return;
    }

    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    const char* caseDesc = "Y smallest (no rotation)";

    if (sizeX <= sizeY && sizeX <= sizeZ) {
        roll = flip ? -90.0f : 90.0f;
        caseDesc = "X smallest (roll 90)";
    } else if (sizeZ <= sizeX && sizeZ <= sizeY) {
        roll = flip ? -90.0f : 90.0f;
        caseDesc = "Z smallest (roll 90)";
    } else {
        roll = flip ? -90.0f : 90.0f;
        caseDesc = "Y smallest (roll 90)";
    }

    proj->SetRotationCorrection(RE::NiPoint3(pitch, roll, yaw));
    CORRECTIONS_LOG_INFO("[ORIENT] {} -> rotationCorrection: ({:.1f}, {:.1f}, {:.1f})",
        caseDesc, pitch, roll, yaw);
}

// Internal helper: Apply form-type based rotation correction
// Returns true if a correction was applied, false if no matching type was found
inline bool ApplyFormTypeBasedRotation(ControlledProjectile* proj, RE::TESForm* form) {
    // Category-based rotation corrections (adapted from SpellWheelVR)
    // Values are in degrees: (pitch, roll, yaw) = (X-rotation, Y-rotation, Z-rotation)

    // Ammo (arrows, bolts)
    if (auto* ammo = form->As<RE::TESAmmo>()) {
        // Arrows stand upright: 90° around X
        proj->SetRotationCorrection(RE::NiPoint3(90.0f, 0.0f, 0.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Ammo -> rotationCorrection: (90, 0, 0)");
        return true;
    }

    // Scrolls
    if (form->Is(RE::FormType::Scroll)) {
        // 90° X + 180° Z
        proj->SetRotationCorrection(RE::NiPoint3(90.0f, 0.0f, 180.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Scroll -> rotationCorrection: (90, 0, 180)");
        return true;
    }

    // Weapons and Lights (torches)
    if (form->Is(RE::FormType::Weapon) || form->Is(RE::FormType::Light)) {
        // -90° around X axis
        proj->SetRotationCorrection(RE::NiPoint3(-90.0f, 0.0f, 0.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Weapon/Light -> rotationCorrection: (-90, 0, 0)");
        return true;
    }

    // Armor (shields, body armor, other)
    if (auto* armor = form->As<RE::TESObjectARMO>()) {
        // Check if it's a shield
        if (armor->IsShield()) {
            proj->SetRotationCorrection(RE::NiPoint3(-90.0f, 0.0f, 0.0f));
            CORRECTIONS_LOG_INFO("[ORIENT] Shield -> rotationCorrection: (-90, 0, 0)");
            return true;
        }

        // Check for body armor (cuirass) or head gear via bipedSlots
        auto bipedSlots = static_cast<std::uint32_t>(armor->GetSlotMask());
        bool isBody = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kBody)) != 0;
        bool isHead = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kHead)) != 0;
        bool isHair = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kHair)) != 0;

        if (isBody || isHead || isHair) {
            proj->SetRotationCorrection(RE::NiPoint3(90.0f, 0.0f, 0.0f));
            CORRECTIONS_LOG_INFO("[ORIENT] Body/Head armor -> rotationCorrection: (90, 0, 0)");
            return true;
        }

        // Other armor (gloves, boots, etc.) - no X rotation
        proj->SetRotationCorrection(RE::NiPoint3(0.0f, 0.0f, 0.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Other armor -> rotationCorrection: (0, 0, 0)");
        return true;
    }

    // Books
    if (form->Is(RE::FormType::Book)) {
        // -90° X + 180° Z
        proj->SetRotationCorrection(RE::NiPoint3(-90.0f, 0.0f, 180.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Book -> rotationCorrection: (-90, 0, 180)");
        return true;
    }

    // Potions
    if (auto* potion = form->As<RE::AlchemyItem>()) {
        if (!potion->IsFood()) {
            // Non-food potions: 90° Z only
            proj->SetRotationCorrection(RE::NiPoint3(0.0f, 0.0f, 90.0f));
            CORRECTIONS_LOG_INFO("[ORIENT] Potion (non-food) -> rotationCorrection: (0, 0, 90)");
            return true;
        }
        // Food items fall through to default/bound-based
    }

    // Ingredients
    if (form->Is(RE::FormType::Ingredient)) {
        // 90° Z only
        proj->SetRotationCorrection(RE::NiPoint3(0.0f, 0.0f, 90.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Ingredient -> rotationCorrection: (0, 0, 90)");
        return true;
    }

    // Misc items - no special rotation
    if (form->Is(RE::FormType::Misc)) {
        proj->SetRotationCorrection(RE::NiPoint3(0.0f, 0.0f, 0.0f));
        CORRECTIONS_LOG_INFO("[ORIENT] Misc -> rotationCorrection: (0, 0, 0)");
        return true;
    }

    return false;
}

inline void ApplyRotationCorrectionFor(ControlledProjectile* proj, RE::TESForm* form) {
    if (!proj) {
        CORRECTIONS_LOG_WARN("[ORIENT] ApplyRotationCorrectionFor called with null projectile");
        return;
    }
    if (!form) {
        CORRECTIONS_LOG_WARN("[ORIENT] ApplyRotationCorrectionFor called with null form");
        return;
    }

    // Armor-specific rotation fixes take precedence over bounds
    if (auto* armor = form->As<RE::TESObjectARMO>()) {
        auto bipedSlots = static_cast<std::uint32_t>(armor->GetSlotMask());
        bool isHead = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kHead)) != 0;
        bool isHair = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kHair)) != 0;
        bool isBody = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kBody)) != 0;

        if (isHead || isHair) {
            // Helmet type: rotate y -90
            proj->SetRotationCorrection(RE::NiPoint3(0.0f, 90.0f, 0.0f));
            CORRECTIONS_LOG_INFO("[ORIENT] Helmet armor -> rotationCorrection: (0, -90, 0)");
            return;
        }
        if (isBody) {
            // Body armor type: rotate y +90
            proj->SetRotationCorrection(RE::NiPoint3(0.0f, -90.0f, 0.0f));
            CORRECTIONS_LOG_INFO("[ORIENT] Body armor -> rotationCorrection: (0, 90, 0)");
            return;
        }
        // Other armor falls through to bound-based
    }

    // Fallback: Try bound-based rotation if we can cast to TESBoundObject
    if (auto* boundObj = form->As<RE::TESBoundObject>()) {
        CORRECTIONS_LOG_INFO("[ORIENT] Using bound-based rotation");
        ApplyBoundBasedRotation(proj, boundObj);
        return;
    }

    // No correction possible
    CORRECTIONS_LOG_INFO("[ORIENT] Form {:08X} is not a BoundObject, no rotation applied",
        form->GetFormID());
}

inline void ApplyScaleCorrectionFor(ControlledProjectile* proj, RE::TESBoundObject* boundObj) {
    constexpr float TARGET_SIZE = 30.0f;
    constexpr float MAX_SCALE_UP_CORRECTION = 5.0f;
    constexpr float ARMOR_FALLBACK_SCALE = 0.5f;

    if (!proj) {
        CORRECTIONS_LOG_WARN("[SCALE] ApplyScaleCorrectionFor called with null projectile");
        return;
    }
    if (!boundObj) {
        CORRECTIONS_LOG_WARN("[SCALE] ApplyScaleCorrectionFor called with null boundObj");
        return;
    }

    auto& bd = boundObj->boundData;

    // Calculate dimensions (size in each axis)
    float sizeX = static_cast<float>(bd.boundMax.x - bd.boundMin.x);
    float sizeY = static_cast<float>(bd.boundMax.y - bd.boundMin.y);
    float sizeZ = static_cast<float>(bd.boundMax.z - bd.boundMin.z);

    // If all bounds are 0, use fallback scale for body armor only
    if (sizeX == 0.0f && sizeY == 0.0f && sizeZ == 0.0f) {
        if (auto* armor = boundObj->As<RE::TESObjectARMO>()) {
            auto bipedSlots = static_cast<std::uint32_t>(armor->GetSlotMask());
            bool isBody = (bipedSlots & static_cast<std::uint32_t>(RE::BIPED_MODEL::BipedObjectSlot::kBody)) != 0;
            if (isBody) {
                proj->SetScaleCorrection(ARMOR_FALLBACK_SCALE);
                CORRECTIONS_LOG_INFO("[SCALE] Body armor with no bounds -> fallback scaleCorrection: {:.2f}", ARMOR_FALLBACK_SCALE);
                return;
            }
        }
        CORRECTIONS_LOG_INFO("[SCALE] All bounds are 0, skipping scale correction");
        return;
    }

    // Find the largest dimension
    float largestSize = sizeX;
    if (sizeY > largestSize) largestSize = sizeY;
    if (sizeZ > largestSize) largestSize = sizeZ;

    // Guard against division by zero (can happen if all dimensions are very small but non-zero)
    if (largestSize < 0.001f) {
        CORRECTIONS_LOG_INFO("[SCALE] Largest dimension too small ({:.6f}), skipping scale correction", largestSize);
        return;
    }

    // Calculate scale factor to fit within TARGET_SIZE
    float scaleFactor = TARGET_SIZE / largestSize;

    // Cap scale-up at MAX_SCALE_UP_CORRECTION
    if (scaleFactor > MAX_SCALE_UP_CORRECTION) {
        CORRECTIONS_LOG_INFO("[SCALE] Capping scale factor from {:.2f} to {:.2f}", scaleFactor, MAX_SCALE_UP_CORRECTION);
        scaleFactor = MAX_SCALE_UP_CORRECTION;
    }

    proj->SetScaleCorrection(scaleFactor);
    CORRECTIONS_LOG_INFO("[SCALE] Largest dim: {:.1f}, scaleCorrection: {:.2f} (baseScale: {:.2f})",
        largestSize, scaleFactor, proj->GetBaseScale());
}

} // namespace ProjectileCorrections
} // namespace Projectile
