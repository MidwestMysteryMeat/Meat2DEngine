#include "meat2d/input/Input.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace meat2d::input {

void InputState::begin_frame() noexcept {
    pressed_keys_.fill(false);
    released_keys_.fill(false);
    pressed_mouse_buttons_.fill(false);
    released_mouse_buttons_.fill(false);
    mouse_delta_x_ = 0;
    mouse_delta_y_ = 0;
}

void InputState::set_key(Key key, bool down) noexcept {
    if (!valid_key(key)) {
        return;
    }
    const auto index = static_cast<std::size_t>(key);
    if (keys_[index] == down) {
        return;
    }
    keys_[index] = down;
    if (down) {
        pressed_keys_[index] = true;
    } else {
        released_keys_[index] = true;
    }
}

bool InputState::key_down(Key key) const noexcept {
    return valid_key(key) && keys_[static_cast<std::size_t>(key)];
}

bool InputState::key_pressed(Key key) const noexcept {
    return valid_key(key) && pressed_keys_[static_cast<std::size_t>(key)];
}

bool InputState::key_released(Key key) const noexcept {
    return valid_key(key) && released_keys_[static_cast<std::size_t>(key)];
}

void InputState::set_mouse_button(MouseButton button, bool down) noexcept {
    if (!valid_mouse_button(button)) {
        return;
    }
    const auto index = static_cast<std::size_t>(button);
    if (mouse_buttons_[index] == down) {
        return;
    }
    mouse_buttons_[index] = down;
    if (down) {
        pressed_mouse_buttons_[index] = true;
    } else {
        released_mouse_buttons_[index] = true;
    }
}

bool InputState::mouse_down(MouseButton button) const noexcept {
    return valid_mouse_button(button) && mouse_buttons_[static_cast<std::size_t>(button)];
}

bool InputState::mouse_pressed(MouseButton button) const noexcept {
    return valid_mouse_button(button) &&
           pressed_mouse_buttons_[static_cast<std::size_t>(button)];
}

bool InputState::mouse_released(MouseButton button) const noexcept {
    return valid_mouse_button(button) &&
           released_mouse_buttons_[static_cast<std::size_t>(button)];
}

void InputState::set_mouse_position(std::int32_t x, std::int32_t y) noexcept {
    mouse_delta_x_ += x - mouse_x_;
    mouse_delta_y_ += y - mouse_y_;
    mouse_x_ = x;
    mouse_y_ = y;
}

std::int32_t InputState::mouse_x() const noexcept {
    return mouse_x_;
}

std::int32_t InputState::mouse_y() const noexcept {
    return mouse_y_;
}

std::int32_t InputState::mouse_delta_x() const noexcept {
    return mouse_delta_x_;
}

std::int32_t InputState::mouse_delta_y() const noexcept {
    return mouse_delta_y_;
}

bool InputState::valid_key(Key key) noexcept {
    const auto value = static_cast<std::size_t>(key);
    return value > static_cast<std::size_t>(Key::Unknown) && value < key_count;
}

bool InputState::valid_mouse_button(MouseButton button) noexcept {
    return static_cast<std::size_t>(button) < mouse_button_count;
}

ActionId ActionMap::register_action(std::string name) {
    if (name.empty()) {
        return invalid_action;
    }
    if (const auto existing = find_action(name); existing != invalid_action) {
        return existing;
    }
    if (actions_.size() >= static_cast<std::size_t>(std::numeric_limits<ActionId>::max())) {
        return invalid_action;
    }
    actions_.push_back(Action{.name = std::move(name), .bindings = {}});
    return static_cast<ActionId>(actions_.size());
}

ActionId ActionMap::find_action(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < actions_.size(); ++index) {
        if (actions_[index].name == name) {
            return static_cast<ActionId>(index + 1U);
        }
    }
    return invalid_action;
}

std::string_view ActionMap::action_name(ActionId action) const noexcept {
    const auto* value = action_readonly(action);
    return value == nullptr ? std::string_view{} : value->name;
}

bool ActionMap::bind_key(ActionId action, Key key) {
    if (!InputState::valid_key(key)) {
        return false;
    }
    return bind(action, Binding{.type = BindingType::Key,
                                .value = static_cast<std::uint16_t>(key)});
}

bool ActionMap::bind_mouse_button(ActionId action, MouseButton button) {
    if (!InputState::valid_mouse_button(button)) {
        return false;
    }
    return bind(action, Binding{.type = BindingType::MouseButton,
                                .value = static_cast<std::uint16_t>(button)});
}

bool ActionMap::clear_bindings(ActionId action) noexcept {
    auto* value = action_mutable(action);
    if (value == nullptr) {
        return false;
    }
    value->bindings.clear();
    return true;
}

bool ActionMap::down(ActionId action, const InputState& state) const noexcept {
    const auto* value = action_readonly(action);
    if (value == nullptr) {
        return false;
    }
    return std::any_of(value->bindings.begin(), value->bindings.end(),
                       [this, &state](const Binding& binding) {
                           return binding_down(binding, state);
                       });
}

bool ActionMap::pressed(ActionId action, const InputState& state) const noexcept {
    const auto* value = action_readonly(action);
    if (value == nullptr) {
        return false;
    }
    return std::any_of(value->bindings.begin(), value->bindings.end(),
                       [this, &state](const Binding& binding) {
                           return binding_pressed(binding, state);
                       });
}

bool ActionMap::released(ActionId action, const InputState& state) const noexcept {
    const auto* value = action_readonly(action);
    if (value == nullptr) {
        return false;
    }
    return std::any_of(value->bindings.begin(), value->bindings.end(),
                       [this, &state](const Binding& binding) {
                           return binding_released(binding, state);
                       });
}

ActionMap::Action* ActionMap::action_mutable(ActionId action) noexcept {
    if (action == invalid_action || static_cast<std::size_t>(action) > actions_.size()) {
        return nullptr;
    }
    return &actions_[static_cast<std::size_t>(action - 1U)];
}

const ActionMap::Action* ActionMap::action_readonly(ActionId action) const noexcept {
    if (action == invalid_action || static_cast<std::size_t>(action) > actions_.size()) {
        return nullptr;
    }
    return &actions_[static_cast<std::size_t>(action - 1U)];
}

bool ActionMap::bind(ActionId action, Binding binding) {
    auto* value = action_mutable(action);
    if (value == nullptr) {
        return false;
    }
    if (std::find(value->bindings.begin(), value->bindings.end(), binding) != value->bindings.end()) {
        return true;
    }
    value->bindings.push_back(binding);
    return true;
}

bool ActionMap::binding_down(const Binding& binding, const InputState& state) const noexcept {
    if (binding.type == BindingType::Key) {
        return state.key_down(static_cast<Key>(binding.value));
    }
    return state.mouse_down(static_cast<MouseButton>(binding.value));
}

bool ActionMap::binding_pressed(const Binding& binding, const InputState& state) const noexcept {
    if (binding.type == BindingType::Key) {
        return state.key_pressed(static_cast<Key>(binding.value));
    }
    return state.mouse_pressed(static_cast<MouseButton>(binding.value));
}

bool ActionMap::binding_released(const Binding& binding, const InputState& state) const noexcept {
    if (binding.type == BindingType::Key) {
        return state.key_released(static_cast<Key>(binding.value));
    }
    return state.mouse_released(static_cast<MouseButton>(binding.value));
}

} // namespace meat2d::input
