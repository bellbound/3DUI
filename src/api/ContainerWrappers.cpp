#include "WrapperTypes.h"
#include "../log.h"

#include <algorithm>

namespace P3DUI {

// =============================================================================
// Container AddChild Helper
// =============================================================================
// All container types share the same AddChild logic - validate the child,
// check for double-add, delegate to implementation, track in m_children.
// This helper template reduces duplication.

template<typename ContainerT, typename ImplT>
static bool AddChildImpl(
    ContainerT* container,
    Positionable* child,
    std::shared_ptr<ImplT>& impl,
    std::vector<Positionable*>& children,
    const std::string& containerId,
    const char* containerType)
{
    if (!child) return false;

    // Handle Element children
    if (auto* elem = dynamic_cast<ElementWrapper*>(child)) {
        if (elem->IsDestroyed()) {
            spdlog::warn("P3DUI::{}::AddChild: Element '{}' was destroyed", containerType, elem->GetID());
            return false;
        }
        if (elem->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::{}::AddChild: Element '{}' already has a parent", containerType, elem->GetID());
            return false;
        }
        impl->AddChild(elem->GetImpl());
        children.push_back(child);
        spdlog::trace("[{} '{}'] AddChild: Element '{}' (total: {})",
            containerType, containerId, elem->GetID(), children.size());
        return true;
    }

    // Handle Text children
    if (auto* text = dynamic_cast<TextWrapper*>(child)) {
        if (text->IsDestroyed()) {
            spdlog::warn("P3DUI::{}::AddChild: Text '{}' was destroyed", containerType, text->GetID());
            return false;
        }
        if (text->GetImpl()->GetParent() != nullptr) {
            spdlog::warn("P3DUI::{}::AddChild: Text '{}' already has a parent", containerType, text->GetID());
            return false;
        }
        impl->AddChild(text->GetImpl());
        children.push_back(child);
        spdlog::trace("[{} '{}'] AddChild: Text '{}' (total: {})",
            containerType, containerId, text->GetID(), children.size());
        return true;
    }

    // Nested containers could be supported here if needed
    return false;
}

// =============================================================================
// Container Clear Helper
// =============================================================================
// Shared logic for destroying children when a container is cleared.

template<typename ImplT>
static void ClearChildrenImpl(
    std::shared_ptr<ImplT>& impl,
    std::vector<Positionable*>& children,
    const std::string& containerId,
    const char* containerType)
{
    spdlog::trace("[{} '{}'] Clear: removing {} children", containerType, containerId, children.size());

    // Mark children as destroyed (tombstone pattern) then remove from registry
    for (auto* child : children) {
        if (auto* elem = dynamic_cast<ElementWrapper*>(child)) {
            elem->MarkDestroyed();
        } else if (auto* text = dynamic_cast<TextWrapper*>(child)) {
            text->MarkDestroyed();
        }
        WrapperRegistry::Get().Destroy(child);
    }

    impl->Clear();
    children.clear();
}

// =============================================================================
// ScrollWheelWrapper Implementation
// =============================================================================
// Wraps HalfWheelProjectileDriver to implement Container.
// Arranges children in a half-wheel pattern radiating outward from center.
// Commonly used for radial menus with many items.

ScrollWheelWrapper::ScrollWheelWrapper(const ScrollWheelConfig& config)
    : m_id(config.id ? config.id : "")
    , m_impl(std::make_shared<Projectile::HalfWheelProjectileDriver>())
    , m_destroyed(false)
{
    m_impl->SetID(m_id);
    m_impl->SetItemSpacing(config.itemSpacing);
    m_impl->SetRingSpacing(config.ringSpacing);
    if (config.firstRingSpacing > 0.0f) {
        m_impl->SetFirstRingSpacing(config.firstRingSpacing);
    }

    WrapperRegistry::Get().RegisterMapping(m_impl.get(), this);
}

ScrollWheelWrapper::~ScrollWheelWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_impl.get());
}

// --- Positionable ---

