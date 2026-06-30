#include "command_parser.hpp"
#include "test_framework.hpp"

using namespace bytecachedb;

int main() {
    CommandParser parser;
    int failures = 0;

    failures += test::run("valid SET", [&] {
        const auto parsed = parser.parse("SET name Satvik");
        ASSERT_TRUE(parsed.ok);
        ASSERT_EQ(CommandType::SET, parsed.command.type);
        ASSERT_EQ(std::string("name"), parsed.command.args[0]);
        ASSERT_EQ(std::string("Satvik"), parsed.command.args[1]);
    });

    failures += test::run("valid GET lowercase", [&] {
        const auto parsed = parser.parse("get name");
        ASSERT_TRUE(parsed.ok);
        ASSERT_EQ(CommandType::GET, parsed.command.type);
        ASSERT_EQ(std::string("name"), parsed.command.args[0]);
    });

    failures += test::run("unknown command", [&] {
        const auto parsed = parser.parse("NOPE key");
        ASSERT_FALSE(parsed.ok);
        ASSERT_EQ(std::string("unknown command"), parsed.error);
    });

    failures += test::run("wrong argument count", [&] {
        const auto parsed = parser.parse("SET only_key");
        ASSERT_FALSE(parsed.ok);
        ASSERT_TRUE(parsed.error.find("SET expects 2 arguments") != std::string::npos);
    });

    failures += test::run("extra spaces", [&] {
        const auto parsed = parser.parse("   EXISTS    a   ");
        ASSERT_TRUE(parsed.ok);
        ASSERT_EQ(CommandType::EXISTS, parsed.command.type);
        ASSERT_EQ(std::string("a"), parsed.command.args[0]);
    });

    failures += test::run("empty input", [&] {
        const auto parsed = parser.parse("   ");
        ASSERT_FALSE(parsed.ok);
        ASSERT_EQ(std::string("empty command"), parsed.error);
    });

    failures += test::run("MSET validates even arguments", [&] {
        ASSERT_TRUE(parser.parse("MSET a 1 b 2").ok);
        ASSERT_FALSE(parser.parse("MSET a 1 b").ok);
    });

    return failures == 0 ? 0 : 1;
}
