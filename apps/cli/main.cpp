#include "meat2d/core/Version.hpp"
#include "meat2d/tools/Process.hpp"
#include "meat2d/tools/ProjectManager.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout << "Meat2D project tools\n\n"
              << "  meat2d new NAME [--directory PATH] [--template side|top|metroidvania|falling-sand]\n"
              << "  meat2d build PATH [--release]\n"
              << "  meat2d run PATH\n"
              << "  meat2d package PATH\n"
              << "  meat2d publish PATH --repo OWNER/REPOSITORY [--private]\n"
              << "  meat2d doctor\n";
}

int print_result(const meat2d::tools::ToolResult& result) {
    std::cout << result.summary << '\n';
    if (!result.details.empty()) {
        std::cout << result.details;
        if (result.details.back() != '\n') {
            std::cout << '\n';
        }
    }
    return result.success ? 0 : 1;
}

int run_doctor(const std::filesystem::path& templates) {
    const auto cmake = meat2d::tools::locate_cmake_executable();
    const auto ninja = meat2d::tools::locate_ninja_executable(cmake);
    std::cout << "Meat2D " << meat2d::version_string << '\n'
              << "templates: " << (templates.empty() ? "NOT FOUND" : templates.string()) << '\n'
              << "ninja: " << (ninja.empty() ? "not available" : ninja) << '\n';
    bool healthy = !templates.empty() && !ninja.empty();
    for (const auto executable : {cmake, std::string("git"), std::string("gh")}) {
        const auto result = meat2d::tools::run_process({executable, "--version"});
        std::cout << executable << ": " << (result.success() ? "ready" : "not available") << '\n';
        if (executable != "gh") {
            healthy = healthy && result.success();
        }
    }
    return healthy ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const auto template_root = meat2d::tools::locate_template_root(argv[0]);
    meat2d::tools::ProjectManager manager(template_root);
    const std::string_view command(argv[1]);
    if (command == "--help" || command == "-h" || command == "help") {
        print_usage();
        return 0;
    }
    if (command == "--version" || command == "version") {
        std::cout << meat2d::version_string << '\n';
        return 0;
    }
    if (command == "doctor") {
        return run_doctor(template_root);
    }
    if (command == "new") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        meat2d::tools::NewProjectOptions options{
            .name = argv[2],
            .directory = {},
            .project_template = meat2d::tools::ProjectTemplate::SideScroller,
            .engine_git_tag = "main",
        };
        for (int index = 3; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--directory" && index + 1 < argc) {
                options.directory = argv[++index];
            } else if (argument == "--template" && index + 1 < argc) {
                const std::string_view choice(argv[++index]);
                if (choice == "side" || choice == "side-scroller") {
                    options.project_template = meat2d::tools::ProjectTemplate::SideScroller;
                } else if (choice == "top" || choice == "top-down") {
                    options.project_template = meat2d::tools::ProjectTemplate::TopDown;
                } else if (choice == "metroidvania") {
                    options.project_template = meat2d::tools::ProjectTemplate::Metroidvania;
                } else if (choice == "falling-sand" || choice == "falling_sand" ||
                           choice == "sand") {
                    options.project_template = meat2d::tools::ProjectTemplate::FallingSand;
                } else {
                    std::cerr << "Unknown template: " << choice
                              << " (choose side, top, metroidvania, or falling-sand)\n";
                    return 1;
                }
            } else if (argument == "--engine-tag" && index + 1 < argc) {
                options.engine_git_tag = argv[++index];
            }
        }
        if (options.directory.empty()) {
            options.directory = std::filesystem::current_path() /
                                meat2d::tools::ProjectManager::project_slug(options.name);
        }
        return print_result(manager.create_project(options));
    }
    if (command == "build" || command == "package" || command == "run") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        const std::filesystem::path project(argv[2]);
        if (command == "package") {
            return print_result(manager.package_project(project));
        }
        if (command == "run") {
            return print_result(manager.run_project(project));
        }
        auto profile = meat2d::tools::BuildProfile::Debug;
        for (int index = 3; index < argc; ++index) {
            if (std::string_view(argv[index]) == "--release") {
                profile = meat2d::tools::BuildProfile::Release;
            }
        }
        return print_result(manager.build_project(project, profile));
    }
    if (command == "publish") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        meat2d::tools::PublishOptions options{
            .project_directory = argv[2],
            .repository = {},
            .description = {},
            .visibility = meat2d::tools::RepositoryVisibility::Public,
        };
        for (int index = 3; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--repo" && index + 1 < argc) {
                options.repository = argv[++index];
            } else if (argument == "--description" && index + 1 < argc) {
                options.description = argv[++index];
            } else if (argument == "--private") {
                options.visibility = meat2d::tools::RepositoryVisibility::Private;
            }
        }
        return print_result(manager.publish_project(options));
    }

    print_usage();
    return 1;
}
