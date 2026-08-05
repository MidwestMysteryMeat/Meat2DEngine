#include "meat2d/render/Ui.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace meat2d::ui {

bool Rect::contains(std::int32_t point_x, std::int32_t point_y) const noexcept {
    const auto right = static_cast<std::int64_t>(x) + width;
    const auto bottom = static_cast<std::int64_t>(y) + height;
    return static_cast<std::int64_t>(point_x) >= x &&
           static_cast<std::int64_t>(point_y) >= y &&
           static_cast<std::int64_t>(point_x) < right &&
           static_cast<std::int64_t>(point_y) < bottom;
}

Context::Context() {
    widgets_.reserve(maximum_widgets);
    events_.reserve(maximum_widgets);
    draw_commands_.reserve(maximum_widgets);
}

void Context::begin_frame() noexcept {
    events_.clear();
    draw_commands_.clear();
}

std::optional<WidgetId> Context::create_widget(WidgetKind kind,
                                               WidgetId parent,
                                               Rect bounds,
                                               std::string_view text) {
    if (widgets_.size() >= maximum_widgets ||
        (parent != invalid_widget && !exists(parent)) || !valid_bounds(bounds) ||
        text.size() > maximum_text_bytes || next_id_ == invalid_widget) {
        return std::nullopt;
    }
    try {
        const auto id = next_id_++;
        Widget widget{};
        widget.id = id;
        widget.parent = parent;
        widget.kind = kind;
        widget.bounds = bounds;
        widget.text.assign(text.begin(), text.end());
        widgets_.push_back(std::move(widget));
        if (focused_ == invalid_widget && focusable(widgets_.back())) {
            focused_ = id;
        }
        return id;
    } catch (...) {
        return std::nullopt;
    }
}

bool Context::remove_widget(WidgetId widget) noexcept {
    auto* target = find(widget);
    if (target == nullptr) {
        return false;
    }
    const auto child = std::find_if(widgets_.begin(), widgets_.end(), [widget](const Widget& item) {
        return item.alive && item.parent == widget;
    });
    if (child != widgets_.end()) {
        return false;
    }
    target->alive = false;
    target->visible = false;
    if (focused_ == widget) {
        focused_ = next_focus(widget, true);
    }
    return true;
}

bool Context::set_bounds(WidgetId widget, Rect bounds) noexcept {
    auto* target = find(widget);
    if (target == nullptr || !valid_bounds(bounds)) {
        return false;
    }
    target->bounds = bounds;
    return true;
}

bool Context::set_text(WidgetId widget, std::string_view text) {
    auto* target = find(widget);
    if (target == nullptr || text.size() > maximum_text_bytes) {
        return false;
    }
    try {
        target->text.assign(text.begin(), text.end());
        return true;
    } catch (...) {
        return false;
    }
}

bool Context::set_enabled(WidgetId widget, bool enabled) noexcept {
    auto* target = find(widget);
    if (target == nullptr) {
        return false;
    }
    target->enabled = enabled;
    if (!enabled && focused_ == widget) {
        focused_ = next_focus(widget, true);
    }
    return true;
}

bool Context::set_visible(WidgetId widget, bool visible) noexcept {
    auto* target = find(widget);
    if (target == nullptr) {
        return false;
    }
    target->visible = visible;
    if (!visible && focused_ == widget) {
        focused_ = next_focus(widget, true);
    }
    return true;
}

bool Context::set_checked(WidgetId widget, bool checked) noexcept {
    auto* target = find(widget);
    if (target == nullptr || target->kind != WidgetKind::CheckBox) {
        return false;
    }
    target->checked = checked;
    return true;
}

bool Context::handle_navigation(Navigation navigation) noexcept {
    switch (navigation) {
    case Navigation::Next:
        return focus_widget(next_focus(focused_, true));
    case Navigation::Previous:
        return focus_widget(next_focus(focused_, false));
    case Navigation::Activate:
        return activate_widget(focused_);
    case Navigation::Cancel:
        if (focused_ == invalid_widget) {
            return false;
        }
        append_event(focused_, EventType::Cancelled, false);
        return true;
    }
    return false;
}

bool Context::pointer_move(std::int32_t x, std::int32_t y) noexcept {
    for (const auto& widget : widgets_) {
        if (widget.alive && widget.visible && focusable(widget) &&
            widget.bounds.contains(x, y)) {
            return focus_widget(widget.id);
        }
    }
    return false;
}

bool Context::pointer_activate(std::int32_t x, std::int32_t y) noexcept {
    return pointer_move(x, y) && activate_widget(focused_);
}

