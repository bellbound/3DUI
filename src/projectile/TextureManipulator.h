#pragma once

#include "TextAssets.h"

#if !defined(TEST_ENVIRONMENT)
#include "RE/Skyrim.h"
#else
#include "TestStubs.h"
#endif

#include <vector>

namespace Projectile {

// =============================================================================
// TextureManipulator
// Utility class for manipulating UV coordinates on NiAVObject nodes.
// Used for selecting characters from a texture atlas.
// =============================================================================
class TextureManipulator {
public:
    // =========================================================================
    // Atlas UV Manipulation
    // =========================================================================

    // Set UV coordinates to display a specific character from the atlas
    static bool SetCharUV(RE::NiAVObject* node, const TextAssets::UVCoord& uv);
    static bool SetCharUV(RE::NiAVObject* node, int col, int row);

    // Hide a character by setting UV scale to zero
    static bool HideCharacter(RE::NiAVObject* node);

    // Set the texture on a node's material (for effect shaders)
    static bool SetTexture(RE::NiAVObject* node, const char* texturePath);

    // =========================================================================
    // Character Node Access
    // =========================================================================

    // Get a character node by index from a projectile's 3D
    static RE::NiAVObject* GetCharNode(RE::NiAVObject* projNode, size_t charIndex);

    // Get all geometry child nodes from a projectile
    static std::vector<RE::NiAVObject*> GetAllCharNodes(RE::NiAVObject* projNode);

    // Get the container node that holds character geometries
    static RE::NiNode* GetCharacterContainer(RE::NiAVObject* projNode);

    // Clone a character node and attach it to the container
    // Returns the cloned node, or nullptr on failure
    static RE::NiAVObject* CloneCharacterNode(RE::NiAVObject* templateNode, RE::NiNode* container);

    // =========================================================================
    // Node Transform Helpers
    // =========================================================================

    // Set local X position of a node (used for character spacing)
    static void SetNodeLocalX(RE::NiAVObject* node, float x);

    // Set local Z position of a node (used for multiline vertical offset)
    static void SetNodeLocalZ(RE::NiAVObject* node, float z);

    // Hide a node by moving it far away
    static void HideNodeByPosition(RE::NiAVObject* node);

    // Show a node (reset Y position to 0)
    static void ShowNodeByPosition(RE::NiAVObject* node);

private:
    // Internal helper to set material UV offset and scale
    static bool SetMaterialUV(RE::BSGeometry* geometry,
                              float offsetX, float offsetY,
                              float scaleX, float scaleY);
};

} // namespace Projectile
