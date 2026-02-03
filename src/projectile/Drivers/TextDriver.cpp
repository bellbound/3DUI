#include "TextDriver.h"
#include "../TextureManipulator.h"
#include "../AsyncTextureLoader.h"
#include "../ProjectileSubsystem.h"
#include "../InteractionController.h"
#include "../../log.h"

#undef min
#undef max

#include <algorithm>
#include <codecvt>
#include <locale>

namespace Projectile {

// =============================================================================
// Construction
// =============================================================================

TextDriver::TextDriver() {
    SetTransitionMode(TransitionMode::Instant);
}

TextDriver::~TextDriver() {
    CleanupClonedNodes();
}

// =============================================================================
// Text Content
// =============================================================================

void TextDriver::SetText(const std::wstring& text) {
    if (m_text == text) {
        return;
    }

    m_text = text;
    MarkDirty();
    m_boundsValid = false;
    m_uvsApplied = false;

    std::string narrowText;
    narrowText.reserve(text.length());
    for (wchar_t ch : text) {
        narrowText.push_back(static_cast<char>(ch <= 127 ? ch : '?'));
    }
    spdlog::trace("TextDriver::SetText - '{}' ({} chars)", narrowText, text.length());
}

void TextDriver::SetText(const std::string& text) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    SetText(converter.from_bytes(text));
}

// =============================================================================
// Appearance Settings
// =============================================================================

void TextDriver::SetTextScale(float scale) {
    if (m_textScale != scale) {
        m_textScale = scale;
        MarkDirty();
        m_boundsValid = false;
    }
}

void TextDriver::SetAlignment(TextAlignment align) {
    if (m_alignment != align) {
        m_alignment = align;
        MarkDirty();
        m_boundsValid = false;
    }
}


// =============================================================================
// Bounds Calculation
// =============================================================================

TextBounds TextDriver::GetBounds() const {
    if (m_boundsValid) {
        return m_cachedBounds;
    }

    m_cachedBounds = TextBounds{};

    if (m_layout.empty()) {
        m_boundsValid = true;
        return m_cachedBounds;
    }

    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    bool foundFirst = false;

    float letterDistance = GetLetterDistance();
    float charHeight = GetCharacterHeight();

    for (const auto& charLayout : m_layout) {
        if (!charLayout.visible) {
            continue;
        }

        // Use per-character width from CSV (xOffset is left edge)
        float charWidth = letterDistance * charLayout.widthRatio;
        float charLeft = charLayout.xOffset;
        float charRight = charLayout.xOffset + charWidth;

        // Y bounds: character's yOffset plus half char height above/below
        float charTop = charLayout.yOffset + charHeight * 0.5f;
        float charBottom = charLayout.yOffset - charHeight * 0.5f;

        if (!foundFirst) {
            minX = charLeft;
            maxX = charRight;
            minY = charBottom;
            maxY = charTop;
            foundFirst = true;
        } else {
            minX = std::min(minX, charLeft);
            maxX = std::max(maxX, charRight);
            minY = std::min(minY, charBottom);
            maxY = std::max(maxY, charTop);
        }
    }

    if (foundFirst) {
        m_cachedBounds.minX = minX;
        m_cachedBounds.maxX = maxX;
        m_cachedBounds.width = maxX - minX;
        m_cachedBounds.minY = minY;
        m_cachedBounds.maxY = maxY;
        m_cachedBounds.height = maxY - minY;
    }

    m_boundsValid = true;
    return m_cachedBounds;
}

float TextDriver::GetCharacterHeight() const {
    // Returns unscaled height - projectile's baseScale handles uniform scaling
    constexpr float BASE_HEIGHT = 100.0f;
    return BASE_HEIGHT;
}

float TextDriver::GetLetterDistance() const {
    return TextAssets::BASE_LETTER_DISTANCE;
}

// =============================================================================
// Driver Overrides
// =============================================================================

void TextDriver::Initialize() {
    ProjectileDriver::Initialize();
    spdlog::trace("TextDriver::Initialize");
}

void TextDriver::SetVisible(bool visible) {
    // Clean up cloned nodes when hiding - this is safer than cleaning on show
    // because the renderer will have stopped referencing them by next frame
    if (!visible && !m_clonedNodes.empty()) {
        CleanupClonedNodes();
        m_dirty = true;  // Force re-clone when shown again
    }

    // Base class propagates visibility to children (including m_textProjectile)
    ProjectileDriver::SetVisible(visible);

    if (visible && !m_textProjectile && !m_text.empty()) {
        EnsureProjectile();
    }
}

void TextDriver::Clear() {
    // Clean up cloned nodes before clearing
    CleanupClonedNodes();

    // Reset cached pointer - base class will destroy the actual projectile
    m_textProjectile = nullptr;

    // Call base to clear all children
    ProjectileDriver::Clear();
}

