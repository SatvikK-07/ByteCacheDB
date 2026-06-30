#include "command_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace bytecachedb {

ParseResult CommandParser::parse(std::string_view line) const {
    auto tokens = tokenize(line);
    if (tokens.empty()) {
        return {false, {}, "empty command"};
    }

    const auto type = type_from_token(tokens.front());
    if (type == CommandType::UNKNOWN) {
        return {false, {}, "unknown command"};
    }

    std::string error;
    const size_t arg_count = tokens.size() - 1;
    if (!validate_argument_count(type, arg_count, error)) {
        return {false, {}, error};
    }

    Command command;
    command.type = type;
    command.raw.assign(line.begin(), line.end());
    command.args.assign(tokens.begin() + 1, tokens.end());
    return {true, std::move(command), ""};
}

std::string CommandParser::command_name(CommandType type) {
    switch (type) {
        case CommandType::SET:
            return "SET";
        case CommandType::GET:
            return "GET";
        case CommandType::DEL:
            return "DEL";
        case CommandType::EXISTS:
            return "EXISTS";
        case CommandType::EXPIRE:
            return "EXPIRE";
        case CommandType::EXPIREAT:
            return "EXPIREAT";
        case CommandType::TTL:
            return "TTL";
        case CommandType::PERSIST:
            return "PERSIST";
        case CommandType::KEYS:
            return "KEYS";
        case CommandType::FLUSH:
            return "FLUSH";
        case CommandType::PING:
            return "PING";
        case CommandType::INFO:
            return "INFO";
        case CommandType::MSET:
            return "MSET";
        case CommandType::MGET:
            return "MGET";
        case CommandType::INCR:
            return "INCR";
        case CommandType::DECR:
            return "DECR";
        case CommandType::APPEND:
            return "APPEND";
        case CommandType::STRLEN:
            return "STRLEN";
        case CommandType::UNKNOWN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

bool CommandParser::is_mutating(CommandType type) {
    switch (type) {
        case CommandType::SET:
        case CommandType::DEL:
        case CommandType::EXPIRE:
        case CommandType::EXPIREAT:
        case CommandType::PERSIST:
        case CommandType::FLUSH:
        case CommandType::MSET:
        case CommandType::INCR:
        case CommandType::DECR:
        case CommandType::APPEND:
            return true;
        default:
            return false;
    }
}

CommandType CommandParser::type_from_token(const std::string& token) {
    static const std::unordered_map<std::string, CommandType> kTypes = {
        {"SET", CommandType::SET},       {"GET", CommandType::GET},
        {"DEL", CommandType::DEL},       {"EXISTS", CommandType::EXISTS},
        {"EXPIRE", CommandType::EXPIRE}, {"EXPIREAT", CommandType::EXPIREAT},
        {"TTL", CommandType::TTL},       {"PERSIST", CommandType::PERSIST},
        {"KEYS", CommandType::KEYS},     {"FLUSH", CommandType::FLUSH},
        {"PING", CommandType::PING},     {"INFO", CommandType::INFO},
        {"MSET", CommandType::MSET},     {"MGET", CommandType::MGET},
        {"INCR", CommandType::INCR},     {"DECR", CommandType::DECR},
        {"APPEND", CommandType::APPEND}, {"STRLEN", CommandType::STRLEN},
    };

    const auto it = kTypes.find(uppercase(token));
    return it == kTypes.end() ? CommandType::UNKNOWN : it->second;
}

std::string CommandParser::uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::vector<std::string> CommandParser::tokenize(std::string_view line) {
    std::string normalized(line);
    if (!normalized.empty() && normalized.back() == '\r') {
        normalized.pop_back();
    }

    std::istringstream input(normalized);
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool CommandParser::validate_argument_count(CommandType type,
                                            size_t count,
                                            std::string& error) {
    auto exact = [&](size_t expected) {
        if (count != expected) {
            error = command_name(type) + " expects " + std::to_string(expected) + " argument";
            if (expected != 1) {
                error += "s";
            }
            return false;
        }
        return true;
    };

    switch (type) {
        case CommandType::PING:
        case CommandType::KEYS:
        case CommandType::FLUSH:
        case CommandType::INFO:
            return exact(0);
        case CommandType::GET:
        case CommandType::DEL:
        case CommandType::EXISTS:
        case CommandType::TTL:
        case CommandType::PERSIST:
        case CommandType::INCR:
        case CommandType::DECR:
        case CommandType::STRLEN:
            return exact(1);
        case CommandType::SET:
        case CommandType::EXPIRE:
        case CommandType::EXPIREAT:
        case CommandType::APPEND:
            return exact(2);
        case CommandType::MGET:
            if (count == 0) {
                error = "MGET expects at least 1 argument";
                return false;
            }
            return true;
        case CommandType::MSET:
            if (count < 2 || count % 2 != 0) {
                error = "MSET expects an even number of key/value arguments";
                return false;
            }
            return true;
        case CommandType::UNKNOWN:
            error = "unknown command";
            return false;
    }
    error = "unknown command";
    return false;
}

} // namespace bytecachedb
