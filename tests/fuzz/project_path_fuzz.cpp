#include "meat2d/tools/ProjectManager.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);
    static_cast<void>(meat2d::tools::ProjectManager::project_slug(input));
    static_cast<void>(meat2d::tools::ProjectManager::project_identifier(input));
    return 0;
}

