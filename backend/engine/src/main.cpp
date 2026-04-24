#include "gdb_controller.hpp"
#include "types.hpp"

#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>

// Escape a string for JSON output
static std::string jsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

// Serialize a ProgramState to JSON string
static std::string stateToJson(const ccv::ProgramState& state) {
    std::ostringstream out;
    out << "{";
    out << "\"status\":\"" << jsonEscape(state.status) << "\",";
    out << "\"line\":" << state.currentLine << ",";

    // Variables
    out << "\"vars\":[";
    for (size_t i = 0; i < state.variables.size(); i++) {
        if (i > 0) out << ",";
        const auto& v = state.variables[i];
        out << "{\"name\":\"" << jsonEscape(v.name) << "\","
            << "\"type\":\"" << jsonEscape(v.type) << "\","
            << "\"value\":\"" << jsonEscape(v.value) << "\"}";
    }
    out << "],";

    // Stack
    out << "\"stack\":[";
    for (size_t i = 0; i < state.stack.size(); i++) {
        if (i > 0) out << ",";
        const auto& f = state.stack[i];
        out << "{\"level\":" << f.level << ","
            << "\"func\":\"" << jsonEscape(f.function) << "\","
            << "\"file\":\"" << jsonEscape(f.file) << "\","
            << "\"line\":" << f.line << "}";
    }
    out << "],";

    // Output and error
    out << "\"output\":\"" << jsonEscape(state.output) << "\",";
    out << "\"error\":\"" << jsonEscape(state.errorMessage) << "\"";
    out << "}";

    return out.str();
}

// Extract a JSON string value for a given key from raw JSON text
static std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Find the colon after the key
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // skip opening quote

    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            switch (next) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                default:   result += next; break;
            }
            pos += 2;
        } else if (c == '"') {
            return result;
        } else {
            result += c;
            pos++;
        }
    }
    return result;
}

// Main

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: visualizer <work_dir>" << std::endl;
        return 1;
    }

    std::string workDir = argv[1];
    std::filesystem::create_directories(workDir);

    ccv::GDBController gdb;
    std::string binaryPath;
    std::string sourcePath;
    std::string sourceCode;
    bool compiled = false;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::string cmd = jsonGetString(line, "cmd");

        if (cmd == "compile") {
            sourceCode = jsonGetString(line, "source");
            auto result = gdb.compile(sourceCode, workDir);

            if (result.success) {
                compiled = true;
                binaryPath = result.binaryPath;
                sourcePath = result.sourcePath;

                // Send back success and source lines
                std::ostringstream out;
                out << "{\"success\":true,\"warnings\":\""
                    << jsonEscape(result.warnings) << "\",\"lines\":[";

                auto lines = sourceCode;
                std::istringstream ss(lines);
                std::string l;
                bool first = true;
                while (std::getline(ss, l)) {
                    if (!first) out << ",";
                    out << "\"" << jsonEscape(l) << "\"";
                    first = false;
                }
                out << "]}";

                std::cout << out.str() << std::endl;
            } else {
                std::cout << "{\"success\":false,\"error\":\""
                          << jsonEscape(result.error) << "\"}" << std::endl;
            }
        }
        else if (cmd == "start") {
            if (!compiled) {
                std::cout << "{\"status\":\"error\",\"error\":\"Not compiled\"}" << std::endl;
                continue;
            }
            try {
                auto state = gdb.start(binaryPath, sourcePath);
                std::cout << stateToJson(state) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "{\"status\":\"error\",\"error\":\""
                          << jsonEscape(e.what()) << "\"}" << std::endl;
            }
        }
        else if (cmd == "next") {
            try {
                auto state = gdb.stepOver();
                std::cout << stateToJson(state) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "{\"status\":\"error\",\"error\":\""
                          << jsonEscape(e.what()) << "\"}" << std::endl;
            }
        }
        else if (cmd == "step") {
            try {
                auto state = gdb.stepInto();
                std::cout << stateToJson(state) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "{\"status\":\"error\",\"error\":\""
                          << jsonEscape(e.what()) << "\"}" << std::endl;
            }
        }
        else if (cmd == "continue") {
            try {
                auto state = gdb.continueExec();
                std::cout << stateToJson(state) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "{\"status\":\"error\",\"error\":\""
                          << jsonEscape(e.what()) << "\"}" << std::endl;
            }
        }
        else if (cmd == "stop") {
            gdb.stop();
            std::cout << "{\"status\":\"stopped\"}" << std::endl;
        }
        else if (cmd == "quit") {
            gdb.stop();
            break;
        }
        else {
            std::cout << "{\"status\":\"error\",\"error\":\"Unknown command: "
                      << jsonEscape(cmd) << "\"}" << std::endl;
        }

        std::cout.flush(); // Flush
    }

    gdb.stop();
    return 0;
}
