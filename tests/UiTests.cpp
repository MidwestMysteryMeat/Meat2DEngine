#include "TestSupport.hpp"

#include "meat2d/render/Ui.hpp"

#include <string>

namespace meat2d_tests {

void test_ui_context() {
    meat2d::ui::Context ui;
    const auto root = ui.create_widget(meat2d::ui::WidgetKind::Panel,
                                       meat2d::ui::invalid_widget,
                                       {.x = 0, .y = 0, .width = 160, .height = 100});
    check(root.has_value(), "UI root creation failed");
    const auto label = ui.create_widget(meat2d::ui::WidgetKind::Label,
                                        *root,
                                        {.x = 0, .y = 0, .width = 100, .height = 10},
                                        "Options");
    const auto button = ui.create_widget(meat2d::ui::WidgetKind::Button,
                                         *root,
                                         {.x = 0, .y = 0, .width = 100, .height = 10},
                                         "Start");
    const auto checkbox = ui.create_widget(meat2d::ui::WidgetKind::CheckBox,
                                            *root,
                                            {.x = 0, .y = 0, .width = 100, .height = 10},
                                            "Fullscreen");
    check(label.has_value() && button.has_value() && checkbox.has_value(),
          "UI child creation failed");
    check(ui.focused_widget() == *button, "UI did not focus first actionable widget");

    check(ui.handle_navigation(meat2d::ui::Navigation::Next), "UI next navigation failed");
    check(ui.focused_widget() == *checkbox, "UI next navigation selected wrong widget");
    check(ui.handle_navigation(meat2d::ui::Navigation::Activate), "UI checkbox activation failed");
    check(ui.events().size() == 1U, "UI checkbox emitted wrong event count");
    check(ui.events()[0].widget == *checkbox, "UI checkbox event selected wrong widget");
    check(ui.events()[0].type == meat2d::ui::EventType::Changed, "UI checkbox event type is wrong");
    check(ui.events()[0].value, "UI checkbox did not toggle on");

    ui.begin_frame();
    check(ui.events().empty(), "UI begin_frame did not clear events");
    check(ui.handle_navigation(meat2d::ui::Navigation::Previous), "UI previous navigation failed");
    check(ui.focused_widget() == *button, "UI previous navigation selected wrong widget");
    check(ui.pointer_activate(2, 1), "UI pointer activation failed");
    check(ui.events().size() == 1U, "UI button emitted wrong event count");
    check(ui.events()[0].widget == *button, "UI button event selected wrong widget");
    check(ui.events()[0].type == meat2d::ui::EventType::Activated, "UI button event type is wrong");

    check(ui.layout_vertical(*root, 10, 20, 120, 2), "UI vertical layout failed");
    const auto commands = ui.draw_commands();
    check(commands.size() == 4U, "UI draw command count is wrong");
    check(commands[0].widget == *root, "UI root draw command is missing");
    check(commands[1].widget == *label, "UI label draw command is missing");
    check(commands[1].bounds.y == 20, "UI label layout is wrong");
    check(commands[2].bounds.y == 32, "UI button layout is wrong");
    check(commands[3].bounds.y == 44, "UI checkbox layout is wrong");
    check(commands[3].text == "Fullscreen", "UI checkbox text is wrong");

    check(!ui.set_text(*button, std::string(meat2d::ui::maximum_text_bytes + 1U, 'x')),
          "UI accepted over-limit text");
    check(ui.set_enabled(*button, false), "UI button disable failed");
    check(ui.focused_widget() == *checkbox, "UI focus did not move after disable");
    check(ui.set_visible(*checkbox, false), "UI checkbox hide failed");
    check(ui.focused_widget() == meat2d::ui::invalid_widget, "UI focus did not clear after hide");
    check(!ui.pointer_move(12, 45), "UI focused hidden widget from pointer input");
    check(ui.remove_widget(*label), "UI label removal failed");
    check(!ui.exists(*label), "UI removed label still exists");
    check(!ui.remove_widget(*root), "UI removed parent with live children");

    meat2d::ui::Context bounded;
    for (std::size_t index = 0; index < meat2d::ui::maximum_widgets; ++index) {
        check(bounded.create_widget(meat2d::ui::WidgetKind::Panel).has_value(),
              "UI bounded widget capacity rejected a valid widget");
    }
    check(!bounded.create_widget(meat2d::ui::WidgetKind::Panel).has_value(),
          "UI exceeded bounded widget capacity");
}

} // namespace meat2d_tests
