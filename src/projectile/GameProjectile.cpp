#include "GameProjectile.h"
#if !defined(TEST_ENVIRONMENT)
#include "TextureManipulator.h"
#endif
#include "../log.h"
#include "../util/VRNodes.h"

#include <cmath>

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
    , m_effectGlow(other.m_effectGlow)
    , m_hasEffectColor(other.m_hasEffectColor)
    , m_needsColorSet(other.m_needsColorSet)
    , m_visible(other.m_visible)
    , m_markedForDeletion(other.m_markedForDeletion)
    , m_assignmentTime(other.m_assignmentTime)
    , m_modelCenter(other.m_modelCenter)
    , m_modelCenterMeasured(other.m_modelCenterMeasured)
{
    std::copy(std::begin(other.m_effectColor), std::end(other.m_effectColor), std::begin(m_effectColor));

    other.m_projectile = nullptr;
    other.m_refHandle = 0;
    other.m_needsTextureSet = false;
    other.m_needsColorSet = false;
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
        std::copy(std::begin(other.m_effectColor), std::end(other.m_effectColor), std::begin(m_effectColor));
        m_effectGlow = other.m_effectGlow;
        m_hasEffectColor = other.m_hasEffectColor;
        m_needsColorSet = other.m_needsColorSet;
        m_visible = other.m_visible;
        m_markedForDeletion = other.m_markedForDeletion;
        m_assignmentTime = other.m_assignmentTime;
        m_modelCenter = other.m_modelCenter;
        m_modelCenterMeasured = other.m_modelCenterMeasured;

        other.m_projectile = nullptr;
        other.m_refHandle = 0;
        other.m_needsTextureSet = false;
        other.m_needsColorSet = false;
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

    // Same reasoning for the tint: the new projectile has a new 3D node whose material
    // is back at the mesh's authored colour.
    if (m_hasEffectColor) {
        m_needsColorSet = true;
        m_colorRetryCount = 0;
    }

    // And for the model's centre: a new node, possibly a new mesh on the form.
    m_modelCenterMeasured = false;
    m_modelCenterAttempts = 0;
    m_modelCenter = {0.0f, 0.0f, 0.0f};

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

#if !defined(TEST_ENVIRONMENT)
namespace {

// Rotation, translation and scale of a node relative to the root of the walk. Composed by
// hand rather than through NiTransform's operator*, which is one of the game's own functions
// reached by address - not something to put on a per-mesh path in VR when the arithmetic is
// four lines.
struct RelativeTransform {
    RE::NiMatrix3 rotate = IdentityMatrix3();
    RE::NiPoint3  translate{0.0f, 0.0f, 0.0f};
    float         scale = 1.0f;
};

RE::NiMatrix3 MultiplyRotations(const RE::NiMatrix3& a, const RE::NiMatrix3& b) {
    RE::NiMatrix3 result;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.entry[row][col] = a.entry[row][0] * b.entry[0][col] +
                                     a.entry[row][1] * b.entry[1][col] +
                                     a.entry[row][2] * b.entry[2][col];
        }
    }
    return result;
}

// Rotate and scale a model-space offset into the space the node draws in.
RE::NiPoint3 RotateAndScale(const RE::NiMatrix3& r, const RE::NiPoint3& point, float scale) {
    return RE::NiPoint3{
        (r.entry[0][0] * point.x + r.entry[0][1] * point.y + r.entry[0][2] * point.z) * scale,
        (r.entry[1][0] * point.x + r.entry[1][1] * point.y + r.entry[1][2] * point.z) * scale,
        (r.entry[2][0] * point.x + r.entry[2][1] * point.y + r.entry[2][2] * point.z) * scale};
}

RE::NiPoint3 Apply(const RelativeTransform& transform, const RE::NiPoint3& point) {
    const RE::NiMatrix3& r = transform.rotate;
    const RE::NiPoint3 scaled{point.x * transform.scale, point.y * transform.scale,
                              point.z * transform.scale};
    return RE::NiPoint3{
        r.entry[0][0] * scaled.x + r.entry[0][1] * scaled.y + r.entry[0][2] * scaled.z +
            transform.translate.x,
        r.entry[1][0] * scaled.x + r.entry[1][1] * scaled.y + r.entry[1][2] * scaled.z +
            transform.translate.y,
        r.entry[2][0] * scaled.x + r.entry[2][1] * scaled.y + r.entry[2][2] * scaled.z +
            transform.translate.z};
}

