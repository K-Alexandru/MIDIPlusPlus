#pragma once

// AudioToMidi: runs tools/mp3-to-midi/convert.py beside the app and reads what
// it says.
//
// The transcription half of the YouTube to MIDI section of HANDOFF.md. Transkun
// is a PyTorch model with no clean ONNX path, so it is never loaded in process:
// the app starts Python, reads one status per line, and the .mid lands in the
// MIDI folder, where a folder scan picks it up like any other file.
//
// The job runs on its own thread and owns the whole process tree through a job
// object, so Cancel also stops the Transkun process Python starts, and nothing
// here ever touches the message loop.
//
// Header only and free of project references, so consuming it costs no change
// to either vcxproj.

#include <windows.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace audio_to_midi {

struct Status {
    enum class Kind { Step, Done, Error, Text };
    Kind kind = Kind::Text;
    std::string text;  // for Done, the path of the .mid, UTF-8
};

inline Status ParseLine(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    const auto take = [&](std::string_view prefix, Status::Kind kind, Status& out) {
        if (line.substr(0, prefix.size()) != prefix) return false;
        out = {kind, std::string(line.substr(prefix.size()))};
        return true;
    };
    Status status;
    if (take("step: ", Status::Kind::Step, status) || take("done: ", Status::Kind::Done, status) ||
        take("error: ", Status::Kind::Error, status))
        return status;
    return {Status::Kind::Text, std::string(line)};
}

inline bool IsLink(std::wstring_view source) {
    const auto starts = [&](std::wstring_view prefix) {
        if (source.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (towlower(source[i]) != prefix[i]) return false;
        return true;
    };
    return starts(L"http://") || starts(L"https://");
}

// One argument, quoted so CommandLineToArgvW and the C runtime read it back
// unchanged: backslashes only double when a quote follows them.
inline std::wstring QuoteArgument(std::wstring_view argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
        return std::wstring(argument);
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        quoted.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        quoted.push_back(c);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

inline std::wstring CommandLine(const std::filesystem::path& python, const std::filesystem::path& script,
                                std::wstring_view source, const std::filesystem::path& outputFolder) {
    return QuoteArgument(python.native()) + L" -u " + QuoteArgument(script.native()) + L" " +
           QuoteArgument(source) + L" --out-dir " + QuoteArgument(outputFolder.native());
}

// Where the converter is, looked for in this order: MIDIPP_CONVERTER_PYTHON, a
// converter folder shipped beside the exe, then the repository's tools folder
// for a development build. Empty when none is there, which the app reports as
// the converter not being installed rather than failing to start it.
struct Install {
    std::filesystem::path python;
    std::filesystem::path script;
    bool Found() const { return !python.empty() && !script.empty(); }
};

inline Install FindInstall(const std::filesystem::path& exeFolder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    Install found;
    for (const auto& folder : {exeFolder / L"converter", exeFolder.parent_path().parent_path() / L"tools" / L"mp3-to-midi"})
        if (fs::is_regular_file(folder / L"convert.py", ec)) { found.script = folder / L"convert.py"; break; }
    if (const DWORD size = GetEnvironmentVariableW(L"MIDIPP_CONVERTER_PYTHON", nullptr, 0)) {
        std::wstring value(size, L'\0');
        value.resize(GetEnvironmentVariableW(L"MIDIPP_CONVERTER_PYTHON", value.data(), size));
        if (fs::is_regular_file(value, ec)) found.python = value;
    }
    if (found.python.empty() && !found.script.empty())
        for (const auto& candidate : {found.script.parent_path() / L"python" / L"python.exe",
                                      found.script.parent_path() / L".venv" / L"Scripts" / L"python.exe"})
            if (fs::is_regular_file(candidate, ec)) { found.python = candidate; break; }
    return found;
}

class Job {
public:
    using Sink = std::function<void(const Status&)>;

    Job() = default;
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    ~Job() { Cancel(); if (worker_.joinable()) worker_.join(); }

    // False when the process could not start; the sink is not called then.
    // Otherwise every status reaches the sink from the job's thread, and the
    // last one is always Done or Error.
    bool Start(const std::wstring& commandLine, Sink sink) {
        if (worker_.joinable()) worker_.join();
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, &inherit, 0)) return false;
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

        HANDLE group = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (group) SetInformationJobObject(group, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = write;
        startup.hStdError = write;
        PROCESS_INFORMATION process{};
        std::wstring mutableLine = commandLine;
        const BOOL started = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process);
        CloseHandle(write);
        if (!started) {
            CloseHandle(read);
            if (group) CloseHandle(group);
            return false;
        }
        if (group) AssignProcessToJobObject(group, process.hProcess);
        ResumeThread(process.hThread);
        CloseHandle(process.hThread);
        {
            std::lock_guard lock(mutex_);
            process_ = process.hProcess;
            group_ = group;
            cancelled_ = false;
        }
        worker_ = std::thread([this, read, sink = std::move(sink)] { Pump(read, sink); });
        return true;
    }

    void Cancel() {
        std::lock_guard lock(mutex_);
        if (!process_) return;
        cancelled_ = true;
        if (group_) TerminateJobObject(group_, 1);
        else TerminateProcess(process_, 1);
    }

    bool Running() const {
        std::lock_guard lock(mutex_);
        return process_ != nullptr;
    }

private:
    void Pump(HANDLE read, const Sink& sink) {
        std::string pending;
        bool finished = false;
        const auto emit = [&](std::string_view line) {
            auto status = ParseLine(line);
            if (status.kind == Status::Kind::Text && status.text.empty()) return;
            if (status.kind == Status::Kind::Done || status.kind == Status::Kind::Error) finished = true;
            sink(status);
        };
        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got) {
            pending.append(buffer, got);
            for (size_t end; (end = pending.find_first_of("\r\n")) != std::string::npos;) {
                emit(std::string_view(pending).substr(0, end));
                pending.erase(0, end + 1);
            }
        }
        if (!pending.empty()) emit(pending);
        CloseHandle(read);

        HANDLE process, group;
        bool cancelled;
        {
            std::lock_guard lock(mutex_);
            process = process_;
            group = group_;
            cancelled = cancelled_;
        }
        WaitForSingleObject(process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        {
            std::lock_guard lock(mutex_);
            process_ = nullptr;
            group_ = nullptr;
        }
        CloseHandle(process);
        if (group) CloseHandle(group);
        // Exactly one final status. A Cancel that lands after the converter has
        // said done, which the destructor does, is not a cancelled conversion.
        if (finished) return;
        sink({Status::Kind::Error, cancelled ? std::string("Conversion cancelled.")
                                             : "The converter stopped with exit code " + std::to_string(code) + "."});
    }

    mutable std::mutex mutex_;
    HANDLE process_ = nullptr;
    HANDLE group_ = nullptr;
    bool cancelled_ = false;
    std::thread worker_;
};

}  // namespace audio_to_midi
