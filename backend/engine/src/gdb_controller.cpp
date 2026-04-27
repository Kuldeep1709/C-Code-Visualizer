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
    std::string response = readUntilStopped(10000);
    drainPTY();
    if (isExitReason(response)) {
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
    state.status = "paused";

    // Fetch current frame info once and reuse for both line number and variable filtering
    sendCommand("-stack-info-frame");
    std::string frameResp = readUntilResult(3000);
    auto frameRecords = mi::parse(frameResp);
    auto* frameResult = mi::findRecord(frameRecords, mi::RecordType::Result, "done");

    // Extract current line from frame
    state.currentLine = -1;
    std::string currentFrameFile;
    if (frameResult) {
        auto it = frameResult->payload.find("frame");
        if (it != frameResult->payload.end()) {
            state.currentLine = it->second.getInt("line", -1);
            currentFrameFile = it->second.getString("fullname", "");
            if (currentFrameFile.empty())
                currentFrameFile = it->second.getString("file", "");
        }
    }

    state.variables = getLocalVariables(currentFrameFile);
    state.stack     = getStackFrames();
    state.output    = programOutput_;
    return state;
}

// Note: getCurrentLine() is now unused — line extraction is done in getCurrentState().
// Kept for potential future use.
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

std::vector<Variable> GDBController::getLocalVariables(const std::string& currentFrameFile) {
    // Only show locals if we're in the user's source file (skip system/library frames)
    if (!sourcePath_.empty() && !currentFrameFile.empty() && currentFrameFile != "??") {
        std::string srcBase = std::filesystem::path(sourcePath_).filename().string();
        std::string frmBase = std::filesystem::path(currentFrameFile).filename().string();
        if (frmBase != srcBase) return {};
    }

    sendCommand("-stack-list-variables --all-values");
    std::string response = readUntilResult(3000);
    auto records = mi::parse(response);
    auto* result = mi::findRecord(records, mi::RecordType::Result, "done");
    if (!result) return {};

    std::vector<Variable> vars;
    auto it = result->payload.find("variables");
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

            // If the MI list didn't include a type field (common in older GDB versions
            // where --all-values only returns name+value), fetch it with `whatis`.
            if (v.type == "?") {
                sendCommand("-interpreter-exec console \"whatis " + v.name + "\"");
                std::string whatisResp = readUntilResult(2000);
                // The GDB MI console stream emits: ~"type = <typename>\n"
                // We search for "type = " and read until the GDB C-string escape \n or closing "
                const std::string marker = "type = ";
                auto mpos = whatisResp.find(marker);
                if (mpos != std::string::npos) {
                    mpos += marker.size();
                    size_t tend = mpos;
                    while (tend < whatisResp.size()) {
                        // GDB MI C-strings end their line with \n (two chars: backslash + n)
                        if (whatisResp[tend] == '\\' && tend + 1 < whatisResp.size() && whatisResp[tend + 1] == 'n')
                            break;
                        if (whatisResp[tend] == '"') break;  // end of C-string token
                        tend++;
                    }
                    if (tend > mpos) {
                        v.type = whatisResp.substr(mpos, tend - mpos);
                        // Trim trailing whitespace
                        while (!v.type.empty() && (v.type.back() == ' ' || v.type.back() == '\t'))
                            v.type.pop_back();
                    }
                }
            }

            // Get memory address using GDB address-of operator
            sendCommand("-data-evaluate-expression \"&" + v.name + "\"");
            std::string addrResp = readUntilResult(2000);
            auto addrRecords = mi::parse(addrResp);
            auto* addrResult = mi::findRecord(addrRecords, mi::RecordType::Result, "done");
            if (addrResult) {
                auto valIt = addrResult->payload.find("value");
                if (valIt != addrResult->payload.end()) {
                    std::string addrStr = valIt->second.getString("value", "");
                    // Extract address from format like "$1 = 0x7fff5fbff8ac"
                    size_t pos = addrStr.find("0x");
                    if (pos != std::string::npos) {
                        v.address = addrStr.substr(pos);
                    }
                }
            }

            // Detect arrays (type contains '[')
            if (v.type.find("[") != std::string::npos && v.type.find("]") != std::string::npos) {
                v.isArray = true;
                // Parse array size from type like "int[5]" or "char[100]"
                size_t start = v.type.find("[");
                size_t end = v.type.find("]");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string sizeStr = v.type.substr(start + 1, end - start - 1);
                    int arraySize = 0;
                    try { arraySize = std::stoi(sizeStr); } catch (...) { arraySize = 0; }

                    if (arraySize > 0 && arraySize <= 1000) {
                        // Fetch array elements
                        for (int i = 0; i < arraySize; i++) {
                            sendCommand("-data-evaluate-expression \"" + v.name + "[" + std::to_string(i) + "]\"");
                            std::string elemResp = readUntilResult(2000);
                            auto elemRecords = mi::parse(elemResp);
                            auto* elemResult = mi::findRecord(elemRecords, mi::RecordType::Result, "done");
                            if (elemResult) {
                                auto valIt = elemResult->payload.find("value");
                                if (valIt != elemResult->payload.end()) {
                                    Variable elem;
                                    elem.name = "[" + std::to_string(i) + "]";
                                    elem.value = valIt->second.getString("value", "?");
                                    elem.type = v.type.substr(0, start);  // Element type

                                    // Get element address
                                    sendCommand("-data-evaluate-expression \"&" + v.name + "[" + std::to_string(i) + "]\"");
                                    std::string elemAddrResp = readUntilResult(2000);
                                    auto elemAddrRecords = mi::parse(elemAddrResp);
                                    auto* elemAddrResult = mi::findRecord(elemAddrRecords, mi::RecordType::Result, "done");
                                    if (elemAddrResult) {
                                        auto elemValIt = elemAddrResult->payload.find("value");
                                        if (elemValIt != elemAddrResult->payload.end()) {
                                            std::string elemAddrStr = elemValIt->second.getString("value", "");
                                            size_t pos = elemAddrStr.find("0x");
                                            if (pos != std::string::npos) {
                                                elem.address = elemAddrStr.substr(pos);
                                            }
                                        }
                                    }

                                    v.elements.push_back(elem);
                                }
                            }
                        }
                    }
                }
            }
            // Detect char pointers (strings)
            else if (v.type == "char *" || v.type.find("char *") != std::string::npos) {
                v.isPointer = true;
                v.isString = true;
                // Fetch string content and address
                sendCommand("-data-evaluate-expression \"" + v.name + "\"");
                std::string strResp = readUntilResult(2000);
                auto strRecords = mi::parse(strResp);
                auto* strResult = mi::findRecord(strRecords, mi::RecordType::Result, "done");
                if (strResult) {
                    auto valIt = strResult->payload.find("value");
                    if (valIt != strResult->payload.end()) {
                        std::string strVal = valIt->second.getString("value", "");
                        // GDB returns strings like "\"hello\\000\""
                        if (!strVal.empty() && strVal[0] == '"') {
                            // Extract characters
                            size_t idx = 1;
                            int charIdx = 0;
                            while (idx < strVal.size() && strVal[idx] != '"' && charIdx < 50) {
                                Variable ch;
                                ch.name = "[" + std::to_string(charIdx) + "]";
                                if (strVal[idx] == '\\' && idx + 1 < strVal.size()) {
                                    // Escape sequence
                                    if (strVal[idx + 1] == '0') {
                                        ch.value = "\\0";
                                        v.elements.push_back(ch);
                                        break;  // Null terminator
                                    } else if (strVal[idx + 1] == 'n') {
                                        ch.value = "\\n";
                                    } else if (strVal[idx + 1] == 't') {
                                        ch.value = "\\t";
                                    } else {
                                        ch.value = std::string(1, strVal[idx + 1]);
                                    }
                                    idx += 2;
                                } else {
                                    ch.value = std::string(1, strVal[idx]);
                                    idx++;
                                }
                                v.elements.push_back(ch);
                                charIdx++;
                            }
                        }
                    }
                }
            }
            // Detect pointers
            else if (v.type.find("*") != std::string::npos) {
                v.isPointer = true;
                // Get pointed-to value
                sendCommand("-data-evaluate-expression \"*" + v.name + "\"");
                std::string ptrResp = readUntilResult(2000);
                auto ptrRecords = mi::parse(ptrResp);
                auto* ptrResult = mi::findRecord(ptrRecords, mi::RecordType::Result, "done");
                if (ptrResult) {
                    auto valIt = ptrResult->payload.find("value");
                    if (valIt != ptrResult->payload.end()) {
                        Variable deref;
                        deref.name = "*" + v.name;
                        deref.value = valIt->second.getString("value", "?");
                        v.elements.push_back(deref);
                    }
                }
            }

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
        if (!item.isTuple()) continue;

        const mi::Value* frameVal = &item;
        const auto& entry = item.asTuple();
        auto frameIt = entry.find("frame");
        if (frameIt != entry.end() && frameIt->second.isTuple()) {
            frameVal = &frameIt->second;
        }

        StackFrame f;
        f.level    = frameVal->getInt   ("level", 0);
        f.function = frameVal->getString("func",  "??");
        f.file     = frameVal->getString("fullname", "");
        if (f.file.empty()) f.file = frameVal->getString("file", "??");
        f.line     = frameVal->getInt   ("line",  0);
        frames.push_back(f);
    }
    return frames;
}

bool GDBController::isExitReason(const std::string& miOutput) {
    return miOutput.find("exited-normally") != std::string::npos ||
           miOutput.find("exited")          != std::string::npos;
}

} // namespace ccv
