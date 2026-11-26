#include "PB59ReadbackWireValues.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
using namespace PB59::Recovery::ReadbackWire;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while (false)
int main()
{
    std::uint8_t u8 = 42;
    CHECK(Integer(Cell{"0"}, u8) && u8 == 0);
    CHECK(Integer(Cell{"255"}, u8) && u8 == 255);
    for (Cell c : {Cell{}, Cell{""}, Cell{"256"}, Cell{"-1"}, Cell{"+1"}, Cell{"01"}, Cell{" 1"}, Cell{"1 "}, Cell{"1x"}, Cell{"1.0"}, Cell{std::string_view("1\0", 2)}, Cell{"9999999999999999999999999"}})
        CHECK(!Integer(c, u8) && u8 == 255);
    std::int16_t i16 = 0;
    CHECK(Integer(Cell{"-32768"}, i16) && i16 == std::numeric_limits<std::int16_t>::min());
    CHECK(Integer(Cell{"32767"}, i16) && i16 == std::numeric_limits<std::int16_t>::max());
    for (Cell c : {Cell{"32768"}, Cell{"-32769"}, Cell{"-0"}, Cell{"-01"}, Cell{"-"}, Cell{"--1"}, Cell{"+1"}})
        CHECK(!Integer(c, i16) && i16 == 32767);
    std::uint32_t u32 = 7;
    CHECK(Integer(Cell{"4294967295"}, u32) && u32 == std::numeric_limits<std::uint32_t>::max());
    CHECK(!Integer(Cell{"4294967296"}, u32) && u32 == std::numeric_limits<std::uint32_t>::max());
    std::int32_t i32 = 7;
    CHECK(Integer(Cell{"-2147483648"}, i32) && i32 == std::numeric_limits<std::int32_t>::min());
    CHECK(Integer(Cell{"2147483647"}, i32) && i32 == std::numeric_limits<std::int32_t>::max());
    CHECK(!Integer(Cell{"2147483648"}, i32) && i32 == std::numeric_limits<std::int32_t>::max());
    std::uint64_t u64 = 7;
    CHECK(Integer(Cell{"18446744073709551615"}, u64) && u64 == std::numeric_limits<std::uint64_t>::max());
    CHECK(!Integer(Cell{"18446744073709551616"}, u64) && u64 == std::numeric_limits<std::uint64_t>::max());
    std::optional<std::string> text{"old"};
    CHECK(!Text(Cell{}, 3, false, text) && text == "old");
    CHECK(Text(Cell{}, 3, true, text) && !text);
    CHECK(Text(Cell{""}, 0, true, text) && text && text->empty());
    CHECK(Text(Cell{""}, 0, false, text) && text && text->empty());
    CHECK(Text(Cell{std::string_view("a\0b", 3)}, 3, false, text) && text == std::string("a\0b", 3));
    CHECK(!Text(Cell{"abcd"}, 3, true, text) && text == std::string("a\0b", 3));
    CHECK(Text(Cell{"a "}, 2, false, text) && text == "a ");
    std::string multibyte("\xc3\xa9", 2);
    CHECK(Text(Cell{multibyte}, 2, false, text) && text == multibyte);
    CHECK(!Text(Cell{multibyte}, 1, false, text) && text == multibyte);
    CHECK(Text(Cell{*text}, 2, false, text) && text == multibyte);
    CHECK(Text(Cell{std::string_view{}}, 0, false, text) && text && text->empty());
    std::cout << checks << '/' << checks << " wire value checks passed\n";
}
