#include "GameProjectile.h"
#if !defined(TEST_ENVIRONMENT)
#include "TextureManipulator.h"
#endif
#include "../log.h"
#include "../util/VRNodes.h"

namespace Projectile {

GameProjectile::~GameProjectile() {
    Unbind();
}

GameProjectile::GameProjectile(GameProjectile&& other) noexcept
    : m_projectile(other.m_projectile)
    , m_refHandle(other.m_refHandle)
    , m_targetTransform(other.m_targetTransform)
    , m_modelPath(std::move(other.m_modelPath))
    , m_texturePath(std::move(other.m_texturePath))
    , m_borderColor(std::move(other.m_borderColor))
    , m_needsTextureSet(other.m_needsTextureSet)
    , m_visible(other.m_visible)
    , m_markedForDeletion(other.m_markedForDeletion)
    , m_assignmentTime(other.m_assignmentTime)
{
    other.m_projectile = nullptr;
    other.m_refHandle = 0;
    other.m_needsTextureSet = false;
}

GameProjectile& GameProjectile::operator=(GameProjectile&& other) noexcept {
    if (this != &other) {
        Unbind();
        m_projectile = other.m_projectile;
        m_refHandle = other.m_refHandle;
        m_targetTransform = other.m_targetTransform;
        m_modelPath = std::move(other.m_modelPath);
        m_texturePath = std::move(other.m_texturePath);
        m_borderColor = std::move(other.m_borderColor);
        m_needsTextureSet = other.m_needsTextureSet;
        m_visible = other.m_visible;
        m_markedForDeletion = other.m_markedForDeletion;
        m_assignmentTime = other.m_assignmentTime;

        other.m_projectile = nullptr;
        other.m_refHandle = 0;
        other.m_needsTextureSet = false;
    }
    return *this;
}

void GameProjectile::BindToProjectile(RE::Projectile* proj) {

    if (m_projectile) {
        Unbind();
    }

    if (!proj) {
        spdlog::warn("GameProjectile::BindToProjectile called with null projectile");
        return;
    }

    // Try to get formID - this could crash if proj is garbage
    RE::FormID formId = 0;
    try {
        formId = proj->GetFormID();
    } catch (...) {
        spdlog::error("GameProjectile::BindToProjectile EXCEPTION getting formID!");
        return;
    }

    if (formId == 0) {
        spdlog::warn("GameProjectile::BindToProjectile projectile has formID 0, suspicious");
    }

    m_projectile = proj;

    m_refHandle = GameProjectileUtils::GetOrCreateRefHandle(proj);

    // CRITICAL: Reset texture flag when binding to a NEW projectile
    // This ensures textures are re-applied after visibility toggle (unbind/rebind cycle).
    // Without this, rebound projectiles would not apply their texture because
    // m_needsTextureSet was cleared by the previous ApplyPendingTexture() call.
    if (!m_texturePath.empty()) {
        m_needsTextureSet = true;
        m_textureRetryCount = 0;
        spdlog::trace("GameProjectile::BindToProjectile - Reset m_needsTextureSet for texture '{}'", m_texturePath);
    }

    PreventDestruction();

    spdlog::trace("GameProjectile bound to projectile {:x}, refHandle: {:x}", formId, m_refHandle);
}

void GameProjectile::Unbind() {
    spdlog::trace("GameProjectile::Unbind ENTER proj={:p} refHandle={:x}",
        static_cast<void*>(m_projectile), m_refHandle);

    if (m_projectile && m_refHandle != 0) {
        if (ValidateProjectileExists(false)) {
            // Projectile is still valid - safe to call methods on it
            spdlog::trace("GameProjectile::Unbind projectile still valid, hiding");
            SetVisible(false);
            ApplyTransform();
            spdlog::trace("GameProjectile unbound from projectile {:x}", m_projectile->GetFormID());
        } else {
            // Projectile was destroyed by the game - don't touch it!
            spdlog::trace("GameProjectile::Unbind projectile already destroyed by game, skipping hide");
        }
    }

    m_projectile = nullptr;
    m_refHandle = 0;
    m_markedForDeletion = false;
}

bool GameProjectile::IsProjectileValid() const {
    if (!m_projectile || m_refHandle == 0) {
        return false;
    }

    // Look up the reference by handle - if the game destroyed the projectile,
    // this will return nullptr or a different pointer
    auto refPtr = RE::TESObjectREFR::LookupByHandle(m_refHandle);

    // Verify the handle still resolves to our projectile
    return refPtr && static_cast<void*>(refPtr.get()) == static_cast<void*>(m_projectile);
}

RE::FormID GameProjectile::GetBaseFormID() const {
    if (m_projectile && m_projectile->GetBaseObject()) {
        return m_projectile->GetBaseObject()->GetFormID();
    }
    return 0;
}

void GameProjectile::SetTransform(const ProjectileTransform& transform) {
    m_targetTransform = transform;
}

void GameProjectile::ApplyTransform() {
    // Throttled logging counter
    static uint64_t s_applyCount = 0;
    ++s_applyCount;
    bool shouldLog = (s_applyCount % 300 == 1);

    if (!m_projectile) {
        spdlog::warn("GameProjectile::ApplyTransform - m_projectile is null");
        return;
    }

    // Validate projectile still exists (game may have destroyed it via collision/range)
    if (m_refHandle != 0 && !ValidateProjectileExists(true)) {
        spdlog::warn("GameProjectile::ApplyTransform - ValidateProjectileExists failed, refHandle={:x}", m_refHandle);
        return;
    }

    auto* node = m_projectile->Get3D();
    if (!node) {
        spdlog::warn("GameProjectile::ApplyTransform - Get3D() returned null");
        return;
    }

    // === CRITICAL: Prevent game from destroying the projectile ===
    // Must be called EVERY FRAME to reset lifetime counters and traveled distance.
    // This is the key fix - SpellWheelVR does this continuously in their update hook.
    PreventDestruction();

    // Apply position
    m_projectile->data.location = m_targetTransform.position;

    // Note: proj->data.angle is not set - we apply rotation directly to the node
    // (data.angle doesn't propagate to visuals for stationary projectiles)

    // Update scene node transforms
    UpdateNodeTransform();
}

bool GameProjectile::ValidateProjectileExists(bool clearIfInvalid) {
    if (!m_projectile || m_refHandle == 0) {
        return false;
    }

    // Look up the reference by handle - if the game destroyed the projectile,
    // this will return nullptr or a different pointer
    auto refPtr = RE::TESObjectREFR::LookupByHandle(m_refHandle);

    // Cast to void* for comparison - works in both test and production builds
    // (In tests, Projectile doesn't inherit from TESObjectREFR)
    bool isValid = refPtr && static_cast<void*>(refPtr.get()) == static_cast<void*>(m_projectile);

    if (!isValid) {
        spdlog::warn("[VALIDATE] Projectile {:p} no longer valid! refHandle={:x} lookup={:p}. Game likely destroyed it.",
            static_cast<void*>(m_projectile), m_refHandle, refPtr ? static_cast<void*>(refPtr.get()) : nullptr);

        if (clearIfInvalid) {
            m_projectile = nullptr;
            m_refHandle = 0;
        }
    }

    return isValid;
}

void GameProjectile::ZeroVelocity() {
    if (m_projectile) {
        // Zero out projectile velocity to keep it stationary
        // The velocity is stored in the projectile's runtime data
        m_projectile->GetProjectileRuntimeData().linearVelocity = RE::NiPoint3(0.0f, 0.0f, 0.0f);
    }
}

void GameProjectile::UpdateNodeTransform() {
    if (!m_projectile) {
        return;
    }

    auto* node = m_projectile->Get3D();
    if (!node) {
        return;
    }

    // Update node position
    node->local.translate = m_targetTransform.position;
    node->world.translate = m_targetTransform.position;

    // Update scale
    float effectiveScale = m_visible ? m_targetTransform.scale : 0.00001f;
    node->local.scale = effectiveScale;

    // Update rotation - apply matrix directly to node
    // (rotation is now stored as matrix in ProjectileTransform)
    node->local.rotate = m_targetTransform.rotation;
}

void GameProjectile::SetVisible(bool visible) {
    m_visible = visible;
    // Visibility will be applied on next ApplyTransform() call via scale
}

void GameProjectile::ApplyPendingTexture() {
#if !defined(TEST_ENVIRONMENT)
    // Early out if no texture pending
    if (!m_needsTextureSet || m_texturePath.empty()) {
        return;
    }

    // Get the 3D node - may not be ready yet
    auto* node = m_projectile ? m_projectile->Get3D() : nullptr;
    if (!node) {
        return;  // Not ready yet, will retry next frame
    }

    // Increment retry counter
    ++m_textureRetryCount;

    // Check if we've exceeded the retry limit
    if (m_textureRetryCount > MAX_TEXTURE_RETRIES) {
        spdlog::error("GameProjectile::ApplyPendingTexture - Exceeded {} retries for texture '{}', giving up",
            MAX_TEXTURE_RETRIES, m_texturePath);
        m_needsTextureSet = false;
        m_textureRetryCount = 0;
        return;
    }

    spdlog::trace("GameProjectile::ApplyPendingTexture - projFormID={:x} refHandle={:x} texture='{}' (attempt {}/{})",
        m_projectile ? m_projectile->GetFormID() : 0, m_refHandle, m_texturePath,
        m_textureRetryCount, MAX_TEXTURE_RETRIES);

    // icon_template.nif structure: BSFadeNode → container → geometry nodes
    auto* container = TextureManipulator::GetCharacterContainer(node);
    if (!container) {
        spdlog::warn("GameProjectile::ApplyPendingTexture - No container found, will retry next frame");
        return;  // Node not ready, try again next frame
    }

    auto charNodes = TextureManipulator::GetAllCharNodes(node);
    if (charNodes.empty()) {
        spdlog::warn("GameProjectile::ApplyPendingTexture - No char nodes found, will retry next frame");
        return;
    }

    spdlog::trace("GameProjectile::ApplyPendingTexture - Found {} geometry nodes, applying texture", charNodes.size());

    // Apply texture to all geometry nodes in the icon
    bool success = false;
    for (auto* charNode : charNodes) {
        if (TextureManipulator::SetTexture(charNode, m_texturePath.c_str())) {
            success = true;
        }
    }

    if (success) {
        m_needsTextureSet = false;
        m_textureRetryCount = 0;
        spdlog::trace("GameProjectile::ApplyPendingTexture - SUCCESS, texture applied for projFormID={:x}",
            m_projectile ? m_projectile->GetFormID() : 0);
    } else {
        spdlog::error("GameProjectile::ApplyPendingTexture - FAILED to apply texture '{}', will retry", m_texturePath);
    }
#else
    // Stub for test environment - just clear the flag
    m_needsTextureSet = false;
    m_textureRetryCount = 0;
#endif
}

void GameProjectile::SetModelPath(const std::string& path) {
    m_modelPath = path;
    // Note: Model path must be set on the BGSProjectile form before firing
    // This is stored here for reference and should be used during spawn setup
}

void GameProjectile::SetTexturePath(const std::string& path) {
    m_texturePath = path;
    if (!path.empty()) {
        m_needsTextureSet = true;
        m_textureRetryCount = 0;  // Reset retry counter for new texture
    }
}

void GameProjectile::SetBorderColor(const std::string& hexColor) {
    m_borderColor = hexColor;
}

void GameProjectile::MarkForDeletion() {
    m_markedForDeletion = true;
    SetVisible(false);
}

void GameProjectile::PreventDestruction() {
    if (!m_projectile) {
        spdlog::warn("GameProjectile::PreventDestruction - m_projectile is null, returning early");
        return;
    }

    // === One-time form setup (only logs once per projectile) ===
    auto* baseObj = m_projectile->GetBaseObject();
    if (baseObj) {
        auto* projForm = baseObj->As<RE::BGSProjectile>();
        if (projForm) {
            // Ensure high range as safety net
            if (projForm->data.range < 99999.0f) {
                spdlog::trace("[FIX] Set projForm->data.range: {:.1f} -> 99999.0", projForm->data.range);
                projForm->data.range = 99999.0f;
            }

            // Zero gravity to prevent falling
            if (projForm->data.gravity != 0.0f) {
                spdlog::trace("[FIX] Zeroing projForm->data.gravity: {:.4f} -> 0", projForm->data.gravity);
                projForm->data.gravity = 0.0f;
            }
        }
    }

    // === Per-frame: Zero velocity to keep projectile stationary ===
    auto& runtimeData = m_projectile->GetProjectileRuntimeData();
    runtimeData.linearVelocity = RE::NiPoint3(0.0f, 0.0f, 0.0f);
    runtimeData.velocity = RE::NiPoint3(0.0f, 0.0f, 0.0f);
}

// =============================================================================
// Utility implementations
// =============================================================================

namespace GameProjectileUtils {

uint32_t GetOrCreateRefHandle(RE::Projectile* proj) {
    if (!proj) {
        return 0;
    }

    RE::ObjectRefHandle handle = proj->GetHandle();
    return static_cast<bool>(handle) ? handle.native_handle() : 0;
}

void GetAttitudeAndHeading(const RE::NiPoint3& from, const RE::NiPoint3& to,
                           float& outAttitude, float& outHeading) {
    float x = to.x - from.x;
    float y = to.y - from.y;
    float z = to.z - from.z;
    float xy = std::sqrt(x * x + y * y);

    outHeading = std::atan2(x, y);
    outAttitude = std::atan2(-z, xy);
}

RE::NiPoint3 GetHMDPosition() {
    // Use VR API to get the actual HMD node
    auto* hmdNode = VRNodes::GetHMD();
    if (hmdNode) {
        return hmdNode->world.translate;
    }

    // Fallback for non-VR or if VR data unavailable
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::NiPoint3(0.0f, 0.0f, 0.0f);
    }

    auto* root = player->Get3D();
    if (!root) {
        return player->GetPosition();
    }

    // Fallback to player head position
    auto* headNode = root->GetObjectByName("NPC Head [Head]"sv);
    if (headNode) {
        return headNode->world.translate;
    }

    return player->GetPosition();
}

RE::NiPoint3 GetPlayerPosition() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::NiPoint3(0.0f, 0.0f, 0.0f);
    }
    return player->GetPosition();
}

} // namespace GameProjectileUtils

} // namespace Projectile
