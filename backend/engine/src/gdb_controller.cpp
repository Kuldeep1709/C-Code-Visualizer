#include "gdb_controller.hpp"
#include "mi_parser.hpp"

#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <termios.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <filesystem>

namespace ccv {

GDBController::GDBController() = default;

GDBController::~GDBController() {
    stop();
}

// Compilation

CompileResult GDBController::compile(const std::string& sourceCode, const std::string& workDir) {
    CompileResult result;
    std::filesystem::create_directories(workDir);

    std::string sourcePath = workDir + "/main.c";
    std::string binaryPath = workDir + "/main";

    {
        std::ofstream f(sourcePath);
        if (!f.is_open()) { result.error = "Failed to write source file"; return result; }
        f << sourceCode;
    }

    std::string cmd = "gcc -g -O0 -o " + binaryPath + " " + sourcePath + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { result.error = "Failed to run gcc"; return result; }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int status = pclose(pipe);

    if (status != 0) {
        result.error = output.empty() ? "Compilation failed" : output;
        return result;
    }
    result.success    = true;
    result.binaryPath = binaryPath;
    result.sourcePath = sourcePath;
    result.warnings   = output;
    return result;
}

// PTY helpers

static bool openPTY(int& masterFd, std::string& slaveName) {
    masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0) return false;
    if (grantpt(masterFd) != 0 || unlockpt(masterFd) != 0) {
        close(masterFd); masterFd = -1; return false;
    }
    char* name = ptsname(masterFd);
    if (!name) { close(masterFd); masterFd = -1; return false; }
    slaveName = name;

