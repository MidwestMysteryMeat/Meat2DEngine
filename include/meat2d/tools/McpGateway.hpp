#pragma once

#include "meat2d/tools/SceneEditor.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::tools {

// Operations deliberately mirror the discovery-first shape used by mature
// editor bridges. A transport adapter can map these to MCP JSON-RPC without
// giving the transport direct access to engine objects.
enum class McpOperation : std::uint8_t { Search, Describe, Execute, Configure };
enum class McpPermission : std::uint8_t { Read, Write };
enum class McpCapability : std::uint8_t {
    ReadScene = 1U,
    WriteScene = 2U,
    ConfigureTools = 4U,
};

inline constexpr std::uint8_t mcp_capability_all =
    static_cast<std::uint8_t>(McpCapability::ReadScene) |
    static_cast<std::uint8_t>(McpCapability::WriteScene) |
    static_cast<std::uint8_t>(McpCapability::ConfigureTools);

struct McpToolDescriptor {
    std::string name;
    std::string description;
    McpPermission permission{McpPermission::Read};
    std::vector<std::string> actions;
    bool enabled{true};
};

struct McpResponse {
    bool success{false};
    std::uint64_t request_id{};
    std::string code;
    std::string message;
    std::string payload;
    std::vector<McpToolDescriptor> tools;
};

struct McpAuditEvent {
    std::uint64_t request_id{};
    McpOperation operation{McpOperation::Search};
    bool success{false};
    std::string tool;
    std::string action;
    std::string code;
};

// Bounded, transport-neutral MCP gateway for editor operations. It is the
// security and validation boundary; stdio/HTTP adapters should only parse,
// authenticate, and forward requests here.
class McpGateway {
  public:
    static constexpr std::size_t maximum_tools = 32U;
    static constexpr std::size_t maximum_parameter_bytes = 256U;
    static constexpr std::size_t maximum_requests_per_session = 128U;
    static constexpr std::size_t maximum_audit_events = 128U;

    McpGateway(SceneEditor& editor, std::string capability_token,
               std::uint8_t capabilities = mcp_capability_all);

    [[nodiscard]] bool authenticate(std::string_view token) const noexcept;
    [[nodiscard]] McpResponse search(std::string_view query,
                                     std::string_view token,
                                     std::uint64_t request_id = 0U);
    [[nodiscard]] McpResponse describe(std::string_view tool,
                                       std::string_view token,
                                       std::uint64_t request_id = 0U);
    [[nodiscard]] McpResponse execute(std::string_view tool, std::string_view action,
                                      std::string_view parameters, std::string_view token,
                                      std::string_view consent = {},
                                      std::uint64_t request_id = 0U);
    [[nodiscard]] McpResponse configure(std::string_view tool, bool enabled,
                                        std::string_view token,
                                        std::string_view consent = {},
                                        std::uint64_t request_id = 0U);

    // Starts a fresh bounded adapter/session budget. It does not mutate the
    // editor or scene and should be called when a transport connection closes.
    void reset_session() noexcept;
    [[nodiscard]] std::size_t requests_this_session() const noexcept;
    [[nodiscard]] std::span<const McpAuditEvent> audit_log() const noexcept;
    void clear_audit_log() noexcept;

    [[nodiscard]] static bool constant_time_equal(std::string_view left,
                                                   std::string_view right) noexcept;

  private:
    [[nodiscard]] McpToolDescriptor* find_tool(std::string_view name) noexcept;
    [[nodiscard]] const McpToolDescriptor* find_tool(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<McpResponse> authorize_request(
        McpOperation operation, std::string_view tool, std::string_view action,
        std::string_view token, std::uint8_t required_capability,
        std::uint64_t request_id);
    void record_audit(std::uint64_t request_id, McpOperation operation,
                      std::string_view tool, std::string_view action, bool success,
                      std::string_view code);
    [[nodiscard]] McpResponse unauthorized(std::uint64_t request_id) const;
    [[nodiscard]] McpResponse invalid_request(std::string message,
                                              std::uint64_t request_id = 0U) const;
    [[nodiscard]] McpResponse write_denied(std::uint64_t request_id) const;

    SceneEditor& editor_;
    std::string capability_token_;
    std::uint8_t capabilities_{};
    std::vector<McpToolDescriptor> tools_;
    std::size_t requests_this_session_{};
    std::uint64_t last_request_id_{};
    std::vector<McpAuditEvent> audit_log_;
};

} // namespace meat2d::tools