const char* ScrollWheelWrapper::GetID() { return m_id.c_str(); }

void ScrollWheelWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLocalPosition({x, y, z});
}

void ScrollWheelWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void ScrollWheelWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void ScrollWheelWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetVisible(visible);
}

bool ScrollWheelWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_impl->IsVisible();
}

Positionable* ScrollWheelWrapper::GetParent() {
    if (m_destroyed) return nullptr;
    auto* parent = m_impl->GetParent();
    return parent ? WrapperRegistry::Get().FindWrapper(parent) : nullptr;
}

// --- Container ---

void ScrollWheelWrapper::AddChild(Positionable* child) {
    if (m_destroyed) return;
    AddChildImpl(this, child, m_impl, m_children, m_id, "ScrollWheel");
}

void ScrollWheelWrapper::SetChildren(Positionable** children, uint32_t count) {
    if (m_destroyed) return;
    if (count > 0 && !children) {
        spdlog::warn("P3DUI::ScrollWheel::SetChildren: null array with count {}", count);
        return;
    }
    spdlog::trace("[ScrollWheel '{}'] SetChildren: count={}", m_id, count);
    Clear();
    for (uint32_t i = 0; i < count; ++i) {
        AddChild(children[i]);
    }
}

void ScrollWheelWrapper::Clear() {
    if (m_destroyed) return;
    ClearChildrenImpl(m_impl, m_children, m_id, "ScrollWheel");
}

uint32_t ScrollWheelWrapper::GetChildCount() {
    if (m_destroyed) return 0;
    return static_cast<uint32_t>(m_children.size());
}

Positionable* ScrollWheelWrapper::GetChildAt(uint32_t index) {
    if (m_destroyed) return nullptr;
    return index < m_children.size() ? m_children[index] : nullptr;
}

void ScrollWheelWrapper::SetUseHapticFeedback(bool enabled) {
    if (m_destroyed) return;
    m_impl->SetUseHapticFeedback(enabled);
}

bool ScrollWheelWrapper::GetUseHapticFeedback() {
    if (m_destroyed) return true;
    return m_impl->GetUseHapticFeedback();
}

// =============================================================================
// WheelWrapper Implementation
// =============================================================================
// Wraps RadialProjectileDriver to implement Container.
// Arranges children in full concentric rings (360°) radiating outward from center.
// Unlike ScrollWheelWrapper (half-wheel with scrolling), this is a static full wheel.

WheelWrapper::WheelWrapper(const WheelConfig& config)
    : m_id(config.id ? config.id : "")
    , m_impl(std::make_shared<Projectile::RadialProjectileDriver>())
    , m_destroyed(false)
{
    m_impl->SetID(m_id);
    m_impl->SetItemSpacing(config.itemSpacing);
    m_impl->SetRingSpacing(config.ringSpacing);

    WrapperRegistry::Get().RegisterMapping(m_impl.get(), this);
}

WheelWrapper::~WheelWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_impl.get());
}

// --- Positionable ---

const char* WheelWrapper::GetID() { return m_id.c_str(); }

void WheelWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLocalPosition({x, y, z});
}

void WheelWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void WheelWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void WheelWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetVisible(visible);
}

bool WheelWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_impl->IsVisible();
}

Positionable* WheelWrapper::GetParent() {
    if (m_destroyed) return nullptr;
    auto* parent = m_impl->GetParent();
    return parent ? WrapperRegistry::Get().FindWrapper(parent) : nullptr;
}

// --- Container ---

void WheelWrapper::AddChild(Positionable* child) {
    if (m_destroyed) return;
    AddChildImpl(this, child, m_impl, m_children, m_id, "Wheel");
}

void WheelWrapper::SetChildren(Positionable** children, uint32_t count) {
    if (m_destroyed) return;
    if (count > 0 && !children) {
        spdlog::warn("P3DUI::Wheel::SetChildren: null array with count {}", count);
        return;
    }
    spdlog::trace("[Wheel '{}'] SetChildren: count={}", m_id, count);
    Clear();
    for (uint32_t i = 0; i < count; ++i) {
        AddChild(children[i]);
    }
}

