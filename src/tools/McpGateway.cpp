#include "meat2d/tools/McpGateway.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>

namespace meat2d::tools {
namespace {

std::string lower_copy(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

bool contains_case_insensitive(std::string_view value, std::string_view query) {
    return lower_copy(value).find(lower_copy(query)) != std::string::npos;
}

std::string safe_field(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (character == '\n' || character == '\r' || character == ';') {
            result.push_back('_');
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::optional<scene::EntityId> parse_entity_parameter(std::string_view parameters) {
    constexpr std::string_view prefix = "entity=";
    if (!parameters.starts_with(prefix) || parameters.size() == prefix.size()) {
        return std::nullopt;
    }
    scene::EntityId entity{};
    const auto* begin = parameters.data() + prefix.size();
    const auto* end = parameters.data() + parameters.size();
    const auto parsed = std::from_chars(begin, end, entity);
    if (parsed.ec != std::errc{} || parsed.ptr != end || entity == scene::invalid_entity) {
        return std::nullopt;
    }
    return entity;
}

McpResponse success_response(std::string message, std::string payload = {}) {
    return {.success = true, .code = "ok", .message = std::move(message),
            .payload = std::move(payload), .tools = {}};
}

} // namespace

McpGateway::McpGateway(SceneEditor& editor, std::string capability_token)
    : editor_(editor), capability_token_(std::move(capability_token)) {
    tools_ = {
        {.name = "scene",
         .description = "Inspect and safely edit the active scene through editor history.",
         .permission = McpPermission::Write,
         .actions = {"inspect", "list_entities", "select", "clear_selection", "undo", "redo"}},
    };
}

bool McpGateway::constant_time_equal(std::string_view left, std::string_view right) noexcept {
    const auto length = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0; index < length; ++index) {
        const auto left_value = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
        const auto right_value = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
        difference |= static_cast<std::size_t>(left_value ^ right_value);
    }
    return difference == 0U;
}

bool McpGateway::authenticate(std::string_view token) const noexcept {
    return !capability_token_.empty() && constant_time_equal(capability_token_, token);
}

McpToolDescriptor* McpGateway::find_tool(std::string_view name) noexcept {
    const auto found = std::find_if(tools_.begin(), tools_.end(),
                                    [name](const McpToolDescriptor& tool) {
                                        return tool.name == name;
                                    });
    return found == tools_.end() ? nullptr : &*found;
}

const McpToolDescriptor* McpGateway::find_tool(std::string_view name) const noexcept {
    const auto found = std::find_if(tools_.begin(), tools_.end(),
                                    [name](const McpToolDescriptor& tool) {
                                        return tool.name == name;
                                    });
    return found == tools_.end() ? nullptr : &*found;
}

McpResponse McpGateway::unauthorized() const {
    return {.success = false,
            .code = "unauthorized",
            .message = "invalid capability token",
            .payload = {},
            .tools = {}};
}

McpResponse McpGateway::invalid_request(std::string message) const {
    return {.success = false,
            .code = "invalid_request",
            .message = std::move(message),
            .payload = {},
            .tools = {}};
}

McpResponse McpGateway::write_denied() const {
    return {.success = false,
            .code = "consent_required",
            .message = "write operations require explicit consent=write",
            .payload = {},
            .tools = {}};
}

McpResponse McpGateway::search(std::string_view query, std::string_view token) const {
    if (!authenticate(token)) {
        return unauthorized();
    }
    McpResponse response = success_response("tool search completed");
    for (const auto& tool : tools_) {
        if (query.empty() || contains_case_insensitive(tool.name, query) ||
            contains_case_insensitive(tool.description, query)) {
            response.tools.push_back(tool);
        }
    }
    return response;
}

McpResponse McpGateway::describe(std::string_view tool, std::string_view token) const {
    if (!authenticate(token)) {
        return unauthorized();
    }
    const auto* descriptor = find_tool(tool);
    if (descriptor == nullptr) {
        return invalid_request("unknown tool");
    }
    McpResponse response = success_response("tool description returned");
    response.tools.push_back(*descriptor);
    return response;
}

McpResponse McpGateway::execute(std::string_view tool, std::string_view action,
                                std::string_view parameters, std::string_view token,
                                std::string_view consent) {
    if (!authenticate(token)) {
        return unauthorized();
    }
    if (parameters.size() > maximum_parameter_bytes) {
        return invalid_request("parameters exceed bounded request size");
    }
    const auto* descriptor = find_tool(tool);
    if (descriptor == nullptr || !descriptor->enabled) {
        return invalid_request("tool is unknown or disabled");
    }
    if (std::find(descriptor->actions.begin(), descriptor->actions.end(), action) ==
        descriptor->actions.end()) {
        return invalid_request("unknown tool action");
    }
    const auto is_write = action == "select" || action == "clear_selection" || action == "undo" ||
                          action == "redo";
    if (is_write && consent != "write") {
        return write_denied();
    }

    if (action == "inspect") {
        std::ostringstream payload;
        payload << "scene=" << safe_field(editor_.scene().name()) << '\n'
                << "hash=" << editor_.scene().state_hash() << '\n'
                << "entities=" << editor_.scene().entities().size() << '\n'
                << "selected=" << editor_.selected().value_or(scene::invalid_entity);
        return success_response("scene inspection completed", payload.str());
    }
    if (action == "list_entities") {
        std::ostringstream payload;
        for (const auto& entity : editor_.scene().entities()) {
            payload << "id=" << entity.id << ";name=" << safe_field(entity.name)
                    << ";parent=" << entity.parent << ";enabled=" << entity.enabled << '\n';
        }
        return success_response("scene entity listing completed", payload.str());
    }
    if (action == "select") {
        const auto entity = parse_entity_parameter(parameters);
        if (!entity || !editor_.select(*entity)) {
            return invalid_request("select requires an existing entity=<id> parameter");
        }
        return success_response("scene selection updated");
    }
    if (!parameters.empty()) {
        return invalid_request("this action does not accept parameters");
    }
    if (action == "clear_selection") {
        editor_.clear_selection();
        return success_response("scene selection cleared");
    }
    if (action == "undo") {
        return editor_.undo() ? success_response("scene undo completed")
                              : invalid_request("scene history has no undo entry");
    }
    return editor_.redo() ? success_response("scene redo completed")
                          : invalid_request("scene history has no redo entry");
}

McpResponse McpGateway::configure(std::string_view tool, bool enabled,
                                  std::string_view token, std::string_view consent) {
    if (!authenticate(token)) {
        return unauthorized();
    }
    if (consent != "write") {
        return write_denied();
    }
    auto* descriptor = find_tool(tool);
    if (descriptor == nullptr) {
        return invalid_request("unknown tool");
    }
    descriptor->enabled = enabled;
    return success_response(enabled ? "tool enabled" : "tool disabled");
}

} // namespace meat2d::tools
