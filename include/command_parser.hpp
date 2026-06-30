#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace bytecachedb {

enum class CommandType {
    SET,
    GET,
    DEL,
    EXISTS,
    EXPIRE,
    EXPIREAT,
    TTL,
    PERSIST,
    KEYS,
    FLUSH,
    PING,
    INFO,
    MSET,
    MGET,
    INCR,
    DECR,
    APPEND,
    STRLEN,
    UNKNOWN
};

struct Command {
    CommandType type{CommandType::UNKNOWN};
    std::vector<std::string> args;
    std::string raw;
};

struct ParseResult {
    bool ok{false};
    Command command;
    std::string error;
};

class CommandParser {
public:
    ParseResult parse(std::string_view line) const;

    static std::string command_name(CommandType type);
    static bool is_mutating(CommandType type);

private:
    static CommandType type_from_token(const std::string& token);
    static std::string uppercase(std::string value);
    static std::vector<std::string> tokenize(std::string_view line);
    static bool validate_argument_count(CommandType type,
                                        size_t count,
                                        std::string& error);
};

} // namespace bytecachedb
