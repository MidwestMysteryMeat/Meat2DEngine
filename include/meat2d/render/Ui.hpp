#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::ui {

using WidgetId = std::uint32_t;
inline constexpr WidgetId invalid_widget = 0;
inline constexpr std::size_t maximum_widgets = 1024U;
inline constexpr std::size_t maximum_text_bytes = 4096U;

struct Rect {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] bool contains(std::int32_t point_x, std::int32_t point_y) const noexcept;
};

enum class WidgetKind : std::uint8_t { Panel, Label, Button, CheckBox };
enum class Navigation : std::uint8_t { Next, Previous, Activate, Cancel };
enum class EventType : std::uint8_t { Activated, Changed, Cancelled };

struct UiEvent {
    WidgetId widget{invalid_widget};
    EventType type{EventType::Activated};
    bool value{};
};

struct DrawCommand {
    WidgetId widget{invalid_widget};
    WidgetKind kind{WidgetKind::Panel};
    Rect bounds{};
    bool focused{};
    bool checked{};
    std::string text;
};

class Context {
  public:
    Context();

    void begin_frame() noexcept;

    [[nodiscard]] std::optional<WidgetId> create_widget(
        WidgetKind kind,
        WidgetId parent = invalid_widget,
        Rect bounds = {},
        std::string_view text = {});
    bool remove_widget(WidgetId widget) noexcept;
    bool set_bounds(WidgetId widget, Rect bounds) noexcept;
    bool set_text(WidgetId widget, std::string_view text);
    bool set_enabled(WidgetId widget, bool enabled) noexcept;
    bool set_visible(WidgetId widget, bool visible) noexcept;
    bool set_checked(WidgetId widget, bool checked) noexcept;

    [[nodiscard]] bool handle_navigation(Navigation navigation) noexcept;
    [[nodiscard]] bool pointer_move(std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] bool pointer_activate(std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] bool layout_vertical(WidgetId parent,
                                       std::int32_t x,
                                       std::int32_t y,
                                       std::int32_t width,
                                       std::int32_t gap) noexcept;
    [[nodiscard]] std::span<const UiEvent> events() const noexcept;
    [[nodiscard]] std::span<const DrawCommand> draw_commands();
    [[nodiscard]] WidgetId focused_widget() const noexcept;
    [[nodiscard]] bool exists(WidgetId widget) const noexcept;

  private:
    struct Widget {
        WidgetId id{invalid_widget};
        WidgetId parent{invalid_widget};
        WidgetKind kind{WidgetKind::Panel};
        Rect bounds{};
        std::string text;
        bool enabled{true};
        bool visible{true};
        bool checked{};
        bool alive{true};
    };

    [[nodiscard]] Widget* find(WidgetId widget) noexcept;
    [[nodiscard]] const Widget* find(WidgetId widget) const noexcept;
    [[nodiscard]] static bool focusable(const Widget& widget) noexcept;
    [[nodiscard]] bool focus_widget(WidgetId widget) noexcept;
    [[nodiscard]] bool activate_widget(WidgetId widget) noexcept;
    [[nodiscard]] WidgetId next_focus(WidgetId current, bool forward) const noexcept;
    [[nodiscard]] bool valid_bounds(Rect bounds) noexcept;
    void append_event(WidgetId widget, EventType type, bool value) noexcept;

    std::vector<Widget> widgets_;
    std::vector<UiEvent> events_;
    std::vector<DrawCommand> draw_commands_;
    WidgetId focused_{invalid_widget};
    WidgetId next_id_{1};
};

} // namespace meat2d::ui
