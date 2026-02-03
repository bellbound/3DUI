#include "TooltipTextDisplayManager.h"
#include "InteractionController.h"
#include "../util/VRNodes.h"
#include "../log.h"
#include <cmath>
#include <codecvt>
#include <locale>

namespace Widget {

TooltipTextDisplayManager* TooltipTextDisplayManager::GetSingleton() {
    static TooltipTextDisplayManager instance;
    return &instance;
}

void TooltipTextDisplayManager::Initialize() {
    if (m_initialized) {
        return;
    }

    // Create root drivers with text drivers as children
    auto leftText = std::make_shared<Projectile::TextDriver>();
    leftText->SetTextScale(m_textScale);
    leftText->SetAlignment(Projectile::TextAlignment::Center);
    leftText->SetSmoothingSpeed(14);
    m_leftHand.textDriver = leftText.get();
    m_leftHand.root = std::make_unique<Projectile::RootDriver>();

    m_leftHand.root->SetLocalRotation(90.0f, 0.0f, 0.0f);
    m_leftHand.root->SetTransitionMode(Projectile::TransitionMode::Instant);
    m_leftHand.root->AddChild(leftText);
    // RootDriver defaults to hidden - SetVisible(true) will lazily initialize

    auto rightText = std::make_shared<Projectile::TextDriver>();
    rightText->SetTextScale(m_textScale);
    rightText->SetTransitionMode(Projectile::TransitionMode::Lerp);
    rightText->SetSmoothingSpeed(15);
    rightText->SetAlignment(Projectile::TextAlignment::Center);
    m_rightHand.textDriver = rightText.get();
    m_rightHand.root = std::make_unique<Projectile::RootDriver>();
    m_rightHand.root->SetLocalRotation(90.0f, 0.0f, 0.0f);
    m_rightHand.root->SetTransitionMode(Projectile::TransitionMode::Instant);
    m_rightHand.root->AddChild(rightText);
    // RootDriver defaults to hidden - SetVisible(true) will lazily initialize

    m_initialized = true;
    spdlog::info("TooltipTextDisplayManager::Initialize - SUCCESS. Left root={:p}, Right root={:p}",
        (void*)m_leftHand.root.get(), (void*)m_rightHand.root.get());
}

void TooltipTextDisplayManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_leftHand.Clear();
    m_rightHand.Clear();

    m_leftHand.textDriver = nullptr;
    m_rightHand.textDriver = nullptr;
    m_leftHand.root.reset();
    m_rightHand.root.reset();

    m_initialized = false;

    spdlog::info("TooltipTextDisplayManager shutdown");
}

void TooltipTextDisplayManager::ShowTooltip(bool isLeft, const std::string& id, const std::wstring& text) {
    if (!m_initialized) {
        Initialize();
    }

    auto& state = GetHandState(isLeft);
    if (!state.textDriver) {
        spdlog::error("TooltipTextDisplayManager::ShowTooltip - TextDriver is null for {} hand!",
            isLeft ? "left" : "right");
        return;
    }

    // Convert wide string to narrow for logging
    std::string narrowText;
    narrowText.reserve(text.length());
    for (wchar_t ch : text) {
        narrowText.push_back(static_cast<char>(ch <= 127 ? ch : '?'));
    }

    spdlog::info("TooltipTextDisplayManager::ShowTooltip - {} hand, id='{}', text='{}' (len={})",
        isLeft ? "left" : "right", id, narrowText, text.length());

    // Update id, text and show
    state.currentId = id;
    state.currentText = text;
    state.textDriver->SetText(text);
    state.root->SetVisible(true);
    state.visible = true;
}

void TooltipTextDisplayManager::ShowTooltip(bool isLeft, const std::string& id, const std::string& text) {
    // Convert narrow to wide string
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    ShowTooltip(isLeft, id, converter.from_bytes(text));
}

void TooltipTextDisplayManager::HideTooltip(bool isLeft, const std::string& id) {
    if (!m_initialized) {
        return;
    }

    auto& state = GetHandState(isLeft);

    // Only hide if the id matches (prevents race conditions)
    if (state.currentId == id) {
        spdlog::info("TooltipTextDisplayManager::HideTooltip - {} hand, id='{}' MATCHES current, hiding",
            isLeft ? "left" : "right", id);
        ForceHideTooltip(isLeft);
    } else {
        spdlog::info("TooltipTextDisplayManager::HideTooltip - {} hand, id='{}' != current='{}', SKIPPED",
            isLeft ? "left" : "right", id, state.currentId);
    }
}

