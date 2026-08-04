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

McpResponse success_response(std::uint64_t request_id, std::string message,
                             std::string payload = {}) {
    return {.success = true,
            .request_id = request_id,
            .code = "ok",
            .message = std::move(message),
            .payload = std::move(payload),
            .tools = {}};
}

} // namespace

McpGateway::McpGateway(SceneEditor& editor, std::string capability_token,
                       std::uint8_t capabilities)
    : editor_(editor), capability_token_(std::move(capability_token)),
      capabilities_(capabilities) {
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

McpResponse McpGateway::unauthorized(std::uint64_t request_id) const {
    return {.success = false,
            .request_id = request_id,
            .code = "unauthorized",
            .message = "invalid capability token",
            .payload = {},
            .tools = {}};
}

McpResponse McpGateway::invalid_request(std::string message,
                                        std::uint64_t request_id) const {
    return {.success = false,
            .request_id = request_id,
            .code = "invalid_request",
            .message = std::move(message),
            .payload = {},
            .tools = {}};
}

McpResponse McpGateway::write_denied(std::uint64_t request_id) const {
    return {.success = false,
            .request_id = request_id,
            .code = "consent_required",
            .message = "write operations require explicit consent=write",
            .payload = {},
            .tools = {}};
}

std::optional<McpResponse> McpGateway::authorize_request(
    McpOperation operation, std::string_view tool, std::string_view action,
    std::string_view token, std::uint8_t required_capability, std::uint64_t request_id) {
    if (!authenticate(token)) {
        return unauthorized(request_id);
    }
    const auto normalized_id = request_id == 0U
                                   ? (last_request_id_ == std::numeric_limits<std::uint64_t>::max()
                                          ? 0U
                                          : last_request_id_ + 1U)
                                   : request_id;
    if (normalized_id == 0U) {
        return invalid_request("request ID exhausted", request_id);
    }
    if (request_id != 0U && request_id <= last_request_id_) {
        record_audit(normalized_id, operation, tool, action, false, "duplicate_request");
        return invalid_request("request ID must increase within a session", normalized_id);
    }
    if (requests_this_session_ >= maximum_requests_per_session) {
        record_audit(normalized_id, operation, tool, action, false, "rate_limited");
        return McpResponse{.success = false,
                           .request_id = normalized_id,
                           .code = "rate_limited",
                           .message = "MCP session request budget exhausted",
                           .payload = {},
                           .tools = {}};
    }
    if ((capabilities_ & required_capability) != required_capability) {
        record_audit(normalized_id, operation, tool, action, false, "forbidden");
        return McpResponse{.success = false,
                           .request_id = normalized_id,
                           .code = "forbidden",
                           .message = "capability scope does not permit this operation",
                           .payload = {},
                           .tools = {}};
    }
    ++requests_this_session_;
    last_request_id_ = normalized_id;
    record_audit(normalized_id, operation, tool, action, true, "accepted");
    return std::nullopt;
}

void McpGateway::record_audit(std::uint64_t request_id, McpOperation operation,
                              std::string_view tool, std::string_view action, bool success,
                              std::string_view code) {
    if (audit_log_.size() >= maximum_audit_events) {
        return;
    }
    const auto bounded = [](std::string_view value) {
        auto result = safe_field(value);
        if (result.size() > 64U) {
            result.resize(64U);
        }
        return result;
    };
    audit_log_.push_back({.request_id = request_id,
                          .operation = operation,
                          .success = success,
                          .tool = bounded(tool),
                          .action = bounded(action),
                          .code = bounded(code)});
}

void McpGateway::reset_session() noexcept {
    requests_this_session_ = 0U;
    last_request_id_ = 0U;
    audit_log_.clear();
}

std::size_t McpGateway::requests_this_session() const noexcept {
    return requests_this_session_;
}

std::span<const McpAuditEvent> McpGateway::audit_log() const noexcept {
    return std::span<const McpAuditEvent>(audit_log_);
}

void McpGateway::clear_audit_log() noexcept {
    audit_log_.clear();
}

McpResponse McpGateway::search(std::string_view query, std::string_view token,
                               std::uint64_t request_id) {
    if (const auto denied = authorize_request(McpOperation::Search, {}, {}, token,
                                               static_cast<std::uint8_t>(McpCapability::ReadScene),
                                               request_id)) {
        return *denied;
    }
    const auto response_id = last_request_id_;
    McpResponse response = success_response(response_id, "tool search completed");
    for (const auto& tool : tools_) {
        if (query.empty() || contains_case_insensitive(tool.name, query) ||
            contains_case_insensitive(tool.description, query)) {
            response.tools.push_back(tool);
        }
    }
    return response;
}

McpResponse McpGateway::describe(std::string_view tool, std::string_view token,
                                 std::uint64_t request_id) {
    if (const auto denied = authorize_request(McpOperation::Describe, tool, {}, token,
                                               static_cast<std::uint8_t>(McpCapability::ReadScene),
                                               request_id)) {
        return *denied;
    }
    const auto response_id = last_request_id_;
    const auto* descriptor = find_tool(tool);
    if (descriptor == nullptr) {
        return invalid_request("unknown tool", response_id);
    }
    McpResponse response = success_response(response_id, "tool description returned");
    response.tools.push_back(*descriptor);
    return response;
}

McpResponse McpGateway::execute(std::string_view tool, std::string_view action,
                                std::string_view parameters, std::string_view token,
                                std::string_view consent, std::uint64_t request_id) {
    const auto is_write = action == "select" || action == "clear_selection" || action == "undo" ||
                          action == "redo";
    const auto required_capability = static_cast<std::uint8_t>(
        is_write ? McpCapability::WriteScene : McpCapability::ReadScene);
    if (const auto denied = authorize_request(McpOperation::Execute, tool, action, token,
                                               required_capability, request_id)) {
        return *denied;
    }
    const auto response_id = last_request_id_;
    if (parameters.size() > maximum_parameter_bytes) {
        return invalid_request("parameters exceed bounded request size", response_id);
    }
    const auto* descriptor = find_tool(tool);
    if (descriptor == nullptr || !descriptor->enabled) {
        return invalid_request("tool is unknown or disabled", response_id);
    }
    if (std::find(descriptor->actions.begin(), descriptor->actions.end(), action) ==
        descriptor->actions.end()) {
        return invalid_request("unknown tool action", response_id);
    }
    if (is_write && consent != "write") {
        return write_denied(response_id);
    }

    if (action == "inspect") {
        std::ostringstream payload;
        payload << "scene=" << safe_field(editor_.scene().name()) << '\n'
                << "hash=" << editor_.scene().state_hash() << '\n'
                << "entities=" << editor_.scene().entities().size() << '\n'
                << "selected=" << editor_.selected().value_or(scene::invalid_entity);
        return success_response(response_id, "scene inspection completed", payload.str());
    }
    if (action == "list_entities") {
        std::ostringstream payload;
        for (const auto& entity : editor_.scene().entities()) {
            payload << "id=" << entity.id << ";name=" << safe_field(entity.name)
                    << ";parent=" << entity.parent << ";enabled=" << entity.enabled << '\n';
        }
        return success_response(response_id, "scene entity listing completed", payload.str());
    }
    if (action == "select") {
        const auto entity = parse_entity_parameter(parameters);
        if (!entity || !editor_.select(*entity)) {
            return invalid_request("select requires an existing entity=<id> parameter", response_id);
        }
        return success_response(response_id, "scene selection updated");
    }
    if (!parameters.empty()) {
        return invalid_request("this action does not accept parameters", response_id);
    }
    if (action == "clear_selection") {
        editor_.clear_selection();
        return success_response(response_id, "scene selection cleared");
    }
    if (action == "undo") {
        return editor_.undo() ? success_response(response_id, "scene undo completed")
                              : invalid_request("scene history has no undo entry", response_id);
    }
    return editor_.redo() ? success_response(response_id, "scene redo completed")
                          : invalid_request("scene history has no redo entry", response_id);
}

McpResponse McpGateway::configure(std::string_view tool, bool enabled,
                                  std::string_view token, std::string_view consent,
                                  std::uint64_t request_id) {
    if (const auto denied = authorize_request(
            McpOperation::Configure, tool, {}, token,
            static_cast<std::uint8_t>(McpCapability::ConfigureTools), request_id)) {
        return *denied;
    }
    const auto response_id = last_request_id_;
    if (consent != "write") {
        return write_denied(response_id);
    }
    auto* descriptor = find_tool(tool);
    if (descriptor == nullptr) {
        return invalid_request("unknown tool", response_id);
    }
    descriptor->enabled = enabled;
    return success_response(response_id, enabled ? "tool enabled" : "tool disabled");
}

} // namespace meat2d::tools
