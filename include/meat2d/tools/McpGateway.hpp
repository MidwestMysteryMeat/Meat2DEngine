#pragma once

#include "meat2d/tools/SceneEditor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::tools {

// Operations deliberately mirror the discovery-first shape used by mature
// editor bridges. A transport adapter can map these to MCP JSON-RPC without
// giving the transport direct access to engine objects.
enum class McpOperation : std::uint8_t { Search, Describe, Execute, Configure };
enum class McpPermission : std::uint8_t { Read, Write };

struct McpToolDescriptor {
    std::string name;
    std::string description;
    McpPermission permission{McpPermission::Read};
    std::vector<std::string> actions;
    bool enabled{true};
};

struct McpResponse {
    bool success{false};
    std::string code;
    std::string message;
    std::string payload;
    std::vector<McpToolDescriptor> tools;
};

// Bounded, transport-neutral MCP gateway for editor operations. It is the
// security and validation boundary; stdio/HTTP adapters should only parse,
// authenticate, and forward requests here.
class McpGateway {
  public:
    static constexpr std::size_t maximum_tools = 32U;
    static constexpr std::size_t maximum_parameter_bytes = 256U;

    McpGateway(SceneEditor& editor, std::string capability_token);

    [[nodiscard]] bool authenticate(std::string_view token) const noexcept;
    [[nodiscard]] McpResponse search(std::string_view query,
                                     std::string_view token) const;
    [[nodiscard]] McpResponse describe(std::string_view tool,
                                       std::string_view token) const;
    [[nodiscard]] McpResponse execute(std::string_view tool, std::string_view action,
                                      std::string_view parameters, std::string_view token,
                                      std::string_view consent = {});
    [[nodiscard]] McpResponse configure(std::string_view tool, bool enabled,
                                        std::string_view token,
                                        std::string_view consent = {});

    [[nodiscard]] static bool constant_time_equal(std::string_view left,
                                                   std::string_view right) noexcept;

  private:
    [[nodiscard]] McpToolDescriptor* find_tool(std::string_view name) noexcept;
    [[nodiscard]] const McpToolDescriptor* find_tool(std::string_view name) const noexcept;
    [[nodiscard]] McpResponse unauthorized() const;
    [[nodiscard]] McpResponse invalid_request(std::string message) const;
    [[nodiscard]] McpResponse write_denied() const;

    SceneEditor& editor_;
    std::string capability_token_;
    std::vector<McpToolDescriptor> tools_;
};

} // namespace meat2d::tools