RelativeTransform Compose(const RelativeTransform& parent, const RE::NiTransform& local) {
    RelativeTransform result;
    result.rotate = MultiplyRotations(parent.rotate, local.rotate);
    result.translate = Apply(parent, local.translate);
    result.scale = parent.scale * local.scale;
    return result;
}

// Union of every geometry's model bound, expressed in the root's own space. Model bounds are
// filled in by the engine when the mesh loads, so this reads what the NIF authored without
// touching a vertex. Spheres are unioned as boxes - approximate at the corners, exact enough
// for "where is the middle of this thing".
void AccumulateModelBound(RE::NiAVObject* object, const RelativeTransform& toRoot,
                          RE::NiPoint3& min, RE::NiPoint3& max, bool& any) {
    if (!object) return;

    if (auto* geometry = object->AsGeometry(); geometry && !geometry->AsParticlesGeom()) {
        const RE::NiBound& bound = geometry->GetModelData().modelBound;
        if (bound.radius > 0.0f) {
            const RE::NiPoint3 center = Apply(toRoot, bound.center);
            const float extent = bound.radius * toRoot.scale;
            const RE::NiPoint3 low{center.x - extent, center.y - extent, center.z - extent};
            const RE::NiPoint3 high{center.x + extent, center.y + extent, center.z + extent};

            if (!any) {
                min = low;
                max = high;
                any = true;
            } else {
                if (low.x < min.x) min.x = low.x;
                if (low.y < min.y) min.y = low.y;
                if (low.z < min.z) min.z = low.z;
                if (high.x > max.x) max.x = high.x;
                if (high.y > max.y) max.y = high.y;
                if (high.z > max.z) max.z = high.z;
            }
        }
    }

    if (auto* asNode = object->AsNode()) {
        for (auto& child : asNode->GetChildren()) {
            if (!child) continue;
            AccumulateModelBound(child.get(), Compose(toRoot, child->local), min, max, any);
        }
    }
}

}  // namespace
#endif

void GameProjectile::MeasureModelCenter(RE::NiAVObject* node) {
#if !defined(TEST_ENVIRONMENT)
    if (m_modelCenterMeasured || !node) {
        return;
    }

    // The root's own local transform is ours - we write it every frame - so the walk starts
    // from identity and only accumulates what the mesh itself declares.
    RE::NiPoint3 min{}, max{};
    bool any = false;
    AccumulateModelBound(node, RelativeTransform{}, min, max, any);

    // Nothing measurable - the mesh may still be streaming in, so try again next frame. But
    // only a few times: a mesh that genuinely has no measurable geometry (empty.nif, or one
    // whose bounds the engine never fills) would otherwise re-walk its node tree every frame
    // for as long as the element lives.
    if (!any) {
        if (++m_modelCenterAttempts >= MAX_MODEL_CENTER_ATTEMPTS) {
            m_modelCenterMeasured = true;  // latch at zero: nothing to recentre
            spdlog::trace("[CENTER] '{}' has no measurable geometry after {} frames; leaving it "
                "where it is", m_modelPath, m_modelCenterAttempts);
        }
        return;
    }

    m_modelCenterMeasured = true;

    const RE::NiPoint3 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f,
                              (min.z + max.z) * 0.5f};
    const float distance = std::sqrt(center.x * center.x + center.y * center.y +
                                     center.z * center.z);

    // Whether to correct at all is decided against the model's own size, not against a fixed
    // number of units: "is the origin the mesh was authored around" has no absolute scale.
    // The test is whether the geometry's bounding sphere still contains that origin. A mesh
    // authored around its origin does, however large or small it is - orb.nif's assorted
    // billboards average out ~3 units off a ~28 unit radius, which is authored intent and is
    // left alone. A worn armour mesh pressed into service as a ground model does not: the
    // Bandolier sits 97 units up with a 21 unit radius, 4.6x outside its own sphere.
    //
    // Erring towards leaving meshes alone is deliberate. Recentring something that was
    // authored slightly off-origin on purpose is a regression on a mesh nobody complained
    // about; failing to recentre one that is only mildly off costs a barely visible offset.
    const float radius = 0.5f * std::sqrt((max.x - min.x) * (max.x - min.x) +
                                          (max.y - min.y) * (max.y - min.y) +
                                          (max.z - min.z) * (max.z - min.z));

    if (distance <= radius) {
        spdlog::trace("[CENTER] '{}' contains its own origin ({:.1f} off, {:.1f} radius); "
            "leaving it where it is", m_modelPath, distance, radius);
        return;
    }

    m_modelCenter = center;

    // Worth a line at info: an off-centre model is invisible in the code and looks like a
    // layout bug in the headset. A worn armour mesh used as a ground model - the ARMO's world
    // model pointing at an armour addon's _0.nif - is the usual cause, and it shows up here as
    // a Z of roughly chest height. Nothing else in the log names the mesh and the offset
    // together, which is what makes this diagnosable at all.
    spdlog::info("[CENTER] '{}' is {:.1f} units off its origin with a {:.1f} unit radius - "
        "geometry misses the origin entirely, recentring it on the element ({:.1f}, {:.1f}, "
        "{:.1f})", m_modelPath, distance, radius, center.x, center.y, center.z);