void WheelWrapper::Clear() {
    if (m_destroyed) return;
    ClearChildrenImpl(m_impl, m_children, m_id, "Wheel");
}

uint32_t WheelWrapper::GetChildCount() {
    if (m_destroyed) return 0;
    return static_cast<uint32_t>(m_children.size());
}

Positionable* WheelWrapper::GetChildAt(uint32_t index) {
    if (m_destroyed) return nullptr;
    return index < m_children.size() ? m_children[index] : nullptr;
}

void WheelWrapper::SetUseHapticFeedback(bool enabled) {
    if (m_destroyed) return;
    m_impl->SetUseHapticFeedback(enabled);
}

bool WheelWrapper::GetUseHapticFeedback() {
    if (m_destroyed) return true;
    return m_impl->GetUseHapticFeedback();
}

// =============================================================================
// ColumnGridWrapper Implementation
// =============================================================================
// Wraps ColumnGridProjectileDriver to implement ScrollableContainer.
// Arranges children in a scrollable multi-row grid (column-major ordering).
// Items fill vertically first (top-to-bottom), then wrap to next column.
// Scrolls horizontally when there are more columns than fit in visible width.

ColumnGridWrapper::ColumnGridWrapper(const ColumnGridConfig& config)
    : m_id(config.id ? config.id : "")
    , m_impl(std::make_shared<Projectile::ColumnGridProjectileDriver>())
    , m_destroyed(false)
{
    m_impl->SetID(m_id);
    m_impl->SetColumnSpacing(config.columnSpacing);
    m_impl->SetRowSpacing(config.rowSpacing);
    m_impl->SetNumRows(config.numRows);
    m_impl->SetVisibleWidth(config.visibleWidth);

    WrapperRegistry::Get().RegisterMapping(m_impl.get(), this);
}

ColumnGridWrapper::~ColumnGridWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_impl.get());
}

// --- Positionable ---

const char* ColumnGridWrapper::GetID() { return m_id.c_str(); }

void ColumnGridWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLocalPosition({x, y, z});
}

void ColumnGridWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void ColumnGridWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void ColumnGridWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetVisible(visible);
}

bool ColumnGridWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_impl->IsVisible();
}

Positionable* ColumnGridWrapper::GetParent() {
    if (m_destroyed) return nullptr;
    auto* parent = m_impl->GetParent();
    return parent ? WrapperRegistry::Get().FindWrapper(parent) : nullptr;
}

// --- Container ---

void ColumnGridWrapper::AddChild(Positionable* child) {
    if (m_destroyed) return;
    AddChildImpl(this, child, m_impl, m_children, m_id, "ColumnGrid");
}

void ColumnGridWrapper::SetChildren(Positionable** children, uint32_t count) {
    if (m_destroyed) return;
    if (count > 0 && !children) {
        spdlog::warn("P3DUI::ColumnGrid::SetChildren: null array with count {}", count);
        return;
    }
    spdlog::trace("[ColumnGrid '{}'] SetChildren: count={}", m_id, count);
    Clear();
    for (uint32_t i = 0; i < count; ++i) {
        AddChild(children[i]);
    }
}

void ColumnGridWrapper::Clear() {
    if (m_destroyed) return;
    ClearChildrenImpl(m_impl, m_children, m_id, "ColumnGrid");
}

uint32_t ColumnGridWrapper::GetChildCount() {
    if (m_destroyed) return 0;
    return static_cast<uint32_t>(m_children.size());
}

Positionable* ColumnGridWrapper::GetChildAt(uint32_t index) {
    if (m_destroyed) return nullptr;
    return index < m_children.size() ? m_children[index] : nullptr;
}

void ColumnGridWrapper::SetUseHapticFeedback(bool enabled) {
    if (m_destroyed) return;
    m_impl->SetUseHapticFeedback(enabled);
}

