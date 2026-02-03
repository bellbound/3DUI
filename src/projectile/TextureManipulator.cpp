#include "TextureManipulator.h"
#include "AsyncTextureLoader.h"
#include "../log.h"
#include <unordered_map>
#include <string>

namespace Projectile {

// =============================================================================
// Atlas UV Manipulation
// =============================================================================

bool TextureManipulator::SetCharUV(RE::NiAVObject* node, const TextAssets::UVCoord& uv) {
    if (!node) {
        return false;
    }

    auto* geometry = node->AsGeometry();
    if (!geometry) {
        spdlog::trace("TextureManipulator::SetCharUV - Node is not BSGeometry");
        return false;
    }

    // Calculate UV using direct division to minimize float precision issues
    // Using double for intermediate calculation, then cast to float at the end
    const double cols = static_cast<double>(TextAssets::GetAtlasCols());
    const double rows = static_cast<double>(TextAssets::GetAtlasRows());

    // Scale factor - halved because mesh UVs span 2x the cell size (range: -0.5 to 1.5)
    float scaleX = static_cast<float>(1.0 / cols * 0.5);
    float scaleY = static_cast<float>(1.0 / rows * 0.5);

    // Offset to cell position (calculated with double precision)
    // Add 0.5 * scale to compensate for mesh UVs starting at -0.5 instead of 0
    // finalUV = meshUV * scale + offset
    // At meshUV = -0.5: we want finalUV = col/cols, so offset = col/cols + 0.5*scale
    float offsetX = static_cast<float>(static_cast<double>(uv.col) / cols) + 0.5f * scaleX;
    float offsetY = static_cast<float>(static_cast<double>(uv.row) / rows) + 0.5f * scaleY;

    return SetMaterialUV(geometry, offsetX, offsetY, scaleX, scaleY);
}

bool TextureManipulator::SetCharUV(RE::NiAVObject* node, int col, int row) {
    TextAssets::UVCoord uv(col, row);
    return SetCharUV(node, uv);
}

bool TextureManipulator::HideCharacter(RE::NiAVObject* node) {
    if (!node) {
        return false;
    }

    auto* geometry = node->AsGeometry();
    if (!geometry) {
        return false;
    }

    // Set UV scale to zero to hide the character
    return SetMaterialUV(geometry, 0.0f, 0.0f, 0.0f, 0.0f);
}

// Legacy cache - kept for backwards compatibility and synchronous texture loading
// New code should use AsyncTextureLoader for non-blocking loads
static std::unordered_map<std::string, RE::NiPointer<RE::NiTexture>> s_textureCache;

bool TextureManipulator::SetTexture(RE::NiAVObject* node, const char* texturePath) {

    if (!node || !texturePath) {
        spdlog::error("TextureManipulator::SetTexture - null node or texturePath");
        return false;
    }

    auto* geometry = node->AsGeometry();
    if (!geometry) {
        spdlog::error("TextureManipulator::SetTexture - node is not geometry");
        return false;
    }

    auto* effectState = geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
    if (!effectState) {
        spdlog::error("TextureManipulator::SetTexture - no effect state on geometry");
        return false;
    }

    auto* shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effectState);
    if (!shaderProperty) {
        spdlog::error("TextureManipulator::SetTexture - effectState is not BSShaderProperty");
        return false;
    }

    auto* material = shaderProperty->material;
    if (!material) {
        spdlog::error("TextureManipulator::SetTexture - shaderProperty has no material");
        return false;
    }

    // Check if this is an effect shader material
    if (material->GetType() != RE::BSShaderMaterial::Type::kEffect) {
        spdlog::error("TextureManipulator::SetTexture - Material type is NOT kEffect (type={}), cannot set texture",
            static_cast<int>(material->GetType()));
        return false;
    }

    auto* effectMaterial = static_cast<RE::BSEffectShaderMaterial*>(material);
    std::string pathKey(texturePath);

    // Use AsyncTextureLoader for non-blocking texture loading
    // The loader returns immediately with either:
    // - Cached texture (instant)
    // - Placeholder texture (while async load happens in background)
    auto& asyncLoader = AsyncTextureLoader::GetInstance();