#else
    (void)node;
#endif
}

void GameProjectile::UpdateNodeTransform() {
    if (!m_projectile) {
        return;
    }

    auto* node = m_projectile->Get3D();
    if (!node) {
        return;
    }

    MeasureModelCenter(node);

    // Update scale
    float effectiveScale = m_visible ? m_targetTransform.scale : 0.00001f;

    // Update node position. The model's own off-centre-ness is taken back out here rather
    // than at the element: it is a property of the mesh, and every caller that positions an
    // element wants the model to appear where it put it. Rotation and scale apply to the
    // offset too, since that is how the node will draw the geometry.
    RE::NiPoint3 position = m_targetTransform.position;
#if !defined(TEST_ENVIRONMENT)
    if (m_modelCenter.x != 0.0f || m_modelCenter.y != 0.0f || m_modelCenter.z != 0.0f) {
        const RE::NiPoint3 offset =
            RotateAndScale(m_targetTransform.rotation, m_modelCenter, effectiveScale);
        position.x -= offset.x;
        position.y -= offset.y;
        position.z -= offset.z;
    }
#endif

    node->local.translate = position;
    node->world.translate = position;

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

void GameProjectile::SetEffectColor(float r, float g, float b, float a, float glow) {
    m_effectColor[0] = r;
    m_effectColor[1] = g;
    m_effectColor[2] = b;
    m_effectColor[3] = a;
    m_effectGlow = glow;
    m_hasEffectColor = true;
    m_needsColorSet = true;
    m_colorRetryCount = 0;
}

void GameProjectile::ApplyPendingColor() {
#if !defined(TEST_ENVIRONMENT)
    if (!m_needsColorSet) {
        return;
    }

    auto* node = m_projectile ? m_projectile->Get3D() : nullptr;
    if (!node) {
        return;  // Not spawned yet, try again next frame
    }

    ++m_colorRetryCount;
    if (m_colorRetryCount > MAX_TEXTURE_RETRIES) {
        spdlog::error("GameProjectile::ApplyPendingColor - Exceeded {} retries, giving up",
            MAX_TEXTURE_RETRIES);
        m_needsColorSet = false;
        m_colorRetryCount = 0;
        return;
    }

    // Unlike the texture path this walks the whole subtree rather than looking for the
    // icon template's container node: a backdrop mesh is a plain BSFadeNode with the
    // geometry hanging straight off it, so there is no container to find.
    const int applied = TextureManipulator::SetEffectColor(
        node, m_effectColor[0], m_effectColor[1], m_effectColor[2], m_effectColor[3], m_effectGlow);

    if (applied > 0) {
        m_needsColorSet = false;
        m_colorRetryCount = 0;
        spdlog::trace("GameProjectile::ApplyPendingColor - tinted {} geometries", applied);
    }
    // No effect-shader geometry found: the mesh cannot be tinted (a lighting-shader
    // backdrop, say). Retry until the cap, then stop asking.
#else
    m_needsColorSet = false;
    m_colorRetryCount = 0;
#endif
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
