#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace PB59::Recovery::ReadbackWire
{
// Diagnostic decoding only. Input must already be an exact, complete byte
// slice from a trusted query producer. Neither these functions nor a decoded
// row prove receipt provenance, a consistent snapshot or inventory custody.
using Cell = std::optional<std::string_view>;

template<std::integral T> requires (!std::same_as<T, bool>)
[[nodiscard]] bool Integer(Cell const& cell, T& output) noexcept
{
    if (!cell || cell->empty())
        return false;
    auto bytes = *cell;
    // A canonical SQL decimal projection has no leading zero, plus sign,
    // padding, negative zero, or redundant representation.
    if (bytes.front() == '-')
    {
        if constexpr (!std::is_signed_v<T>)
            return false;
        bytes.remove_prefix(1);
        if (bytes.empty() || bytes.front() == '0')
            return false;
    }
    if (bytes.size() > 1 && bytes.front() == '0')
        return false;
    for (char c : bytes)
        if (c < '0' || c > '9')
            return false;
    T parsed{};
    auto const* begin = cell->data();
    auto const* end = begin + cell->size();
    auto const result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end)
        return false;
    output = parsed;
    return true;
}

// NULL is distinct from empty bytes, and byte counts are not character counts.
// Build the owned result before modifying output, including on allocation failure.
[[nodiscard]] inline bool Text(Cell const& cell, std::size_t maximumBytes,
    bool nullable, std::optional<std::string>& output)
{
    if (!cell)
    {
        if (!nullable)
            return false;
        output.reset();
        return true;
    }
    if (cell->size() > maximumBytes)
        return false;
    std::optional<std::string> value(std::in_place, *cell);
    output.swap(value);
    return true;
}
}