    // Check if already fully loaded in async cache
    RE::NiPointer<RE::NiTexture> texture = asyncLoader.GetCachedTexture(pathKey);

    if (!texture) {
        // Not in async cache - request async load with callback for texture swap
        // Capture necessary pointers for the callback (weak references would be safer in production)
        texture = asyncLoader.RequestTexture(pathKey,
            [geometry, shaderProperty, effectMaterial, pathKey](RE::NiPointer<RE::NiTexture> loadedTexture) {
                // Callback fires on main thread when texture load completes
                if (!loadedTexture) {
                    spdlog::error("TextureManipulator: Async load FAILED for '{}'", pathKey);
                    return;
                }

                // Swap placeholder for real texture
                effectMaterial->sourceTexture.reset(static_cast<RE::NiSourceTexture*>(loadedTexture.get()));

                // Rebind to renderer
                shaderProperty->SetMaterial(effectMaterial, true);
                shaderProperty->SetupGeometry(geometry);
                shaderProperty->FinishSetupGeometry(geometry);

                spdlog::trace("TextureManipulator: Async texture swap complete for '{}'", pathKey);
            });

        if (!texture) {
            spdlog::error("TextureManipulator::SetTexture - FAILED to get texture or placeholder for '{}'", texturePath);
            return false;
        }
    }

    // Apply texture immediately (either cached or placeholder)
    effectMaterial->sourceTexture.reset(static_cast<RE::NiSourceTexture*>(texture.get()));
    effectMaterial->sourceTexturePath = texturePath;

    // Reset UV to show full texture (offset=0, scale=1)
    // This ensures icons display correctly without atlas UV artifacts.
    // For text, SetCharUV() is called afterwards to set character-specific UVs.
    effectMaterial->texCoordOffset[0] = {0.25f, 0.25f};
    effectMaterial->texCoordOffset[1] = {0.25f, 0.25f};
    effectMaterial->texCoordScale[0] = {0.5f, 0.5f};
    effectMaterial->texCoordScale[1] = {0.5f, 0.5f};

    // CRITICAL: Re-set material and call SetupGeometry to bind texture to render pipeline
    // SpellWheelVR shows this is required for texture changes to take effect
    // The second parameter 'true' is important - it triggers proper material setup
    shaderProperty->SetMaterial(effectMaterial, true);

    // SetupGeometry and FinishSetupGeometry bind the material/texture to the renderer
    // Without these calls, the material's texture changes are not reflected visually
    shaderProperty->SetupGeometry(geometry);
    shaderProperty->FinishSetupGeometry(geometry);

    return true;
}

bool TextureManipulator::SetMaterialUV(RE::BSGeometry* geometry,
                                        float offsetX, float offsetY,
                                        float scaleX, float scaleY) {
    if (!geometry) {
        return false;
    }

    auto* effectState = geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
    if (!effectState) {
        spdlog::trace("TextureManipulator::SetMaterialUV - No effect state");
        return false;
    }

    auto* shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effectState);
    if (!shaderProperty) {
        spdlog::trace("TextureManipulator::SetMaterialUV - Not a shader property");
        return false;
    }

    auto* material = shaderProperty->material;
    if (!material) {
        spdlog::trace("TextureManipulator::SetMaterialUV - No material");
        return false;
    }

    // BSShaderMaterial base class has texCoordOffset/Scale - works for both
    // BSLightingShaderMaterial and BSEffectShaderMaterial

    // Set UV offset (both texture coordinate sets)
    material->texCoordOffset[0].x = offsetX;
    material->texCoordOffset[0].y = offsetY;
    material->texCoordOffset[1].x = offsetX;
    material->texCoordOffset[1].y = offsetY;

    // Set UV scale
    material->texCoordScale[0].x = scaleX;
    material->texCoordScale[0].y = scaleY;
    material->texCoordScale[1].x = scaleX;
    material->texCoordScale[1].y = scaleY;

    return true;
}

// =============================================================================
// Character Node Access
// =============================================================================

