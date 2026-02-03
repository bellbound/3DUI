#pragma once

#if !defined(TEST_ENVIRONMENT)
#include <RE/Skyrim.h>
#else
#include "TestStubs.h"
#endif

#include <memory>
#include <functional>
#include <cmath>
#include <string>

namespace Projectile {

// Forward declarations
class IPositionable;

// Helper to create an identity rotation matrix
inline RE::NiMatrix3 IdentityMatrix() {
    RE::NiMatrix3 mat;
    mat.entry[0][0] = 1.0f; mat.entry[0][1] = 0.0f; mat.entry[0][2] = 0.0f;
    mat.entry[1][0] = 0.0f; mat.entry[1][1] = 1.0f; mat.entry[1][2] = 0.0f;
    mat.entry[2][0] = 0.0f; mat.entry[2][1] = 0.0f; mat.entry[2][2] = 1.0f;
    return mat;
}

// Helper to multiply a rotation matrix by a point
inline RE::NiPoint3 RotatePoint(const RE::NiMatrix3& rot, const RE::NiPoint3& pt) {
    return RE::NiPoint3(
        rot.entry[0][0] * pt.x + rot.entry[0][1] * pt.y + rot.entry[0][2] * pt.z,
        rot.entry[1][0] * pt.x + rot.entry[1][1] * pt.y + rot.entry[1][2] * pt.z,
        rot.entry[2][0] * pt.x + rot.entry[2][1] * pt.y + rot.entry[2][2] * pt.z
    );
}

// Helper to multiply two rotation matrices
inline RE::NiMatrix3 MultiplyMatrices(const RE::NiMatrix3& a, const RE::NiMatrix3& b) {
    RE::NiMatrix3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.entry[i][j] = a.entry[i][0] * b.entry[0][j] +
                                 a.entry[i][1] * b.entry[1][j] +
                                 a.entry[i][2] * b.entry[2][j];
        }
    }
    return result;
}

// Helper to compute the inverse (transpose) of a rotation matrix
// For orthonormal rotation matrices, inverse == transpose
inline RE::NiMatrix3 InverseRotationMatrix(const RE::NiMatrix3& rot) {
    RE::NiMatrix3 result;
    // Transpose the matrix
    result.entry[0][0] = rot.entry[0][0];
    result.entry[0][1] = rot.entry[1][0];
    result.entry[0][2] = rot.entry[2][0];
    result.entry[1][0] = rot.entry[0][1];
    result.entry[1][1] = rot.entry[1][1];
    result.entry[1][2] = rot.entry[2][1];
    result.entry[2][0] = rot.entry[0][2];
    result.entry[2][1] = rot.entry[1][2];
    result.entry[2][2] = rot.entry[2][2];
    return result;
}

// Helper to build a facing rotation matrix (looks from 'from' toward 'to')
// Similar to FacingStrategy but as a standalone function
inline RE::NiMatrix3 BuildFacingRotation(const RE::NiPoint3& from, const RE::NiPoint3& to) {
    RE::NiMatrix3 rotation = IdentityMatrix();

    RE::NiPoint3 forward = to - from;
    float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);

    if (length < 0.001f) {
        return rotation;  // Identity if too close
    }

    forward.x /= length;
    forward.y /= length;
    forward.z /= length;

    RE::NiPoint3 worldUp(0, 0, 1);
    float dot = forward.x * worldUp.x + forward.y * worldUp.y + forward.z * worldUp.z;

    RE::NiPoint3 up;
    if (std::abs(dot) > 0.999f) {
        up = RE::NiPoint3(0, 1, 0);
    } else {
        up = worldUp;
    }

    // right = forward × up (order matters for handedness!)
    // For right-handed system with Y=forward, Z=up: X = Y × Z
    RE::NiPoint3 right;
    right.x = forward.y * up.z - forward.z * up.y;
    right.y = forward.z * up.x - forward.x * up.z;
    right.z = forward.x * up.y - forward.y * up.x;

    length = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    right.x /= length;
    right.y /= length;
    right.z /= length;

    // Recompute up = right × forward (for right-handed: Z = X × Y)
    up.x = right.y * forward.z - right.z * forward.y;
    up.y = right.z * forward.x - right.x * forward.z;
    up.z = right.x * forward.y - right.y * forward.x;

    // Skyrim coordinate convention: +X=right, +Y=forward, +Z=up
    // Map local Y to forward (toward target), local Z to up
    rotation.entry[0][0] = right.x;   rotation.entry[0][1] = forward.x;   rotation.entry[0][2] = up.x;
    rotation.entry[1][0] = right.y;   rotation.entry[1][1] = forward.y;   rotation.entry[1][2] = up.y;
    rotation.entry[2][0] = right.z;   rotation.entry[2][1] = forward.z;   rotation.entry[2][2] = up.z;

    return rotation;
}