bool ColumnGridWrapper::GetUseHapticFeedback() {
    if (m_destroyed) return true;
    return m_impl->GetUseHapticFeedback();
}

// --- ScrollableContainer ---

float ColumnGridWrapper::GetScrollPosition() {
    if (m_destroyed || !m_impl->CanScroll()) return 0.0f;

    // Normalize scroll offset to 0.0-1.0 range
    float maxOffset = m_impl->GetMaxScrollOffset();
    if (maxOffset <= 0.0f) return 0.0f;

    return std::clamp(m_impl->GetScrollOffset() / maxOffset, 0.0f, 1.0f);
}

void ColumnGridWrapper::SetScrollPosition(float position) {
    if (m_destroyed) return;

    // Convert normalized position to scroll offset
    float maxOffset = m_impl->GetMaxScrollOffset();
    float targetOffset = std::clamp(position, 0.0f, 1.0f) * maxOffset;

    m_impl->SetScrollOffset(targetOffset);
}

void ColumnGridWrapper::ScrollToChild(uint32_t index) {
    if (m_destroyed) return;
    if (m_children.empty()) return;

    // Clamp index to valid range
    index = (std::min)(index, static_cast<uint32_t>(m_children.size() - 1));

    // Compute the column for this index (column-major ordering)
    size_t numRows = m_impl->GetNumRows();
    size_t column = index / numRows;

    // Compute offset to position this column at the left edge
    float columnSpacing = m_impl->GetColumnSpacing();
    float targetX = column * columnSpacing;

    m_impl->SetScrollOffset(targetX);
}

void ColumnGridWrapper::ResetScroll() {
    if (m_destroyed) return;
    m_impl->SetScrollOffset(0.0f);
}

void ColumnGridWrapper::SetFillDirection(VerticalFill verticalFill, HorizontalFill horizontalFill) {
    if (m_destroyed) return;
    m_impl->SetFillDirection(verticalFill, horizontalFill);
}

void ColumnGridWrapper::SetOrigin(VerticalOrigin verticalOrigin, HorizontalOrigin horizontalOrigin) {
    if (m_destroyed) return;
    m_impl->SetOrigin(verticalOrigin, horizontalOrigin);
}

// =============================================================================
// RowGridWrapper Implementation
// =============================================================================
// Wraps RowGridProjectileDriver to implement ScrollableContainer.
// Arranges children in a scrollable multi-column grid (row-major ordering).
// Items fill horizontally first (left-to-right), then wrap to next row.
// Scrolls vertically when there are more rows than fit in visible height.

RowGridWrapper::RowGridWrapper(const RowGridConfig& config)
    : m_id(config.id ? config.id : "")
    , m_impl(std::make_shared<Projectile::RowGridProjectileDriver>())
    , m_destroyed(false)
{
    m_impl->SetID(m_id);
    m_impl->SetColumnSpacing(config.columnSpacing);
    m_impl->SetRowSpacing(config.rowSpacing);
    m_impl->SetNumColumns(config.numColumns);
    m_impl->SetVisibleHeight(config.visibleHeight);

    WrapperRegistry::Get().RegisterMapping(m_impl.get(), this);
}

RowGridWrapper::~RowGridWrapper() {
    WrapperRegistry::Get().UnregisterMapping(m_impl.get());
}

// --- Positionable ---

const char* RowGridWrapper::GetID() { return m_id.c_str(); }

void RowGridWrapper::SetLocalPosition(float x, float y, float z) {
    if (m_destroyed) return;
    m_impl->SetLocalPosition({x, y, z});
}

void RowGridWrapper::GetLocalPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetLocalPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void RowGridWrapper::GetWorldPosition(float& x, float& y, float& z) {
    if (m_destroyed) { x = y = z = 0; return; }
    auto pos = m_impl->GetWorldPosition();
    x = pos.x; y = pos.y; z = pos.z;
}

void RowGridWrapper::SetVisible(bool visible) {
    if (m_destroyed) return;
    m_impl->SetVisible(visible);
}

