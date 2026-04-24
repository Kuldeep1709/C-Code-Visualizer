#include "mi_parser.hpp"
#include <sstream>
#include <stdexcept>

namespace mi {

// Value Methods

std::string Value::getString(const std::string& key, const std::string& defaultVal) const {
    if (!isTuple()) return defaultVal;
    const auto& t = asTuple();
    auto it = t.find(key);
    if (it == t.end()) return defaultVal;
    if (it->second.isString()) return it->second.asString();
    return defaultVal;
}

int Value::getInt(const std::string& key, int defaultVal) const {
    std::string s = getString(key, "");
    if (s.empty()) return defaultVal;
    try { return std::stoi(s); }
    catch (...) { return defaultVal; }
}

// Internal Parsing Helpers

namespace detail {

// Parse a C-string starting at position pos
std::string parseCStringAt(const std::string& text, size_t& pos) {
    if (pos >= text.size() || text[pos] != '"') return "";
    pos++; 

    std::string result;
    while (pos < text.size()) {
        char c = text[pos];
        if (c == '\\' && pos + 1 < text.size()) {
            char next = text[pos + 1];
            switch (next) {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                default:   result += next; break;
            }
            pos += 2;
        } 
        else if (c == '"') {
            pos++; 
            return result;
        } 
        else {
            result += c;
            pos++;
        }
    }
    return result;
}

// Parse a standalone C-string
std::string parseCString(const std::string& text) {
    std::string trimmed = text;
    // Remove leading/trailing whitespace
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    size_t end   = trimmed.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    trimmed = trimmed.substr(start, end - start + 1);

    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        size_t pos = 0;
        return parseCStringAt(trimmed, pos);
    }
    return trimmed;
}

// Parse a tuple
Tuple parseTuple(const std::string& text, size_t& pos) {
    Tuple result;
    if (pos >= text.size() || text[pos] != '{') return result;
    pos++; 

    while (pos < text.size() && text[pos] != '}') {
        // Skip whitespace and commas
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == ','))
            pos++;
        if (pos >= text.size() || text[pos] == '}') break;

        // Read key
        size_t keyStart = pos;
        while (pos < text.size() && text[pos] != '=' && text[pos] != '}')
            pos++;
        if (pos >= text.size() || text[pos] == '}') break;

        std::string key = text.substr(keyStart, pos - keyStart);
        pos++; 

        // Read value
        Value val = parseValue(text, pos);
        result[key] = val;
    }

    if (pos < text.size() && text[pos] == '}') pos++;
    return result;
}

// Parse a list
List parseList(const std::string& text, size_t& pos) {
    List result;
    if (pos >= text.size() || text[pos] != '[') return result;
    pos++; 

    while (pos < text.size() && text[pos] != ']') {
        // Skip whitespace and commas
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == ','))
            pos++;
        if (pos >= text.size() || text[pos] == ']') break;

        size_t lookahead = pos;
        bool isKeyValue = false;
        while (lookahead < text.size() && text[lookahead] != ',' &&
               text[lookahead] != ']' && text[lookahead] != '"' &&
               text[lookahead] != '{' && text[lookahead] != '[') {
            if (text[lookahead] == '=') {
                isKeyValue = true;
                break;
            }
            lookahead++;
        }

        if (isKeyValue) {
            while (pos < text.size() && text[pos] != '=') pos++;
            pos++; 
            Value val = parseValue(text, pos);
            result.push_back(val);
        } else {
            Value val = parseValue(text, pos);
            result.push_back(val);
        }
    }

    if (pos < text.size() && text[pos] == ']') pos++;
    return result;
}

// Parse a value string, tuple, or list starting at position pos
Value parseValue(const std::string& text, size_t& pos) {
    if (pos >= text.size()) return Value("");

    char c = text[pos];
    if (c == '"') {
        std::string s = parseCStringAt(text, pos);
        return Value(s);
    } else if (c == '{') {
        Tuple t = parseTuple(text, pos);
        return Value(t);
    } else if (c == '[') {
        List l = parseList(text, pos);
        return Value(l);
    } else {
        // Bare value — read until delimiter
        size_t start = pos;
        while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']')
            pos++;
        return Value(text.substr(start, pos - start));
    }
}

// Parse comma separated key=value pairs
Tuple parseKeyValuePairs(const std::string& text, size_t& pos) {
    Tuple result;
    while (pos < text.size()) {
        // Skip whitespace and commas
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == ','))
            pos++;
        if (pos >= text.size()) break;

        // Read key
        size_t keyStart = pos;
        while (pos < text.size() && text[pos] != '=')
            pos++;
        if (pos >= text.size()) break;

        std::string key = text.substr(keyStart, pos - keyStart);
        pos++; // skip =

        // Read value
        Value val = parseValue(text, pos);
        result[key] = val;
    }
    return result;
}

} // namespace detail

// Public Parser API

std::vector<Record> parse(const std::string& output) {
    std::vector<Record> records;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty() || line == "(gdb)" || line == "(gdb) ") continue;

        try {
            Record record = parseLine(line);
            records.push_back(std::move(record));
        } catch (...) {
            // Skip unparseable lines
        }
    }

    return records;
}

Record parseLine(const std::string& line) {
    Record record;

    if (line.empty()) {
        record.type = RecordType::Log;
        return record;
    }

    char prefix = line[0];
    std::string rest = line.substr(1);

    switch (prefix) {
        case '~':
            record.type = RecordType::Console;
            record.streamVal = detail::parseCString(rest);
            return record;

        case '@':
            record.type = RecordType::Target;
            record.streamVal = detail::parseCString(rest);
            return record;

        case '&':
            record.type = RecordType::Log;
            record.streamVal = detail::parseCString(rest);
            return record;

        case '^': record.type = RecordType::Result; break;
        case '*': record.type = RecordType::Exec;   break;
        case '+': record.type = RecordType::Status;  break;
        case '=': record.type = RecordType::Notify;  break;

        default:
            record.type = RecordType::Log;
            record.streamVal = line;
            return record;
    }

    size_t commaPos = rest.find(',');
    if (commaPos == std::string::npos) {
        record.klass = rest;
    } else {
        record.klass = rest.substr(0, commaPos);
        std::string payloadStr = rest.substr(commaPos + 1);
        size_t pos = 0;
        record.payload = detail::parseKeyValuePairs(payloadStr, pos);
    }

    return record;
}

const Record* findRecord(const std::vector<Record>& records,
                         RecordType type,
                         const std::string& klass) {
    for (const auto& r : records) {
        if (r.type == type) {
            if (klass.empty() || r.klass == klass) {
                return &r;
            }
        }
    }
    return nullptr;
}

} // namespace mi
