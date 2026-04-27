#ifndef CCV_GDB_CONTROLLER_HPP
#define CCV_GDB_CONTROLLER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <sys/types.h>

namespace ccv {

class GDBController {
public:
    GDBController();
    ~GDBController();

    CompileResult compile(const std::string& sourceCode, const std::string& workDir);
    ProgramState start(const std::string& binaryPath, const std::string& sourcePath);
    ProgramState stepOver();
    ProgramState stepInto();
    ProgramState continueExec();
    void stop();

private:
    void spawnGDB(const std::string& binaryPath);
    void sendCommand(const std::string& command);
    std::string readLine(int timeoutMs);
    std::string readUntilResult(int timeoutMs);
    std::string readUntilStopped(int timeoutMs);
    void drainPTY();
    ProgramState getCurrentState();
    int getCurrentLine();
    std::vector<Variable> getLocalVariables(const std::string& currentFrameFile);
    std::vector<StackFrame> getStackFrames();
    std::string readCapturedOutput();
    bool isExitReason(const std::string& miOutput);

    pid_t gdbPid_ = -1;
    int stdinPipe_[2] = {-1, -1};
    int stdoutPipe_[2] = {-1, -1};
    int ptyMaster_ = -1;
    std::string readBuffer_;
    std::string programOutput_;
    std::string workDir_;
    std::string sourcePath_;
};

} // namespace ccv

#endif // CCV_GDB_CONTROLLER_HPP
