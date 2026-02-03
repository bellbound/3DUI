#pragma once

#include "GameProjectile.h"
#include <string>

namespace Projectile {

// Unified anchor system for positioning objects relative to game nodes.
// Supports multiple anchor sources with automatic resolution.
class Anchor {
public:
    Anchor() = default;

    // === Set anchor source ===

    // Set anchor to a direct NiAVObject pointer (fast but caller must ensure validity)
    void SetDirect(RE::NiAVObject* node);

    // Set anchor by ObjectRefHandle and optional node name (safer, survives cell changes)
    // nodeName examples: "NPC L Hand [LHnd]", "WEAPON", "NPC Head [Head]"
    void SetByHandle(RE::ObjectRefHandle handle, std::string_view nodeName = "");

    // Convenience overload for raw handle value (from ObjectRefHandle::native_handle())
    // Safer for cross-DLL API use where ObjectRefHandle type may differ
    void SetByHandle(uint32_t nativeHandle, std::string_view nodeName = "") {
        if (nativeHandle != 0) {
            // Lookup the ref from native handle and get its handle
            auto refPtr = RE::TESObjectREFR::LookupByHandle(nativeHandle);
            if (refPtr) {
                SetByHandle(refPtr->GetHandle(), nodeName);
                return;
            }
        }
        // Invalid or zero handle - clear
        SetByHandle(RE::ObjectRefHandle(), nodeName);
    }

    // Set fallback world position (used when no anchor node is set)
    void SetWorldPosition(const RE::NiPoint3& pos);

    // Clear all anchor data
    void Clear();

    // === Configuration ===

    // Set offset applied after anchor transform
    void SetOffset(const RE::NiPoint3& offset) { m_offset = offset; }
    RE::NiPoint3 GetOffset() const { return m_offset; }

    // Whether to inherit anchor's rotation
    void SetUseRotation(bool use) { m_useRotation = use; }
    bool GetUseRotation() const { return m_useRotation; }

    // Whether to inherit anchor's scale
    void SetUseScale(bool use) { m_useScale = use; }
    bool GetUseScale() const { return m_useScale; }

    // === Resolution ===

    // Check if any anchor source is configured (direct node or handle)
    bool HasAnchor() const;

    // Check if the anchor is still valid (node exists and is in scene graph)
    bool IsValid() const;

    // Resolve anchor to NiAVObject* (returns nullptr if no anchor or invalid)
    RE::NiAVObject* ResolveNode() const;

    // === Transform computation ===

    // Get world position (anchor position + offset, or fallback worldPosition + offset)
    RE::NiPoint3 GetWorldPosition() const;

    // Get full world transform (includes rotation/scale if configured)
    RE::NiTransform GetWorldTransform() const;

    // Transform a local ProjectileTransform to world space using this anchor
    ProjectileTransform ToWorld(const ProjectileTransform& local) const;

    // === Utility ===

    // Matrix-vector multiplication helper
    static RE::NiPoint3 RotatePoint(const RE::NiMatrix3& matrix, const RE::NiPoint3& point);

private:
    // Anchor sources (priority: m_directNode > m_refHandle)
    RE::NiAVObject* m_directNode = nullptr;
    RE::ObjectRefHandle m_refHandle;
    std::string m_nodeName;  // Node name within refHandle

    // Fallback position when no anchor
    RE::NiPoint3 m_worldPosition{0, 0, 0};

    // Configuration
    RE::NiPoint3 m_offset{0, 0, 0};
    bool m_useRotation = false;
    bool m_useScale = false;
};

} // namespace Projectile
