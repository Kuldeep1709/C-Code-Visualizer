#ifndef CCV_MI_PARSER_HPP
#define CCV_MI_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <variant>

namespace mi {

struct Value;
using Tuple = std::map<std::string, Value>;
using List = std::vector<Value>;

struct Value {
    std::variant<std::string, Tuple, List> data;

    Value() : data("") {}
    Value(const std::string& s) : data(s) {}
    Value(const Tuple& t) : data(t) {}
    Value(const List& l) : data(l) {}

    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isTuple() const { return std::holds_alternative<Tuple>(data); }
    bool isList() const { return std::holds_alternative<List>(data); }

    const std::string& asString() const {
        static const std::string empty;
        if (isString()) return std::get<std::string>(data);
        return empty;
    }

    const Tuple& asTuple() const {
        static const Tuple empty;
        if (isTuple()) return std::get<Tuple>(data);
        return empty;
    }

    const List& asList() const {
        static const List empty;
        if (isList()) return std::get<List>(data);
        return empty;
    }

    std::string getString(const std::string& key, const std::string& defaultVal = "") const;
    int getInt(const std::string& key, int defaultVal = 0) const;
};

enum class RecordType { Result, Exec, Status, Notify, Console, Target, Log, Unknown };

struct Record {
    RecordType type = RecordType::Unknown;
    std::string klass;
    Tuple payload;
    std::string streamVal;
};

std::vector<Record> parse(const std::string& output);
Record parseLine(const std::string& line);
const Record* findRecord(const std::vector<Record>& records, RecordType type, const std::string& klass = "");

namespace detail {
    std::string parseCString(const std::string& text);
    std::string parseCStringAt(const std::string& text, size_t& pos);
    Tuple parseTuple(const std::string& text, size_t& pos);
    List parseList(const std::string& text, size_t& pos);
    Value parseValue(const std::string& text, size_t& pos);
    Tuple parseKeyValuePairs(const std::string& text, size_t& pos);
}

} // namespace mi

#endif // CCV_MI_PARSER_HPP