// Helper to find the node containing character geometries
// Traverses: BSFadeNode → SentenceNode → NiBillboardNode
RE::NiNode* TextureManipulator::GetCharacterContainer(RE::NiAVObject* projNode) {
    if (!projNode) {
        return nullptr;
    }

    auto* rootNode = projNode->AsNode();
    if (!rootNode) {
        return nullptr;
    }

    // Look for SentenceNode or NiBillboardNode containing char geometries
    for (auto& child : rootNode->GetChildren()) {
        if (!child) continue;

        auto* childNode = child->AsNode();
        if (!childNode) continue;

        // Check if this node's children are geometries (direct container)
        for (auto& grandchild : childNode->GetChildren()) {
            if (grandchild && grandchild->AsGeometry()) {
                spdlog::trace("FindCharacterContainerNode - Found container at level 1: {}",
                    childNode->name.c_str());
                return childNode;
            }
        }

        // Check one level deeper (SentenceNode → NiBillboardNode → CharN)
        for (auto& grandchild : childNode->GetChildren()) {
            if (!grandchild) continue;

            auto* grandchildNode = grandchild->AsNode();
            if (!grandchildNode) continue;

            for (auto& greatGrandchild : grandchildNode->GetChildren()) {
                if (greatGrandchild && greatGrandchild->AsGeometry()) {
                    spdlog::trace("FindCharacterContainerNode - Found container at level 2: {}",
                        grandchildNode->name.c_str());
                    return grandchildNode;
                }
            }
        }
    }

    spdlog::warn("FindCharacterContainerNode - No character container found in hierarchy");
    return nullptr;
}

RE::NiAVObject* TextureManipulator::GetCharNode(RE::NiAVObject* projNode, size_t charIndex) {
    auto* containerNode = GetCharacterContainer(projNode);
    if (!containerNode) {
        return nullptr;
    }

    auto& children = containerNode->GetChildren();
    if (charIndex < children.size()) {
        auto* child = children[charIndex].get();
        if (child && child->AsGeometry()) {
            return child;
        }
    }

    return nullptr;
}

std::vector<RE::NiAVObject*> TextureManipulator::GetAllCharNodes(RE::NiAVObject* projNode) {
    std::vector<RE::NiAVObject*> nodes;

    auto* containerNode = GetCharacterContainer(projNode);
    if (!containerNode) {
        return nodes;
    }

    auto& children = containerNode->GetChildren();
    nodes.reserve(children.size());
    for (std::uint32_t i = 0; i < children.size(); ++i) {
        auto* child = children[i].get();
        if (child && child->AsGeometry()) {
            nodes.push_back(child);
        }
    }

    spdlog::trace("TextureManipulator::GetAllCharNodes - Found {} geometry children in '{}'",
        nodes.size(), containerNode->name.c_str());
    return nodes;
}

RE::NiAVObject* TextureManipulator::CloneCharacterNode(RE::NiAVObject* templateNode, RE::NiNode* container) {
    if (!templateNode || !container) {
        spdlog::warn("TextureManipulator::CloneCharacterNode - null template or container");
        return nullptr;
    }

    // Clone the node using simple Clone()
    // Note: Material cloning is handled by SetTexture() which is always called after cloning
    auto* clonedObj = templateNode->Clone();
    if (!clonedObj) {
        spdlog::error("TextureManipulator::CloneCharacterNode - Clone() returned nullptr");
        return nullptr;
    }

    // Attach the cloned node to the container
    container->AttachChild(clonedObj, false);

    spdlog::trace("TextureManipulator::CloneCharacterNode - Cloned node, container now has {} children",
        container->GetChildren().size());

    return clonedObj;
}

// =============================================================================
// Node Transform Helpers
// =============================================================================

void TextureManipulator::SetNodeLocalX(RE::NiAVObject* node, float x) {
    if (node) {
        node->local.translate.x = x;
    }
}

void TextureManipulator::SetNodeLocalZ(RE::NiAVObject* node, float z) {
    if (node) {
        node->local.translate.z = z;
    }
}

void TextureManipulator::HideNodeByPosition(RE::NiAVObject* node) {
    if (node) {
        // Move far away on Y axis to hide
        node->local.translate.y = -10000.0f;
    }
}

void TextureManipulator::ShowNodeByPosition(RE::NiAVObject* node) {
    if (node) {
        node->local.translate.y = 0.0f;
    }
}

} // namespace Projectile
