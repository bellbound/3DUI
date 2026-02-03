#include "Anchor.h"
#include "IPositionable.h"  // For MultiplyMatrices
#include "../log.h"

namespace Projectile {

void Anchor::SetDirect(RE::NiAVObject* node) {
    if(node == m_directNode)
        return;
    m_directNode = node;
    m_refHandle = RE::ObjectRefHandle();
    m_nodeName.clear();

    if (node) {
        spdlog::trace("Anchor: Set direct node {:p}", (void*)node);
    }
}

void Anchor::SetByHandle(RE::ObjectRefHandle handle, std::string_view nodeName) {
    m_directNode = nullptr;
    m_refHandle = handle;
    m_nodeName = std::string(nodeName);

    if (static_cast<bool>(handle)) {
        spdlog::trace("Anchor: Set by handle, nodeName='{}'", nodeName);
    }
}

void Anchor::SetWorldPosition(const RE::NiPoint3& pos) {
    m_directNode = nullptr;
    m_refHandle = RE::ObjectRefHandle();
    m_nodeName.clear();
    m_worldPosition = pos;
}

void Anchor::Clear() {
    m_directNode = nullptr;
    m_refHandle = RE::ObjectRefHandle();
    m_nodeName.clear();
    m_worldPosition = RE::NiPoint3(0, 0, 0);
    m_offset = RE::NiPoint3(0, 0, 0);
    m_useRotation = false;
    m_useScale = false;
}

bool Anchor::HasAnchor() const {
    return m_directNode != nullptr || static_cast<bool>(m_refHandle);
}

bool Anchor::IsValid() const {
    if (!HasAnchor()) {
        return true;  // No anchor needed, worldPosition fallback is always valid
    }

    auto* node = ResolveNode();
    if (!node) {
        return false;
    }

    // Check if node is still in the scene graph
    return node->parent != nullptr;
}

RE::NiAVObject* Anchor::ResolveNode() const {
    // Direct node pointer takes priority
    if (m_directNode) {
        return m_directNode;
    }

    // Try to resolve from handle
    if (static_cast<bool>(m_refHandle)) {
        auto refPtr = RE::TESObjectREFR::LookupByHandle(m_refHandle.native_handle());
        if (!refPtr) {
            return nullptr;
        }

        auto* root = refPtr->Get3D();
        if (!root) {
            return nullptr;
        }

        // If a specific node name was given, find it
        if (!m_nodeName.empty()) {
            return root->GetObjectByName(m_nodeName);
        }

        return root;
    }

    return nullptr;
}

RE::NiPoint3 Anchor::GetWorldPosition() const {
    auto* node = ResolveNode();
    if (node) {
        return node->world.translate + m_offset;
    }
    return m_worldPosition + m_offset;
}

RE::NiTransform Anchor::GetWorldTransform() const {
    RE::NiTransform transform;

    auto* node = ResolveNode();
    if (node) {
        transform.translate = node->world.translate + m_offset;

        if (m_useRotation) {
            transform.rotate = node->world.rotate;
        }
        // else: transform.rotate remains identity (default constructed)

        if (m_useScale) {
            transform.scale = node->world.scale;
        } else {
            transform.scale = 1.0f;
        }
    } else {
        // No anchor node - use worldPosition with identity rotation/scale
        transform.translate = m_worldPosition + m_offset;
        transform.scale = 1.0f;
    }

    return transform;
}

ProjectileTransform Anchor::ToWorld(const ProjectileTransform& local) const {
    auto* node = ResolveNode();
    if (!node) {
        // No anchor - local transform IS world transform (plus offset)
        ProjectileTransform world = local;
        world.position = local.position + m_worldPosition + m_offset;
        return world;
    }

    ProjectileTransform world = local;
    const auto& anchorWorld = node->world;

    // Transform position: worldPos = anchorPos + (anchorRotation * localPos) + offset
    RE::NiPoint3 rotatedLocal = RotatePoint(anchorWorld.rotate, local.position);
    world.position = anchorWorld.translate + rotatedLocal + m_offset;

    // Optionally inherit rotation
    if (m_useRotation) {
        // Compose anchor's world rotation with local rotation
        // WorldRot = AnchorWorldRot × LocalRot
        world.rotation = MultiplyMatrices(anchorWorld.rotate, local.rotation);
    }

    // Optionally inherit scale
    if (m_useScale) {
        world.scale *= anchorWorld.scale;
    }

    return world;
}

RE::NiPoint3 Anchor::RotatePoint(const RE::NiMatrix3& matrix, const RE::NiPoint3& point) {
    return RE::NiPoint3(
        matrix.entry[0][0] * point.x + matrix.entry[0][1] * point.y + matrix.entry[0][2] * point.z,
        matrix.entry[1][0] * point.x + matrix.entry[1][1] * point.y + matrix.entry[1][2] * point.z,
        matrix.entry[2][0] * point.x + matrix.entry[2][1] * point.y + matrix.entry[2][2] * point.z
    );
}

} // namespace Projectile