void TextDriver::OnParentHide() {
    // The game projectile's 3D nodes will be destroyed when it unbinds.
    // Mark dirty so UpdateCharacterNodes() recreates clones when shown again,
    // even if SetText() is called with the same text (which early-returns).
    if (!m_clonedNodes.empty()) {
        CleanupClonedNodes();
    }
    m_dirty = true;
    m_uvsApplied = false;

    // Let base class propagate to children (m_textProjectile will unbind)
    ProjectileDriver::OnParentHide();
}

void TextDriver::UpdateLayout(float deltaTime) {
    if (!IsVisible()) {
        return;
    }

    EnsureProjectile();

    if (m_dirty || !m_uvsApplied) {
        // Defer character setup until texture is fully loaded
        // This avoids race conditions with async texture callbacks
        auto& asyncLoader = AsyncTextureLoader::GetInstance();
        if (!asyncLoader.IsTextureReady(TextAssets::TEXT_ATLAS_PATH)) {
            // Request preload if not already loading, then wait for next frame
            asyncLoader.RequestTexture(TextAssets::TEXT_ATLAS_PATH, nullptr);
            spdlog::trace("TextDriver::UpdateLayout - Waiting for texture '{}' to load",
                TextAssets::TEXT_ATLAS_PATH);
            return;
        }

        bool success = UpdateCharacterNodes();
        if (success) {
            m_dirty = false;
            m_uvsApplied = true;
        }
    }

    // Position the text projectile at our center
    // Note: Update() is called by base class since m_textProjectile is in m_children
    if (m_textProjectile) {
        m_textProjectile->SetLocalPosition(RE::NiPoint3(0, 0, 0));
    }
}

// =============================================================================
// Internal Implementation
// =============================================================================

void TextDriver::EnsureProjectile() {
    if (m_textProjectile || !GetSubsystem()) {
        return;
    }

    spdlog::info("TextDriver::EnsureProjectile - Spawning with mesh '{}'", TextAssets::TEXT_MESH_PATH);

    // Create projectile directly with configuration
    auto proj = std::make_shared<ControlledProjectile>();
    proj->SetModelPath(TextAssets::TEXT_MESH_PATH);
    proj->SetBaseScale(m_textScale * 1.5f);
    proj->SetBillboardMode(BillboardMode::FaceHMD);
    // Use driver's stored settings (defaults to Instant, can be overridden via SetTransitionMode)
    proj->SetTransitionMode(m_transitionMode);
    proj->SetSmoothingSpeed(m_smoothingSpeed);

    // Text elements are non-interactive by default (display-only)
    // No hover scale animation, no haptic feedback
    proj->SetActivateable(false);
    proj->SetUseHapticFeedback(false);

    m_textProjectile = proj.get();  // Cache raw pointer before AddChild moves ownership
    AddChild(proj);  // Adds to m_children, sets parent, handles visibility propagation
    spdlog::info("TextDriver::EnsureProjectile - Success");
    MarkDirty();
}

std::vector<TextDriver::CharOffset> TextDriver::ComputeCharacterOffsets() const {
    std::vector<CharOffset> offsets;
    if (m_text.empty()) {
        return offsets;
    }

    float letterDistance = GetLetterDistance();
    float lineHeight = GetCharacterHeight() * m_lineSpacing;

    // Split text into lines by \n
    std::vector<std::wstring> lines;
    std::wstring currentLine;
    for (wchar_t ch : m_text) {
        if (ch == L'\n') {
            lines.push_back(currentLine);
            currentLine.clear();
        } else {
            currentLine += ch;
        }
    }
    lines.push_back(currentLine);  // Don't forget the last line

    // Process each line
    int lineIndex = 0;
    for (const auto& line : lines) {
        float yOffset = -lineIndex * lineHeight;  // Negative because subsequent lines go down

        // Compute x offsets for this line (same logic as before, but per-line)
        std::vector<float> rawPositions;
        float currentX = 0.0f;
        float firstPosX = 0.0f;
        float lastPosX = 0.0f;
        bool foundFirst = false;

        for (wchar_t ch : line) {
            float widthRatio = TextAssets::GetWidthRatio(ch);

            // Spaces/tabs add to position but don't render
            if (ch == L' ' || ch == L'\t' || ch == L'\u00A0') {
                currentX += letterDistance * (widthRatio + TextAssets::CHAR_GAP);
                continue;
            }

            float posX = currentX;
            rawPositions.push_back(posX);

            if (!foundFirst) {
                firstPosX = posX;
                foundFirst = true;
            }
            lastPosX = posX;

            currentX += letterDistance * (widthRatio + TextAssets::CHAR_GAP);
        }

        // Apply alignment for this line
        float alignOffset = 0.0f;
        if (foundFirst) {
            float centerOffset = (firstPosX + lastPosX) / -2.0f;
            float totalWidth = lastPosX - firstPosX;
            switch (m_alignment) {
                case TextAlignment::Left:
                    alignOffset = totalWidth / 2.0f + centerOffset;
                    break;
                case TextAlignment::Right:
                    alignOffset = -totalWidth / 2.0f + centerOffset;
                    break;
                case TextAlignment::Center:
                default:
                    alignOffset = centerOffset;
                    break;
            }
        }

        // Add offsets for this line's characters
        for (float pos : rawPositions) {
            offsets.push_back({pos + alignOffset, yOffset});
        }

        ++lineIndex;
    }

    return offsets;
}

