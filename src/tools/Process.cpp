#include "meat2d/tools/Process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace meat2d::tools {
namespace {

constexpr std::size_t maximum_captured_output = 2U * 1024U * 1024U;

void append_output(std::string& destination, const char* bytes, std::size_t count) {
    if (destination.size() >= maximum_captured_output) {
        return;
    }
    const auto available = maximum_captured_output - destination.size();
    destination.append(bytes, std::min(count, available));
}

#if defined(_WIN32)
std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), required);
    return result;
}

std::wstring quote_argument(std::wstring_view argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }

    std::wstring result(1, L'"');
    std::size_t backslashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}
#endif

} // namespace

ProcessResult run_process(std::span<const std::string> arguments,
                          const std::filesystem::path& working_directory) {
    ProcessResult result{};
    if (arguments.empty() || arguments.front().empty()) {
        result.output = "no executable was provided";
        return result;
    }

#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0) ||
        !SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        result.output = "could not create process output pipe";
        if (read_pipe != nullptr) {
            CloseHandle(read_pipe);
        }
        if (write_pipe != nullptr) {
            CloseHandle(write_pipe);
        }
        return result;
    }

    std::wstring command_line;
    for (const auto& argument : arguments) {
        if (!command_line.empty()) {
            command_line.push_back(L' ');
        }
        command_line += quote_argument(widen(argument));
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const auto working_text =
        working_directory.empty() ? std::wstring{} : working_directory.wstring();
    const BOOL created = CreateProcessW(
        nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        working_text.empty() ? nullptr : working_text.c_str(), &startup, &process);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        result.output =
            "process launch failed with Windows error " + std::to_string(GetLastError());
        return result;
    }
    result.launched = true;

    std::array<char, 4096> buffer{};
    DWORD bytes_read = 0;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                    nullptr)) {
        append_output(result.output, buffer.data(), bytes_read);
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    result.exit_code = exit_code <= static_cast<DWORD>(std::numeric_limits<int>::max())
                           ? static_cast<int>(exit_code)
                           : -1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    int output_pipe[2]{};
    if (pipe(output_pipe) != 0) {
        result.output = "could not create process output pipe";
        return result;
    }

    const auto child = fork();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        result.output = "could not fork process";
        return result;
    }
    if (child == 0) {
        close(output_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[1]);
        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) {
            _exit(126);
        }

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1U);
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    result.launched = true;
    close(output_pipe[1]);
    std::array<char, 4096> buffer{};
    ssize_t bytes_read = 0;
    while ((bytes_read = read(output_pipe[0], buffer.data(), buffer.size())) > 0) {
        append_output(result.output, buffer.data(), static_cast<std::size_t>(bytes_read));
    }
    close(output_pipe[0]);

    int status = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
#endif
    return result;
}

ProcessResult run_process(std::initializer_list<std::string> arguments,
                          const std::filesystem::path& working_directory) {
    return run_process(std::span<const std::string>(arguments.begin(), arguments.size()),
                       working_directory);
}

} // namespace meat2d::tools