// Helper to convert rotation matrix to euler angles (pitch, roll, yaw)
// Uses ZYX convention (yaw around Z, then pitch around Y, then roll around X)
inline RE::NiPoint3 MatrixToEuler(const RE::NiMatrix3& rot) {
    float pitch, roll, yaw;

    // Extract yaw (rotation around Z)
    yaw = std::atan2(rot.entry[1][0], rot.entry[0][0]);

    // Extract pitch (rotation around Y)
    float cosPitch = std::sqrt(rot.entry[0][0] * rot.entry[0][0] + rot.entry[1][0] * rot.entry[1][0]);
    pitch = std::atan2(-rot.entry[2][0], cosPitch);

    // Extract roll (rotation around X)
    float sinYaw = std::sin(yaw);
    float cosYaw = std::cos(yaw);
    roll = std::atan2(sinYaw * rot.entry[0][2] - cosYaw * rot.entry[1][2],
                      cosYaw * rot.entry[1][1] - sinYaw * rot.entry[0][1]);

    return RE::NiPoint3(pitch, roll, yaw);
}

// Helper to convert euler angles (pitch, roll, yaw) to rotation matrix
// Uses ZYX convention to match MatrixToEuler
// R = Rz(yaw) × Ry(pitch) × Rx(roll)
inline RE::NiMatrix3 EulerToMatrix(const RE::NiPoint3& euler) {
    float pitch = euler.x;  // rotation around Y axis
    float roll = euler.y;   // rotation around X axis
    float yaw = euler.z;    // rotation around Z axis

    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cr = std::cos(roll),  sr = std::sin(roll);
    float cy = std::cos(yaw),   sy = std::sin(yaw);

    RE::NiMatrix3 mat;
    mat.entry[0][0] = cy * cp;
    mat.entry[0][1] = cy * sp * sr - sy * cr;
    mat.entry[0][2] = cy * sp * cr + sy * sr;
    mat.entry[1][0] = sy * cp;
    mat.entry[1][1] = sy * sp * sr + cy * cr;
    mat.entry[1][2] = sy * sp * cr - cy * sr;
    mat.entry[2][0] = -sp;
    mat.entry[2][1] = cp * sr;
    mat.entry[2][2] = cp * cr;
    return mat;
}

// Input event types for the composable hierarchy
enum class InputEventType {
    HoverEnter,     // Hand entered hover threshold
    HoverExit,      // Hand exited hover threshold
    GrabStart,      // Grab button pressed while hovering
    GrabEnd,        // Grab button released
    ActivateDown,   // Activation button pressed while hovering
    ActivateUp      // Activation button released while hovering
};

// Input event dispatched through the IPositionable hierarchy
struct InputEvent {
    InputEventType type;
    IPositionable* source = nullptr;       // The node that originated the event (usually a leaf)
    RE::NiAVObject* handNode = nullptr;    // The hand node involved in the interaction
    bool isLeftHand = false;               // Which hand triggered the event
    bool closeOnActivate = false;          // For Activate events: should close the menu
    bool sendHapticPulse = true;           // Whether to trigger haptic feedback for this event

    // Convenience constructors
    static InputEvent HoverEnter(IPositionable* src, RE::NiAVObject* hand, bool isLeft) {
        return {InputEventType::HoverEnter, src, hand, isLeft, false, true};
    }

    static InputEvent HoverExit(IPositionable* src, RE::NiAVObject* hand, bool isLeft) {
        return {InputEventType::HoverExit, src, hand, isLeft, false, true};
    }

    static InputEvent GrabStart(IPositionable* src, RE::NiAVObject* hand, bool isLeft) {
        return {InputEventType::GrabStart, src, hand, isLeft, false, true};
    }

    static InputEvent GrabEnd(IPositionable* src, RE::NiAVObject* hand, bool isLeft) {
        return {InputEventType::GrabEnd, src, hand, isLeft, false, true};
    }

    static InputEvent ActivateDown(IPositionable* src, RE::NiAVObject* hand, bool isLeft, bool shouldClose) {
        return {InputEventType::ActivateDown, src, hand, isLeft, shouldClose, true};
    }

    static InputEvent ActivateUp(IPositionable* src, RE::NiAVObject* hand, bool isLeft) {
        return {InputEventType::ActivateUp, src, hand, isLeft, false, true};
    }
};

// Base interface for anything that can be positioned in the composable hierarchy.
// Provides local/world transform computation via parent chain (true scene graph).
// Transform inheritance: WorldTransform = Parent.WorldTransform × LocalTransform
class IPositionable {
public:
    virtual ~IPositionable() = default;

    // === Identity ===
    // String ID for API lookup and debugging. Not required for internal use.
    virtual void SetID(const std::string& id) { m_id = id; }
    virtual const std::string& GetID() const { return m_id; }

    // === Local Transform ===
    // Local position relative to parent (or world if no parent)
    virtual void SetLocalPosition(const RE::NiPoint3& pos) { m_localPosition = pos; }
    virtual RE::NiPoint3 GetLocalPosition() const { return m_localPosition; }

    // Local rotation relative to parent (composed up the chain)
    virtual void SetLocalRotation(const RE::NiMatrix3& rot) { m_localRotation = rot; }