void TextDriver::CleanupClonedNodes() {
    if (m_clonedNodes.empty()) {
        return;
    }

    size_t nodeCount = m_clonedNodes.size();

    // Validate projectile exists before touching any nodes
    // The game's IO thread may be accessing these nodes - if projectile is invalid,
    // the nodes may already be freed or in an inconsistent state
    if (!m_textProjectile) {
        spdlog::info("CleanupClonedNodes - No textProjectile, clearing {} node references", nodeCount);
        m_clonedNodes.clear();
        return;
    }

    auto& gameProj = m_textProjectile->GetGameProjectile();

    // CRITICAL: Validate projectile still exists in game world via refHandle lookup.
    // IsBound() only checks pointer != nullptr, but the game may have destroyed the
    // projectile, leaving us with a dangling pointer. IsProjectileValid() verifies
    // the refHandle still resolves to our projectile before we access it.
    if (!gameProj.IsProjectileValid()) {
        spdlog::info("CleanupClonedNodes - Projectile invalid or destroyed by game, clearing {} node references without manipulation", nodeCount);
        m_clonedNodes.clear();
        return;
    }

    auto* proj = gameProj.GetProjectile();
    auto* projNode = proj->Get3D();
    if (!projNode) {
        spdlog::info("CleanupClonedNodes - Get3D() null, clearing {} node references", nodeCount);
        m_clonedNodes.clear();
        return;
    }

    // Projectile is valid - safe to manipulate nodes
    // First hide all cloned nodes (detaching alone may not stop rendering immediately)
    for (auto& clonedNode : m_clonedNodes) {
        if (clonedNode) {
            TextureManipulator::HideNodeByPosition(clonedNode.get());
            TextureManipulator::HideCharacter(clonedNode.get());
        }
    }

    // Then detach from container
    // NiPointer ensures nodes stay alive until we clear() below
    auto* container = TextureManipulator::GetCharacterContainer(projNode);
    if (container) {
        for (auto& clonedNode : m_clonedNodes) {
            if (clonedNode) {
                container->DetachChild2(clonedNode.get());
            }
        }
    }

    spdlog::info("CleanupClonedNodes - Cleaned up {} cloned nodes", nodeCount);
    // Nodes are destroyed here when NiPointer releases its reference
    m_clonedNodes.clear();
}