bool RowGridWrapper::IsVisible() {
    if (m_destroyed) return false;
    return m_impl->IsVisible();
}

Positionable* RowGridWrapper::GetParent() {
    if (m_destroyed) return nullptr;
    auto* parent = m_impl->GetParent();
    return parent ? WrapperRegistry::Get().FindWrapper(parent) : nullptr;
}

// --- Container ---

void RowGridWrapper::AddChild(Positionable* child) {
    if (m_destroyed) return;
    AddChildImpl(this, child, m_impl, m_children, m_id, "RowGrid");
}

void RowGridWrapper::SetChildren(Positionable** children, uint32_t count) {
    if (m_destroyed) return;
    if (count > 0 && !children) {
        spdlog::warn("P3DUI::RowGrid::SetChildren: null array with count {}", count);
        return;
    }
    spdlog::trace("[RowGrid '{}'] SetChildren: count={}", m_id, count);
    Clear();
    for (uint32_t i = 0; i < count; ++i) {
        AddChild(children[i]);
    }
}

void RowGridWrapper::Clear() {
    if (m_destroyed) return;
    ClearChildrenImpl(m_impl, m_children, m_id, "RowGrid");
}

uint32_t RowGridWrapper::GetChildCount() {
    if (m_destroyed) return 0;
    return static_cast<uint32_t>(m_children.size());
}

Positionable* RowGridWrapper::GetChildAt(uint32_t index) {
    if (m_destroyed) return nullptr;
    return index < m_children.size() ? m_children[index] : nullptr;
}

void RowGridWrapper::SetUseHapticFeedback(bool enabled) {
    if (m_destroyed) return;
    m_impl->SetUseHapticFeedback(enabled);
}

bool RowGridWrapper::GetUseHapticFeedback() {
    if (m_destroyed) return true;
    return m_impl->GetUseHapticFeedback();
}

// --- ScrollableContainer ---

float RowGridWrapper::GetScrollPosition() {
    if (m_destroyed || !m_impl->CanScroll()) return 0.0f;

    // Normalize scroll offset to 0.0-1.0 range
    float maxOffset = m_impl->GetMaxScrollOffset();
    if (maxOffset <= 0.0f) return 0.0f;

    return std::clamp(m_impl->GetScrollOffset() / maxOffset, 0.0f, 1.0f);
}

void RowGridWrapper::SetScrollPosition(float position) {
    if (m_destroyed) return;

    // Convert normalized position to scroll offset
    float maxOffset = m_impl->GetMaxScrollOffset();
    float targetOffset = std::clamp(position, 0.0f, 1.0f) * maxOffset;

    m_impl->SetScrollOffset(targetOffset);
}

void RowGridWrapper::ScrollToChild(uint32_t index) {
    if (m_destroyed) return;
    if (m_children.empty()) return;

    // Clamp index to valid range
    index = (std::min)(index, static_cast<uint32_t>(m_children.size() - 1));

    // Compute the row for this index (row-major ordering)
    size_t numColumns = m_impl->GetNumColumns();
    size_t row = index / numColumns;

    // Compute offset to position this row at the top edge
    float rowSpacing = m_impl->GetRowSpacing();
    float targetZ = row * rowSpacing;

    m_impl->SetScrollOffset(targetZ);
}

void RowGridWrapper::ResetScroll() {
    if (m_destroyed) return;
    m_impl->SetScrollOffset(0.0f);
}

void RowGridWrapper::SetFillDirection(VerticalFill verticalFill, HorizontalFill horizontalFill) {
    if (m_destroyed) return;
    m_impl->SetFillDirection(verticalFill, horizontalFill);
}

void RowGridWrapper::SetOrigin(VerticalOrigin verticalOrigin, HorizontalOrigin horizontalOrigin) {
    if (m_destroyed) return;
    m_impl->SetOrigin(verticalOrigin, horizontalOrigin);
}

} // namespace P3DUI
