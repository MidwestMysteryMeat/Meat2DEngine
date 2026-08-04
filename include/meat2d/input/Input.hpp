#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::input {

enum class Key : std::uint16_t {
    Unknown,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Left,
    Right,
    Up,
    Down,
    Space,
    Escape,
    Enter,
    Tab,
    Shift,
    Control,
    Alt,
    Count,
};

enum class MouseButton : std::uint8_t { Left, Middle, Right, Count };

enum class GamepadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    LeftShoulder,
    RightShoulder,
    Back,
    Start,
    Guide,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count,
};

enum class GamepadAxis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

inline constexpr std::size_t key_count = static_cast<std::size_t>(Key::Count);
inline constexpr std::size_t mouse_button_count = static_cast<std::size_t>(MouseButton::Count);
inline constexpr std::size_t gamepad_button_count = static_cast<std::size_t>(GamepadButton::Count);
inline constexpr std::size_t gamepad_axis_count = static_cast<std::size_t>(GamepadAxis::Count);
inline constexpr std::size_t maximum_gamepads = 4U;
inline constexpr std::size_t maximum_touches = 10U;
using ActionId = std::uint16_t;
inline constexpr ActionId invalid_action = 0;

class InputState {
  public:
    void begin_frame() noexcept;

    void set_key(Key key, bool down) noexcept;
    [[nodiscard]] bool key_down(Key key) const noexcept;
    [[nodiscard]] bool key_pressed(Key key) const noexcept;
    [[nodiscard]] bool key_released(Key key) const noexcept;

    void set_mouse_button(MouseButton button, bool down) noexcept;
    [[nodiscard]] bool mouse_down(MouseButton button) const noexcept;
    [[nodiscard]] bool mouse_pressed(MouseButton button) const noexcept;
    [[nodiscard]] bool mouse_released(MouseButton button) const noexcept;

    void set_gamepad_button(std::uint8_t gamepad, GamepadButton button, bool down) noexcept;
    [[nodiscard]] bool gamepad_button_down(std::uint8_t gamepad,
                                            GamepadButton button) const noexcept;
    [[nodiscard]] bool gamepad_button_pressed(std::uint8_t gamepad,
                                              GamepadButton button) const noexcept;
    [[nodiscard]] bool gamepad_button_released(std::uint8_t gamepad,
                                               GamepadButton button) const noexcept;

    void set_gamepad_axis(std::uint8_t gamepad, GamepadAxis axis, std::int16_t value) noexcept;
    [[nodiscard]] std::int16_t gamepad_axis(std::uint8_t gamepad,
                                            GamepadAxis axis) const noexcept;

    void set_touch(std::uint8_t touch, std::uint64_t id, bool down, std::int32_t x,
                   std::int32_t y) noexcept;
    [[nodiscard]] bool touch_down(std::uint8_t touch) const noexcept;
    [[nodiscard]] bool touch_pressed(std::uint8_t touch) const noexcept;
    [[nodiscard]] bool touch_released(std::uint8_t touch) const noexcept;
    [[nodiscard]] std::uint64_t touch_id(std::uint8_t touch) const noexcept;
    [[nodiscard]] std::int32_t touch_x(std::uint8_t touch) const noexcept;
    [[nodiscard]] std::int32_t touch_y(std::uint8_t touch) const noexcept;
    [[nodiscard]] std::int32_t touch_delta_x(std::uint8_t touch) const noexcept;
    [[nodiscard]] std::int32_t touch_delta_y(std::uint8_t touch) const noexcept;

    void set_mouse_position(std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] std::int32_t mouse_x() const noexcept;
    [[nodiscard]] std::int32_t mouse_y() const noexcept;
    [[nodiscard]] std::int32_t mouse_delta_x() const noexcept;
    [[nodiscard]] std::int32_t mouse_delta_y() const noexcept;

  private:
    friend class ActionMap;

    [[nodiscard]] static bool valid_key(Key key) noexcept;
    [[nodiscard]] static bool valid_mouse_button(MouseButton button) noexcept;
    [[nodiscard]] static bool valid_gamepad_button(GamepadButton button) noexcept;
    [[nodiscard]] static bool valid_gamepad_axis(GamepadAxis axis) noexcept;
    [[nodiscard]] static bool valid_gamepad(std::uint8_t gamepad) noexcept;
    [[nodiscard]] static bool valid_touch(std::uint8_t touch) noexcept;

    std::array<bool, key_count> keys_{};
    std::array<bool, key_count> pressed_keys_{};
    std::array<bool, key_count> released_keys_{};
    std::array<bool, mouse_button_count> mouse_buttons_{};
    std::array<bool, mouse_button_count> pressed_mouse_buttons_{};
    std::array<bool, mouse_button_count> released_mouse_buttons_{};
    std::array<std::array<bool, gamepad_button_count>, maximum_gamepads> gamepad_buttons_{};
    std::array<std::array<bool, gamepad_button_count>, maximum_gamepads> pressed_gamepad_buttons_{};
    std::array<std::array<bool, gamepad_button_count>, maximum_gamepads>
        released_gamepad_buttons_{};
    std::array<std::array<std::int16_t, gamepad_axis_count>, maximum_gamepads> gamepad_axes_{};
    std::array<bool, maximum_touches> touches_{};
    std::array<bool, maximum_touches> pressed_touches_{};
    std::array<bool, maximum_touches> released_touches_{};
    std::array<std::uint64_t, maximum_touches> touch_ids_{};
    std::array<std::int32_t, maximum_touches> touch_x_{};
    std::array<std::int32_t, maximum_touches> touch_y_{};
    std::array<std::int32_t, maximum_touches> touch_delta_x_{};
    std::array<std::int32_t, maximum_touches> touch_delta_y_{};
    std::int32_t mouse_x_{};
    std::int32_t mouse_y_{};
    std::int32_t mouse_delta_x_{};
    std::int32_t mouse_delta_y_{};
};

class ActionMap {
  public:
    [[nodiscard]] ActionId register_action(std::string name);
    [[nodiscard]] ActionId find_action(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view action_name(ActionId action) const noexcept;

    bool bind_key(ActionId action, Key key);
    bool bind_mouse_button(ActionId action, MouseButton button);
    bool bind_gamepad_button(ActionId action, std::uint8_t gamepad, GamepadButton button);
    bool bind_touch(ActionId action, std::uint8_t touch);
    bool clear_bindings(ActionId action) noexcept;

    [[nodiscard]] bool down(ActionId action, const InputState& state) const noexcept;
    [[nodiscard]] bool pressed(ActionId action, const InputState& state) const noexcept;
    [[nodiscard]] bool released(ActionId action, const InputState& state) const noexcept;

  private:
    enum class BindingType : std::uint8_t { Key, MouseButton, GamepadButton, Touch };
    struct Binding {
        BindingType type{};
        std::uint16_t value{};
        std::uint8_t device{};

        friend bool operator==(Binding, Binding) = default;
    };
    struct Action {
        std::string name;
        std::vector<Binding> bindings;
    };

    [[nodiscard]] Action* action_mutable(ActionId action) noexcept;
    [[nodiscard]] const Action* action_readonly(ActionId action) const noexcept;
    bool bind(ActionId action, Binding binding);
    [[nodiscard]] bool binding_down(const Binding& binding, const InputState& state) const noexcept;
    [[nodiscard]] bool binding_pressed(const Binding& binding,
                                       const InputState& state) const noexcept;
    [[nodiscard]] bool binding_released(const Binding& binding,
                                        const InputState& state) const noexcept;

    std::vector<Action> actions_;
};

} // namespace meat2d::input
