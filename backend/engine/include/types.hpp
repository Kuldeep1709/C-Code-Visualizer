#ifndef CCV_TYPES_HPP
#define CCV_TYPES_HPP

#include <string>
#include <vector>

namespace ccv {

struct Variable {
    std::string name;
    std::string type;
    std::string value;
    std::string address;
    std::vector<Variable> elements;  // For arrays/structs
    bool isArray = false;
    bool isString = false;
    bool isPointer = false;
};

struct StackFrame {
    int level = 0;
    std::string function;
    std::string file;
    int line = 0;
};

struct ProgramState {
    std::string status = "idle";
    int currentLine = 0;
    std::vector<Variable> variables;
    std::vector<StackFrame> stack;
    std::string output;
    std::string errorMessage;
};

struct CompileResult {
    bool success = false;
    std::string error;
    std::string warnings;
    std::string binaryPath;
    std::string sourcePath;
};

} // namespace ccv

#endif // CCV_TYPES_HPP
