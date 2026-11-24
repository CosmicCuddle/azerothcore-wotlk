#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

namespace PB59
{
// Copy native scalar arrays into values; never retain a pointer into Player.
template<class T, std::size_t N>
auto CopyCombatArray(T const (&values)[N])
{
    if constexpr (std::is_array_v<T>)
    {
        std::array<decltype(CopyCombatArray(values[0])), N> result{};
        for (std::size_t index = 0; index < N; ++index)
            result[index] = CopyCombatArray(values[index]);
        return result;
    }
    else
        return std::to_array(values);
}
}