bool TextDriver::UpdateCharacterNodes() {
    m_layout.clear();
    m_boundsValid = false;

    if (!m_textProjectile) {
        CleanupClonedNodes();
        return true;
    }

    // Hide all character nodes (including template) when text is empty
    if (m_text.empty()) {
        CleanupClonedNodes();
        auto& gameProj = m_textProjectile->GetGameProjectile();
        if (gameProj.IsProjectileValid()) {
            if (auto* proj = gameProj.GetProjectile()) {
                if (auto* projNode = proj->Get3D()) {
                    auto charNodes = TextureManipulator::GetAllCharNodes(projNode);
                    for (auto* node : charNodes) {
                        TextureManipulator::HideNodeByPosition(node);
                        TextureManipulator::HideCharacter(node);
                    }
                }
            }
        }
        return true;
    }

    auto& gameProj = m_textProjectile->GetGameProjectile();
    if (!gameProj.IsProjectileValid()) {
        spdlog::trace("TextDriver::UpdateCharacterNodes - Projectile no longer valid");
        return false;
    }
    auto* proj = gameProj.GetProjectile();
    if (!proj) {
        spdlog::trace("TextDriver::UpdateCharacterNodes - No projectile yet");
        return false;
    }

    auto* projNode = proj->Get3D();
    if (!projNode) {
        static int s_noNodeCount = 0;
        if (++s_noNodeCount <= 3 || s_noNodeCount % 60 == 0) {
            spdlog::warn("TextDriver::UpdateCharacterNodes - No 3D node (attempt {}). "
                "Mesh may not exist: {}", s_noNodeCount, TextAssets::TEXT_MESH_PATH);
        }
        return false;
    }

    // Get the container node for attaching clones
    auto* container = TextureManipulator::GetCharacterContainer(projNode);
    if (!container) {
        spdlog::warn("TextDriver::UpdateCharacterNodes - No character container found");
        return false;
    }


    // Get existing character nodes
    auto charNodes = TextureManipulator::GetAllCharNodes(projNode);
    if (charNodes.empty()) {
        spdlog::warn("TextDriver::UpdateCharacterNodes - No geometry children in mesh");
        return false;
    }

    // Store original node count on first call (before any cloning)
    if (m_originalNodeCount == 0) {
        m_originalNodeCount = charNodes.size();
        spdlog::info("TextDriver::UpdateCharacterNodes - Original node count: {}", m_originalNodeCount);
    }

    // Count how many visible characters we need (excluding whitespace and newlines)
    size_t visibleCharCount = 0;
    for (wchar_t ch : m_text) {
        if (ch != L' ' && ch != L'\t' && ch != L'\u00A0' && ch != L'\n') {
            ++visibleCharCount;
        }
    }

    // Clean up old clones first if text changed
    CleanupClonedNodes();

    // Re-get nodes after cleanup (in case cloned nodes were removed)
    charNodes = TextureManipulator::GetAllCharNodes(projNode);

    // Use the first node as template for cloning
    RE::NiAVObject* templateNode = charNodes.empty() ? nullptr : charNodes[0];
    if (!templateNode) {
        spdlog::error("TextDriver::UpdateCharacterNodes - No template node available");
        return false;
    }

    // Clone additional nodes if we need more than we have
    size_t currentNodeCount = charNodes.size();
    if (visibleCharCount > currentNodeCount) {
        size_t needToClone = visibleCharCount - currentNodeCount;
        spdlog::info("TextDriver::UpdateCharacterNodes - Cloning {} additional nodes (have {}, need {})",
            needToClone, currentNodeCount, visibleCharCount);

        for (size_t i = 0; i < needToClone; ++i) {
            auto* clonedNode = TextureManipulator::CloneCharacterNode(templateNode, container);
            if (clonedNode) {
                m_clonedNodes.push_back(RE::NiPointer<RE::NiAVObject>(clonedNode));
                charNodes.push_back(clonedNode);
            } else {
                spdlog::error("TextDriver::UpdateCharacterNodes - Failed to clone node {}", i);
                break;
            }
        }
    }

    std::vector<CharOffset> offsets = ComputeCharacterOffsets();

    // Hide all nodes and set texture on each (each node has its own material)
    for (auto* node : charNodes) {
        TextureManipulator::HideNodeByPosition(node);
        TextureManipulator::HideCharacter(node);
    }

    // Set the font atlas texture on all nodes
    // TextureManipulator::SetTexture caches the loaded texture internally
    for (auto* node : charNodes) {
        TextureManipulator::SetTexture(node, TextAssets::TEXT_ATLAS_PATH);
    }

    size_t nodeIndex = 0;
    size_t offsetIndex = 0;

    for (size_t i = 0; i < m_text.length() && nodeIndex < charNodes.size(); ++i) {
        wchar_t ch = m_text[i];

        // Skip whitespace and newlines (they don't get rendered)
        if (ch == L' ' || ch == L'\t' || ch == L'\u00A0' || ch == L'\n') {
            continue;
        }

        CharacterLayout layout;
        layout.ch = ch;
        layout.widthRatio = TextAssets::GetWidthRatio(ch);
        layout.visible = false;
        layout.xOffset = 0.0f;
        layout.yOffset = 0.0f;

        const TextAssets::UVCoord* uvCoord = TextAssets::GetCharUV(ch);
        if (uvCoord) {
            layout.uv = *uvCoord;
            layout.visible = true;
        } else {
            spdlog::trace("TextDriver::UpdateCharacterNodes - Char '{}' (U+{:04X}) not in atlas",
                static_cast<char>(ch <= 127 ? ch : '?'), static_cast<int>(ch));
        }

        if (offsetIndex < offsets.size()) {
            layout.xOffset = offsets[offsetIndex].x;
            layout.yOffset = offsets[offsetIndex].y;
            ++offsetIndex;
        }

        auto* node = charNodes[nodeIndex];
        if (node && layout.visible) {
            TextureManipulator::ShowNodeByPosition(node);
            TextureManipulator::SetNodeLocalX(node, layout.xOffset);
            TextureManipulator::SetNodeLocalZ(node, layout.yOffset);
            TextureManipulator::SetCharUV(node, layout.uv);
        }

        m_layout.push_back(layout);
        ++nodeIndex;
    }

    spdlog::trace("TextDriver::UpdateCharacterNodes - Set up {} characters ({} cloned nodes)",
        m_layout.size(), m_clonedNodes.size());
    return true;
}

} // namespace Projectile