void TooltipTextDisplayManager::ForceHideTooltip(bool isLeft) {
    if (!m_initialized) {
        return;
    }

    auto& state = GetHandState(isLeft);
    if (state.root) {
        state.root->SetVisible(false);
    }
    state.currentId.clear();
    state.currentText.clear();
    state.visible = false;

    spdlog::trace("TooltipTextDisplayManager: Hidden tooltip on {} hand",
        isLeft ? "left" : "right");
}

void TooltipTextDisplayManager::HideAll() {
    ForceHideTooltip(true);
    ForceHideTooltip(false);
}

void TooltipTextDisplayManager::SetTextScale(float scale) {
    m_textScale = scale;

    if (m_leftHand.textDriver) {
        m_leftHand.textDriver->SetTextScale(scale);
    }
    if (m_rightHand.textDriver) {
        m_rightHand.textDriver->SetTextScale(scale);
    }
}

bool TooltipTextDisplayManager::IsTooltipVisible(bool isLeft) const {
    return GetHandState(isLeft).visible;
}

const std::wstring& TooltipTextDisplayManager::GetCurrentText(bool isLeft) const {
    static std::wstring empty;
    const auto& state = GetHandState(isLeft);
    return state.visible ? state.currentText : empty;
}

void TooltipTextDisplayManager::Update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    UpdateHandTooltip(true, deltaTime);
    UpdateHandTooltip(false, deltaTime);
}

void TooltipTextDisplayManager::UpdateHandTooltip(bool isLeft, float deltaTime) {
    auto& state = GetHandState(isLeft);
    if (!state.root || !state.visible) {
        return;
    }

    // Get wrist node (forearm twist bone)
    static RE::BSFixedString leftWristName("NPC L ForearmTwist1 [LLt1]");
    static RE::BSFixedString rightWristName("NPC R ForearmTwist1 [RLt1]");
    RE::NiAVObject* wristNode = VRNodes::GetPlayerNode(isLeft ? leftWristName : rightWristName);
    if (!wristNode) {
        spdlog::warn("TooltipTextDisplayManager::UpdateHandTooltip - {} wrist node is null!",
            isLeft ? "left" : "right");
        return;
    }

    // Compute position: wrist position + offset
    // X/Y offset is rotated by wrist orientation, but Z is always world-space (unaffected by wrist rotation)
    RE::NiPoint3 wristPos = wristNode->world.translate;
    RE::NiMatrix3 wristRot = wristNode->world.rotate;

    // Transform X/Y offset from wrist-local to world space (only using X/Y components of offset)
    RE::NiPoint3 worldOffset;
    worldOffset.x = wristRot.entry[0][0] * m_offset.x + wristRot.entry[0][1] * m_offset.y;
    worldOffset.y = wristRot.entry[1][0] * m_offset.x + wristRot.entry[1][1] * m_offset.y;
    // Z is always world-space: hand Z + offset Z (not affected by wrist rotation)
    worldOffset.z = m_offset.z;

    RE::NiPoint3 tooltipPos = wristPos + worldOffset;

    // Move tooltip towards HMD by m_towardsPlayerDistance
    if (m_towardsPlayerDistance > 0.0f) {
        if (auto* hmd = VRNodes::GetHMD()) {
            RE::NiPoint3 hmdPos = hmd->world.translate;
            RE::NiPoint3 toHmd = hmdPos - tooltipPos;
            float distance = std::sqrt(toHmd.x * toHmd.x + toHmd.y * toHmd.y + toHmd.z * toHmd.z);
            if (distance > 0.001f) {
                // Normalize and scale by m_towardsPlayerDistance
                RE::NiPoint3 dir = toHmd * (1.0f / distance);
                tooltipPos = tooltipPos + dir * m_towardsPlayerDistance;
            }
        }
    }

    static int s_updateCount = 0;
  

    // Set the tooltip position on root
    state.root->SetCenter(tooltipPos);
}

} // namespace Widget