    int flags = fcntl(masterFd, F_GETFL, 0);
    fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

// Drain available bytes from ptyMaster_ and append to programOutput_
void GDBController::drainPTY() {
    if (ptyMaster_ < 0) return;
    char buf[4096];
    ssize_t n;
    while ((n = read(ptyMaster_, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        programOutput_ += buf;
    }
}

// Process Management

void GDBController::spawnGDB(const std::string& binaryPath) {
    if (pipe(stdinPipe_) == -1 || pipe(stdoutPipe_) == -1)
        throw std::runtime_error("Failed to create pipes");

    gdbPid_ = fork();
    if (gdbPid_ == -1) throw std::runtime_error("Failed to fork");

    if (gdbPid_ == 0) {
        close(stdinPipe_[1]);
        close(stdoutPipe_[0]);
        dup2(stdinPipe_[0],  STDIN_FILENO);
        dup2(stdoutPipe_[1], STDOUT_FILENO);
        dup2(stdoutPipe_[1], STDERR_FILENO);
        close(stdinPipe_[0]);
        close(stdoutPipe_[1]);
        execlp("gdb", "gdb", "--interpreter=mi2", "--quiet", binaryPath.c_str(), nullptr);
        _exit(1);
    }

    close(stdinPipe_[0]);
    close(stdoutPipe_[1]);
    readBuffer_.clear();
    programOutput_.clear();
}

void GDBController::sendCommand(const std::string& command) {
    if (stdinPipe_[1] == -1) return;
    std::string cmd = command + "\n";
    if (write(stdinPipe_[1], cmd.c_str(), cmd.size()) < 0)
        throw std::runtime_error("Failed to write to GDB stdin");
}

std::string GDBController::readLine(int timeoutMs) {
    while (true) {
        auto pos = readBuffer_.find('\n');
        if (pos != std::string::npos) {
            std::string line = readBuffer_.substr(0, pos);
            readBuffer_ = readBuffer_.substr(pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        struct pollfd pfd { stdoutPipe_[0], POLLIN, 0 };
        if (poll(&pfd, 1, timeoutMs) <= 0) return "";
        char buf[4096];
        ssize_t n = read(stdoutPipe_[0], buf, sizeof(buf) - 1);
        if (n <= 0) return "";
        buf[n] = '\0';
        readBuffer_ += buf;
    }
}

std::string GDBController::readUntilResult(int timeoutMs) {
    std::string accumulated;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        int rem = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (rem <= 0) break;
        std::string line = readLine(rem);
        if (line.empty()) continue;
        accumulated += line + "\n";
        // Drain PTY on each iteration
        drainPTY();
        if (!line.empty() && line[0] == '^') return accumulated;
    }
    return accumulated;
}

std::string GDBController::readUntilStopped(int timeoutMs) {
    std::string accumulated;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        int rem = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (rem <= 0) break;
        std::string line = readLine(rem);
        if (line.empty()) continue;
        accumulated += line + "\n";
        drainPTY();
        if (!line.empty() && line[0] == '*' && line.find("stopped") != std::string::npos)
            return accumulated;
        if (!line.empty() && line[0] == '^' && line.find("error") != std::string::npos)
            return accumulated;
    }
    return accumulated;
}

// Session Lifecycle

ProgramState GDBController::start(const std::string& binaryPath, const std::string& sourcePath) {
    if (gdbPid_ > 0) stop();

    workDir_    = std::filesystem::path(binaryPath).parent_path().string();
    sourcePath_ = sourcePath;
    programOutput_.clear();

    std::string slaveName;
    if (!openPTY(ptyMaster_, slaveName)) {
        ptyMaster_ = -1;
    }

    spawnGDB(binaryPath);
    readUntilResult(5000);

    if (!slaveName.empty()) {
        sendCommand("set inferior-tty " + slaveName);
        readUntilResult(2000);
    }

    sendCommand("-break-insert main");
    readUntilResult(3000);

    sendCommand("-exec-run");
    readUntilStopped(10000);
    drainPTY();

    return getCurrentState();
}

ProgramState GDBController::stepOver() {
    sendCommand("-exec-next");
    readUntilStopped(10000);
    drainPTY();

    // Check if program exited
    std::string check;
    sendCommand("-stack-list-frames");
    std::string resp = readUntilResult(2000);
    if (resp.find("error") != std::string::npos || resp.find("exited") != std::string::npos) {
        ProgramState state;
        state.status = "finished";
        state.output = programOutput_;
        return state;
    }

    return getCurrentState();
}

ProgramState GDBController::stepInto() {
    sendCommand("-exec-step");
    std::string response = readUntilStopped(10000);
    drainPTY();
    if (isExitReason(response)) {
        ProgramState state; state.status = "finished"; state.output = programOutput_;
        return state;
    }
    return getCurrentState();
}

ProgramState GDBController::continueExec() {
    sendCommand("-exec-continue");
    std::string response = readUntilStopped(30000);
    drainPTY();
    if (isExitReason(response)) {
        ProgramState state; state.status = "finished"; state.output = programOutput_;
        return state;
    }
    return getCurrentState();
}

void GDBController::stop() {
    if (gdbPid_ > 0) {
        try { sendCommand("-gdb-exit"); } catch (...) {}
        int status;
        if (waitpid(gdbPid_, &status, WNOHANG) == 0) {
            kill(gdbPid_, SIGKILL);
            waitpid(gdbPid_, &status, 0);
        }
        gdbPid_ = -1;
    }
    if (stdinPipe_[1]  != -1) { close(stdinPipe_[1]);  stdinPipe_[1]  = -1; }
    if (stdoutPipe_[0] != -1) { close(stdoutPipe_[0]); stdoutPipe_[0] = -1; }
    if (ptyMaster_     != -1) { close(ptyMaster_);      ptyMaster_     = -1; }
    readBuffer_.clear();
}

// State Extraction

std::string GDBController::readCapturedOutput() { return programOutput_; }

ProgramState GDBController::getCurrentState() {
    ProgramState state;
    state.status      = "paused";
    state.currentLine = getCurrentLine();
    state.variables   = getLocalVariables();
    state.stack       = getStackFrames();
    state.output      = programOutput_;
    return state;
}

int GDBController::getCurrentLine() {
    sendCommand("-stack-info-frame");
    std::string response = readUntilResult(3000);
    auto records = mi::parse(response);
    auto* result = mi::findRecord(records, mi::RecordType::Result, "done");
    if (!result) return -1;
    auto it = result->payload.find("frame");
    if (it == result->payload.end()) return -1;
    return it->second.getInt("line", -1);
}

std::vector<Variable> GDBController::getLocalVariables() {
    if (!sourcePath_.empty()) {
        sendCommand("-stack-info-frame");
        std::string resp = readUntilResult(2000);
        auto recs = mi::parse(resp);
        auto* res = mi::findRecord(recs, mi::RecordType::Result, "done");
        if (res) {
            auto fit = res->payload.find("frame");
            if (fit != res->payload.end()) {
                std::string ff = fit->second.getString("fullname", "");
                if (ff.empty()) ff = fit->second.getString("file", "");
                if (ff.empty() || ff == "??") return {};
                std::string srcBase = std::filesystem::path(sourcePath_).filename().string();
                std::string frmBase = std::filesystem::path(ff).filename().string();
                if (frmBase != srcBase) return {};
            }
        }
    }

    sendCommand("-stack-list-locals --simple-values");
    std::string response = readUntilResult(3000);
    auto records = mi::parse(response);
    auto* result = mi::findRecord(records, mi::RecordType::Result, "done");
    if (!result) return {};

    std::vector<Variable> vars;
    auto it = result->payload.find("locals");
    if (it == result->payload.end()) return vars;
    const mi::Value& locals = it->second;
    if (!locals.isList()) return vars;

    for (const auto& item : locals.asList()) {
        if (item.isTuple()) {
            Variable v;
            v.name  = item.getString("name",  "?");
            v.type  = item.getString("type",  "?");
            v.value = item.getString("value", "?");
            if (v.name.size() >= 2 && v.name[0] == '_' && v.name[1] == '_') continue;
            vars.push_back(v);
        }
    }
    return vars;
}

std::vector<StackFrame> GDBController::getStackFrames() {
    sendCommand("-stack-list-frames");
    std::string response = readUntilResult(3000);
    auto records = mi::parse(response);
    auto* result = mi::findRecord(records, mi::RecordType::Result, "done");
    if (!result) return {};

    std::vector<StackFrame> frames;
    auto it = result->payload.find("stack");
    if (it == result->payload.end()) return frames;
    const mi::Value& stack = it->second;
    if (!stack.isList()) return frames;

    for (const auto& item : stack.asList()) {
        if (item.isTuple()) {
            StackFrame f;
            f.level    = item.getInt   ("level", 0);
            f.function = item.getString("func",  "??");
            f.file     = item.getString("file",  "??");
            f.line     = item.getInt   ("line",  0);
            frames.push_back(f);
        }
    }
    return frames;
}

bool GDBController::isExitReason(const std::string& miOutput) {
    return miOutput.find("exited-normally") != std::string::npos ||
           miOutput.find("exited")          != std::string::npos;
}

} // namespace ccv
