#pragma once

#include "../ProjectileDriver.h"
#include "../TextAssets.h"
#include <string>
#include <vector>

namespace Projectile {

// =============================================================================
// TextBounds
// Represents the bounding box of rendered text in driver local space
// =============================================================================
struct TextBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    RE::NiPoint3 GetCenter() const {
        return RE::NiPoint3((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, 0.0f);
    }

    bool IsEmpty() const { return width <= 0.0f; }
};

// =============================================================================
// TextAlignment
// =============================================================================
enum class TextAlignment {
    Left,
    Center,
    Right
};

// =============================================================================
// TextDriver
// Renders text using a projectile with multiple character geometry nodes.
// Each character's UV is manipulated to display the correct glyph from
// the texture atlas.
// =============================================================================
class TextDriver : public ProjectileDriver {
public:
    TextDriver();
    ~TextDriver() override;

    // =========================================================================
    // Text Content
    // =========================================================================

    void SetText(const std::wstring& text);
    const std::wstring& GetText() const { return m_text; }

    void SetText(const std::string& text);

    // =========================================================================
    // Appearance Settings
    // =========================================================================

    void SetTextScale(float scale);
    float GetTextScale() const { return m_textScale; }

    void SetAlignment(TextAlignment align);
    TextAlignment GetAlignment() const { return m_alignment; }

    void SetLineSpacing(float spacing) { m_lineSpacing = spacing; MarkDirty(); }
    float GetLineSpacing() const { return m_lineSpacing; }

    // =========================================================================
    // Bounds
    // =========================================================================

    TextBounds GetBounds() const;
    float GetCharacterHeight() const;
    float GetLetterDistance() const;

    // =========================================================================
    // ProjectileDriver Overrides
    // =========================================================================

    void Initialize() override;
    void SetVisible(bool visible) override;
    void Clear() override;
    void OnParentHide() override;

protected:
    void UpdateLayout(float deltaTime) override;

private:
    struct CharOffset {
        float x;
        float y;
    };

    void EnsureProjectile();
    std::vector<CharOffset> ComputeCharacterOffsets() const;
    bool UpdateCharacterNodes();
    void CleanupClonedNodes();
    void MarkDirty() { m_dirty = true; }

    std::wstring m_text;

    float m_textScale = 1.0f;
    float m_lineSpacing = 1.2f;  // Multiplier of character height between lines
    TextAlignment m_alignment = TextAlignment::Center;

    struct CharacterLayout {
        wchar_t ch;
        float xOffset;
        float yOffset;
        float widthRatio;
        TextAssets::UVCoord uv;
        bool visible;
    };
    std::vector<CharacterLayout> m_layout;

    mutable TextBounds m_cachedBounds;
    mutable bool m_boundsValid = false;

    bool m_dirty = true;
    bool m_uvsApplied = false;

    ControlledProjectile* m_textProjectile = nullptr;  // Owned by m_children, cached for direct access

    // Track cloned character nodes for cleanup
    // We keep the first node as template and clone additional nodes as needed
    // Using NiPointer to prevent premature destruction while we hold references
    std::vector<RE::NiPointer<RE::NiAVObject>> m_clonedNodes;
    size_t m_originalNodeCount = 0;  // Number of nodes that were in the NIF originally
};

} // namespace Projectile