    // Euler angle overloads (DEGREES) - build rotation matrix internally
    // Uses ZYX convention: euler.x=pitch (Y-axis), euler.y=roll (X-axis), euler.z=yaw (Z-axis)
    virtual void SetLocalRotation(const RE::NiPoint3& eulerDegrees) {
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        m_localRotation = EulerToMatrix(RE::NiPoint3(
            eulerDegrees.x * DEG_TO_RAD,
            eulerDegrees.y * DEG_TO_RAD,
            eulerDegrees.z * DEG_TO_RAD));
    }
    virtual void SetLocalRotation(float pitchDeg, float rollDeg, float yawDeg) {
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        m_localRotation = EulerToMatrix(RE::NiPoint3(
            pitchDeg * DEG_TO_RAD,
            rollDeg * DEG_TO_RAD,
            yawDeg * DEG_TO_RAD));
    }

    virtual RE::NiMatrix3 GetLocalRotation() const { return m_localRotation; }

    // Local scale relative to parent (multiplied up the chain)
    virtual void SetLocalScale(float scale) { m_localScale = scale; }
    virtual float GetLocalScale() const { return m_localScale; }

    // === World Transform (computed from parent chain) ===
    // World rotation = Parent.WorldRotation × LocalRotation
    virtual RE::NiMatrix3 GetWorldRotation() const {
        if (m_parent) {
            return MultiplyMatrices(m_parent->GetWorldRotation(), m_localRotation);
        }
        return m_localRotation;
    }

    // World position = Parent.WorldPosition + Parent.WorldRotation × LocalPosition
    // This is the key scene graph formula - parent rotation affects child position
    virtual RE::NiPoint3 GetWorldPosition() const {
        if (m_parent) {
            RE::NiMatrix3 parentWorldRot = m_parent->GetWorldRotation();
            RE::NiPoint3 rotatedLocalPos = RotatePoint(parentWorldRot, m_localPosition);
            return m_parent->GetWorldPosition() + rotatedLocalPos;
        }
        return m_localPosition;
    }

    virtual float GetWorldScale() const {
        if (m_parent) {
            return m_parent->GetWorldScale() * m_localScale;
        }
        return m_localScale;
    }

    // === Initialization ===
    // Called by the driver when the hierarchy is spawned
    // Override in derived classes to acquire resources (forms, lights, etc.)
    virtual void Initialize() {}

    // === Update ===
    // Called each frame to apply transforms and update state
    // Override in derived classes to apply local transform to actual objects
    virtual void Update(float /*deltaTime*/) {}

    // === Visibility ===
    // Local visibility - what was explicitly set on this node by the user
    // This persists across parent hide/show cycles (tracks user intent)
    virtual void SetVisible(bool visible) { m_localVisible = visible; }
    virtual bool IsVisible() const { return m_localVisible; }

    // Effective visibility - considers parent hierarchy
    // A node is effectively visible only if it AND all ancestors are visible
    virtual bool IsEffectivelyVisible() const {
        if (!m_localVisible) return false;
        if (m_parent) return m_parent->IsEffectivelyVisible();
        return true;
    }

    // Called when parent hides - allows resource cleanup without changing m_localVisible
    // Override in derived classes to release resources (e.g., unbind game projectiles)
    // When parent shows again, Update() will be called and can rebind based on m_localVisible
    virtual void OnParentHide() {}

    // === Parent Management ===
    virtual void SetParent(IPositionable* parent) { m_parent = parent; }
    virtual IPositionable* GetParent() const { return m_parent; }
    virtual bool HasParent() const { return m_parent != nullptr; }

    // === Event System ===
    // Dispatch an event - starts at this node and bubbles up to root
    // Returns true if any handler consumed the event
    virtual bool DispatchEvent(InputEvent& event) {
        // First, try to handle locally
        if (OnEvent(event)) {
            return true;  // Event was consumed
        }

        // Not handled - bubble up to parent
        if (m_parent) {
            return m_parent->DispatchEvent(event);
        }

        return false;  // Event reached root without being handled
    }

    // Handle an event at this node
    // Override in derived classes to handle specific events
    // Return true to consume the event (stop bubbling), false to let it bubble up
    virtual bool OnEvent(InputEvent& event) {
        return false;  // Default: don't handle, let it bubble
    }

    // Callback for when an event bubbles all the way to root without being handled
    // Only called on the root node (no parent)
    using UnhandledEventCallback = std::function<void(const InputEvent&)>;
    void SetUnhandledEventCallback(UnhandledEventCallback callback) {
        m_unhandledEventCallback = std::move(callback);
    }

protected:
    std::string m_id;  // Optional string ID for API lookup
    RE::NiPoint3 m_localPosition{0, 0, 0};
    RE::NiMatrix3 m_localRotation = IdentityMatrix();  // Identity by default
    float m_localScale = 1.0f;
    bool m_localVisible = true;  // User's intended visibility (persists across parent cycles)
    IPositionable* m_parent = nullptr;
    UnhandledEventCallback m_unhandledEventCallback;
};

// Shared handle types for IPositionable
using IPositionablePtr = std::shared_ptr<IPositionable>;
using IPositionableWeakPtr = std::weak_ptr<IPositionable>;

} // namespace Projectile
