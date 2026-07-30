#pragma once

#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>

namespace meat2d::tools {

struct ProcessResult {
    bool launched{};
    int exit_code{-1};
    std::string output;

    [[nodiscard]] bool success() const noexcept {
        return launched && exit_code == 0;
    }
};

[[nodiscard]] ProcessResult run_process(std::span<const std::string> arguments,
                                        const std::filesystem::path& working_directory = {});
[[nodiscard]] ProcessResult run_process(std::initializer_list<std::string> arguments,
                                        const std::filesystem::path& working_directory = {});

} // namespace meat2d::tools
