#include "command_parser.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace bytecachedb {

ParseResult CommandParser::parse(std::string_view line) const {
    std::vector<std::string> tokens;
    std::string token_error;
    if (!tokenize(line, tokens, token_error)) {
        return {false, {}, token_error};
    }
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
        case CommandType::SAVE:
            return "SAVE";
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
        case CommandType::SAVE:
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
        {"SAVE", CommandType::SAVE},
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

bool CommandParser::tokenize(std::string_view line,
                             std::vector<std::string>& tokens,
                             std::string& error) {
    std::string token;
    bool in_quotes = false;
    bool escaping = false;
    bool token_started = false;

    const size_t length = !line.empty() && line.back() == '\r' ? line.size() - 1 : line.size();
    for (size_t i = 0; i < length; ++i) {
        const char c = line[i];
        if (escaping) {
            switch (c) {
                case 'n':
                    token.push_back('\n');
                    break;
                case 'r':
                    token.push_back('\r');
                    break;
                case 't':
                    token.push_back('\t');
                    break;
                default:
                    token.push_back(c);
                    break;
            }
            escaping = false;
            token_started = true;
            continue;
        }
        if (c == '\\' && in_quotes) {
            escaping = true;
            token_started = true;
            continue;
        }
        if (c == '"') {
            in_quotes = !in_quotes;
            token_started = true;
            continue;
        }
        if (!in_quotes && std::isspace(static_cast<unsigned char>(c))) {
            if (token_started) {
                tokens.push_back(std::move(token));
                token.clear();
                token_started = false;
            }
            continue;
        }
        token.push_back(c);
        token_started = true;
    }

    if (escaping || in_quotes) {
        error = escaping ? "unterminated escape sequence" : "unterminated quoted string";
        return false;
    }
    if (token_started) {
        tokens.push_back(std::move(token));
    }
    return true;
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
        case CommandType::SAVE:
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
