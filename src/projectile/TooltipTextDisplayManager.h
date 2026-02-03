#pragma once

#include "../projectile/Drivers/TextDriver.h"
#include "../projectile/Drivers/RootDriver.h"
#include <memory>
#include <string>

namespace Widget {

// =============================================================================
// TooltipTextDisplayManager
// Singleton that manages tooltip text display on the back of VR hands.
// Shows the hovered item's text when hovering over interactive elements.
// Lazy-initialized on first ShowTooltip() call.
//
// Usage:
//   auto* tooltip = TooltipTextDisplayManager::GetSingleton();
//   tooltip->ShowTooltip(isLeft, L"Item Name");  // Called by InteractionController
//   tooltip->HideTooltip(isLeft, L"Item Name");  // Only hides if text matches
// =============================================================================
class TooltipTextDisplayManager {
public:
    static TooltipTextDisplayManager* GetSingleton();

    // Shutdown and release resources
    void Shutdown();

    // === Tooltip Display ===

    // Show tooltip text for a specific hand
    // Called when hovering over an item
    // id: unique identifier for matching (prevents race conditions)
    // text: display text to show
    void ShowTooltip(bool isLeft, const std::string& id, const std::wstring& text);
    void ShowTooltip(bool isLeft, const std::string& id, const std::string& text);

    // Hide tooltip for a specific hand, but only if the id matches
    // This prevents race conditions where a new hover hides an old tooltip
    void HideTooltip(bool isLeft, const std::string& id);

    // Force hide tooltip regardless of text
    void ForceHideTooltip(bool isLeft);

    // Hide all tooltips
    void HideAll();

    // === Configuration ===

    // Offset from hand position (in hand-local space)
    // Default: slightly above and behind the hand (back of hand)
    void SetOffset(const RE::NiPoint3& offset) { m_offset = offset; }
    RE::NiPoint3 GetOffset() const { return m_offset; }

    // Rotation offset (Euler angles) to tilt the text
    // Default: tilted upward so it's readable
    void SetRotationOffset(const RE::NiPoint3& rot) { m_rotationOffset = rot; }
    RE::NiPoint3 GetRotationOffset() const { return m_rotationOffset; }

    // Text scale
    void SetTextScale(float scale);
    float GetTextScale() const { return m_textScale; }

    // Distance to move tooltip towards player HMD (0 = at hand+offset, higher = closer to player)
    void SetTowardsPlayerDistance(float distance) { m_towardsPlayerDistance = distance; }
    float GetTowardsPlayerDistance() const { return m_towardsPlayerDistance; }

    // === State Query ===

    bool IsTooltipVisible(bool isLeft) const;
    const std::wstring& GetCurrentText(bool isLeft) const;

    // === Update (called each frame) ===
    void Update(float deltaTime);

private:
    TooltipTextDisplayManager() = default;
    ~TooltipTextDisplayManager() = default;
    TooltipTextDisplayManager(const TooltipTextDisplayManager&) = delete;
    TooltipTextDisplayManager& operator=(const TooltipTextDisplayManager&) = delete;

    // Per-hand tooltip state
    struct HandTooltipState {
        std::unique_ptr<Projectile::RootDriver> root;
        Projectile::TextDriver* textDriver = nullptr;  // Owned by root
        std::string currentId;      // Unique id for matching (prevents race conditions)
        std::wstring currentText;   // Display text
        bool visible = false;

        void Clear() {
            if (root) {
                root->SetVisible(false);
            }
            currentId.clear();
            currentText.clear();
            visible = false;
        }
    };

    // Lazy initialization - called on first ShowTooltip
    void Initialize();

    // Update hand tooltip position each frame
    void UpdateHandTooltip(bool isLeft, float deltaTime);

    bool m_initialized = false;

    HandTooltipState m_leftHand;
    HandTooltipState m_rightHand;

    // Configuration
    RE::NiPoint3 m_offset{5.0f, 0.0f,8.0f};        // Back of hand, slightly up
    RE::NiPoint3 m_rotationOffset{-0.4f, 0.0f, 0.0f}; // Tilt upward (~23 degrees)
    float m_textScale = 0.9f;
    float m_towardsPlayerDistance = 2.0f;  // Move tooltip towards HMD by this amount

    // Helper to get hand state
    HandTooltipState& GetHandState(bool isLeft) { return isLeft ? m_leftHand : m_rightHand; }
    const HandTooltipState& GetHandState(bool isLeft) const { return isLeft ? m_leftHand : m_rightHand; }
};

} // namespace Widget