bool Context::layout_vertical(WidgetId parent,
                              std::int32_t x,
                              std::int32_t y,
                              std::int32_t width,
                              std::int32_t gap) noexcept {
    if (parent != invalid_widget && !exists(parent)) {
        return false;
    }
    if (width < 0 || gap < 0) {
        return false;
    }
    std::int32_t cursor = y;
    for (std::size_t index = 0; index < widgets_.size(); ++index) {
        auto& widget = widgets_[index];
        if (!widget.alive || widget.parent != parent) {
            continue;
        }
        if (widget.bounds.height < 0 || cursor > std::numeric_limits<std::int32_t>::max() -
                                             widget.bounds.height) {
            return false;
        }
        widget.bounds = {.x = x, .y = cursor, .width = width, .height = widget.bounds.height};
        cursor += widget.bounds.height;
        bool has_following_child = false;
        for (std::size_t following = index + 1U; following < widgets_.size(); ++following) {
            if (widgets_[following].alive && widgets_[following].parent == parent) {
                has_following_child = true;
                break;
            }
        }
        if (has_following_child) {
            if (cursor > std::numeric_limits<std::int32_t>::max() - gap) {
                return false;
            }
            cursor += gap;
        }
    }
    return true;
}

std::span<const UiEvent> Context::events() const noexcept {
    return events_;
}

std::span<const DrawCommand> Context::draw_commands() {
    draw_commands_.clear();
    try {
        draw_commands_.reserve(widgets_.size());
        for (const auto& widget : widgets_) {
            if (!widget.alive || !widget.visible) {
                continue;
            }
            draw_commands_.push_back({
                .widget = widget.id,
                .kind = widget.kind,
                .bounds = widget.bounds,
                .focused = widget.id == focused_,
                .checked = widget.checked,
                .text = widget.text,
            });
        }
    } catch (...) {
        draw_commands_.clear();
    }
    return draw_commands_;
}

WidgetId Context::focused_widget() const noexcept {
    return focused_;
}

bool Context::exists(WidgetId widget) const noexcept {
    return find(widget) != nullptr;
}

Context::Widget* Context::find(WidgetId widget) noexcept {
    const auto iterator = std::find_if(widgets_.begin(), widgets_.end(), [widget](const Widget& item) {
        return item.alive && item.id == widget;
    });
    return iterator == widgets_.end() ? nullptr : &*iterator;
}

const Context::Widget* Context::find(WidgetId widget) const noexcept {
    const auto iterator = std::find_if(widgets_.begin(), widgets_.end(), [widget](const Widget& item) {
        return item.alive && item.id == widget;
    });
    return iterator == widgets_.end() ? nullptr : &*iterator;
}

bool Context::focusable(const Widget& widget) noexcept {
    return widget.alive && widget.visible && widget.enabled &&
           (widget.kind == WidgetKind::Button || widget.kind == WidgetKind::CheckBox);
}

bool Context::focus_widget(WidgetId widget) noexcept {
    if (widget == invalid_widget) {
        return false;
    }
    const auto* target = find(widget);
    if (target == nullptr || !focusable(*target)) {
        return false;
    }
    focused_ = widget;
    return true;
}

bool Context::activate_widget(WidgetId widget) noexcept {
    auto* target = find(widget);
    if (target == nullptr || !focusable(*target)) {
        return false;
    }
    if (target->kind == WidgetKind::CheckBox) {
        target->checked = !target->checked;
        append_event(widget, EventType::Changed, target->checked);
    } else {
        append_event(widget, EventType::Activated, true);
    }
    return true;
}

WidgetId Context::next_focus(WidgetId current, bool forward) const noexcept {
    if (widgets_.empty()) {
        return invalid_widget;
    }

    std::size_t current_index = widgets_.size();
    for (std::size_t index = 0; index < widgets_.size(); ++index) {
        if (widgets_[index].id == current) {
            current_index = index;
            break;
        }
    }

    if (current_index == widgets_.size()) {
        if (forward) {
            for (const auto& widget : widgets_) {
                if (focusable(widget)) {
                    return widget.id;
                }
            }
        } else {
            for (auto iterator = widgets_.rbegin(); iterator != widgets_.rend(); ++iterator) {
                if (focusable(*iterator)) {
                    return iterator->id;
                }
            }
        }
        return invalid_widget;
    }

    for (std::size_t step = 1U; step <= widgets_.size(); ++step) {
        const auto index = forward
                               ? (current_index + step) % widgets_.size()
                               : (current_index + widgets_.size() - step) % widgets_.size();
        if (focusable(widgets_[index])) {
            return widgets_[index].id;
        }
    }
    return invalid_widget;
}

bool Context::valid_bounds(Rect bounds) noexcept {
    return bounds.width >= 0 && bounds.height >= 0;
}

void Context::append_event(WidgetId widget, EventType type, bool value) noexcept {
    if (events_.size() < maximum_widgets) {
        events_.push_back({.widget = widget, .type = type, .value = value});
    }
}

} // namespace meat2d::ui
