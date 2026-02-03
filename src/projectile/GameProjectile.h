#pragma once

#if !defined(TEST_ENVIRONMENT)
#include "RE/Skyrim.h"
#else
#include "TestStubs.h"
#endif

#include <string>
#include <cstdint>

namespace Projectile {

// Identity matrix helper for initialization
inline RE::NiMatrix3 IdentityMatrix3() {
    RE::NiMatrix3 mat;
    mat.entry[0][0] = 1.0f; mat.entry[0][1] = 0.0f; mat.entry[0][2] = 0.0f;
    mat.entry[1][0] = 0.0f; mat.entry[1][1] = 1.0f; mat.entry[1][2] = 0.0f;
    mat.entry[2][0] = 0.0f; mat.entry[2][1] = 0.0f; mat.entry[2][2] = 1.0f;
    return mat;
}

// Transform data for projectile positioning
struct ProjectileTransform {
    RE::NiPoint3 position{0.0f, 0.0f, 0.0f};
    RE::NiMatrix3 rotation = IdentityMatrix3();  // Rotation matrix (single source of truth)
    float scale = 1.0f;
};

// Low-level abstraction over game projectile objects
// This class manages the direct interaction with Skyrim's Projectile class
// and its associated NiNode scene graph
class GameProjectile {
public:
    GameProjectile() = default;
    ~GameProjectile();

    // Non-copyable, movable
    GameProjectile(const GameProjectile&) = delete;
    GameProjectile& operator=(const GameProjectile&) = delete;
    GameProjectile(GameProjectile&& other) noexcept;
    GameProjectile& operator=(GameProjectile&& other) noexcept;

    // Binding to game objects
    void BindToProjectile(RE::Projectile* proj);
    void Unbind();
    bool IsBound() const { return m_projectile != nullptr; }

    // Validates projectile still exists in game world (not just pointer non-null).
    // Use this before accessing projectile methods like Get3D() to avoid crashes
    // from dangling pointers when the game has destroyed the projectile.
    bool IsProjectileValid() const;

    // Get the underlying game projectile (for hook identification)
    RE::Projectile* GetProjectile() const { return m_projectile; }
    RE::FormID GetBaseFormID() const;
    uint32_t GetRefHandle() const { return m_refHandle; }

    // Transform manipulation (applied each frame via hook)
    void SetTransform(const ProjectileTransform& transform);
    const ProjectileTransform& GetTargetTransform() const { return m_targetTransform; }

    // Apply the current transform to the game projectile
    // Called from the projectile update hook
    void ApplyTransform();

    // Visibility control
    void SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }

    // Model/mesh control (set before spawning)
    void SetModelPath(const std::string& path);
    const std::string& GetModelPath() const { return m_modelPath; }

    // Texture-based display (alternative to custom model)
    // When set, uses BasicPicture.nif and applies the texture to "Picture" node
    void SetTexturePath(const std::string& path);
    const std::string& GetTexturePath() const { return m_texturePath; }
    void SetBorderColor(const std::string& hexColor);
    const std::string& GetBorderColor() const { return m_borderColor; }
    bool NeedsTextureSet() const { return m_needsTextureSet; }
    void ClearTextureSetFlag() { m_needsTextureSet = false; }

    // Apply pending texture to the projectile's 3D node.
    // MUST be called from the main thread (texture loading is not thread-safe).
    // Called from ControlledProjectile::Update() which runs on the main thread.
    void ApplyPendingTexture();

    // Mark for deletion - projectile will be hidden and released
    void MarkForDeletion();
    bool IsMarkedForDeletion() const { return m_markedForDeletion; }

    // Timestamp for pool recycling (when was this last assigned)
    void SetAssignmentTime(uint64_t time) { m_assignmentTime = time; }
    uint64_t GetAssignmentTime() const { return m_assignmentTime; }

private:
    void ZeroVelocity();
    void UpdateNodeTransform();

    // Validates that m_projectile still exists in the game world by checking refHandle.
    // Returns true if projectile is valid, false if game destroyed it.
    // If invalid and clearIfInvalid=true, clears m_projectile and m_refHandle.
    bool ValidateProjectileExists(bool clearIfInvalid = true);

    // Prevents the game from destroying the projectile by:
    // 1. Setting very high range on the BGSProjectile form
    // 2. Resetting runtime traveled distance (range) to 0
    // 3. Resetting living time to 0
    // 4. Zeroing gravity and velocity
    // Called during binding and each frame in ApplyTransform.
    void PreventDestruction();

    RE::Projectile* m_projectile = nullptr;
    uint32_t m_refHandle = 0;

    ProjectileTransform m_targetTransform;
    std::string m_modelPath = "meshes\\clutter\\dwemer\\centuriondynamocore01.nif";
    std::string m_texturePath;       // For image-based display
    std::string m_borderColor;       // Hex color for border (e.g., "ff0000")
    bool m_needsTextureSet = false;  // Flag for pending texture application
    int m_textureRetryCount = 0;     // Counter for texture application retries
    static constexpr int MAX_TEXTURE_RETRIES = 50;  // Give up after this many attempts
    bool m_visible = true;
    bool m_markedForDeletion = false;
    uint64_t m_assignmentTime = 0;
};

// Helper functions for projectile creation
namespace GameProjectileUtils {
    // Get or create a reference handle for a projectile
    uint32_t GetOrCreateRefHandle(RE::Projectile* proj);

    // Calculate heading angle from one point to another (for billboarding)
    void GetAttitudeAndHeading(const RE::NiPoint3& from, const RE::NiPoint3& to,
                               float& outAttitude, float& outHeading);

    // Get player HMD position (for VR billboarding)
    RE::NiPoint3 GetHMDPosition();

    // Get player world position
    RE::NiPoint3 GetPlayerPosition();
}

} // namespace Projectile
