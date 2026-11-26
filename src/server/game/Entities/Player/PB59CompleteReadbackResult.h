#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace PB59::Recovery::ReadbackWire
{
// Result capture only, not a database adapter. The eventual native producer
// must supply actual fetch status, accessible initialized bytes and metadata
// from its own query. These values alone cannot prove snapshot or receipt identity.
enum class FetchStatus { Row, End, Truncated, Error };
struct FetchedCell
{
    std::span<char const> buffer;
    std::size_t reportedLength;
    bool isNull;
    bool error;
};

template<std::size_t N>
class CompleteReadbackResult final
{
    static_assert(N > 0);
    enum class State { Reading, Complete, Failed, Taken };

public:
    using OwnedRow = std::array<std::optional<std::string>, N>;
    using Rows = std::vector<OwnedRow>;

    CompleteReadbackResult(std::size_t advertisedRows, std::size_t rowLimit,
        std::array<std::size_t, N> byteLimits) noexcept
        : _advertisedRows(advertisedRows), _byteLimits(byteLimits),
          _state(rowLimit && advertisedRows <= rowLimit ? State::Reading : State::Failed) {}

    CompleteReadbackResult(CompleteReadbackResult const&) = delete;
    CompleteReadbackResult& operator=(CompleteReadbackResult const&) = delete;

    // A null cell's length must be normalized to zero by the producer. Its
    // unused backing buffer need not be empty. Never read beyond the reported
    // bytes or the independently established accessible buffer extent.
    [[nodiscard]] bool Observe(FetchStatus status, std::span<FetchedCell const> cells = {})
    {
        if (_state != State::Reading)
            return Fail();
        if (status == FetchStatus::End)
        {
            if (!cells.empty() || _rows.size() != _advertisedRows)
                return Fail();
            _state = State::Complete;
            return true;
        }
        if (status != FetchStatus::Row || cells.size() != N || _rows.size() >= _advertisedRows)
            return Fail();
        for (std::size_t i = 0; i < N; ++i)
        {
            auto const& cell = cells[i];
            if (cell.error || (cell.isNull && cell.reportedLength != 0) ||
                cell.reportedLength > cell.buffer.size() || cell.reportedLength > _byteLimits[i])
                return Fail();
        }
        try
        {
            OwnedRow row;
            for (std::size_t i = 0; i < N; ++i)
            {
                auto const& cell = cells[i];
                if (!cell.isNull)
                {
                    if (cell.reportedLength == 0)
                        row[i].emplace();
                    else
                        row[i].emplace(cell.buffer.data(), cell.reportedLength);
                }
            }
            _rows.push_back(std::move(row));
        }
        catch (...)
        {
            return Fail();
        }
        return true;
    }

    // Only a verified end-of-stream after exactly the advertised row count
    // can yield bytes. Even an early Take permanently invalidates the capture.
    [[nodiscard]] std::optional<Rows> Take() noexcept
    {
        if (_state != State::Complete)
        {
            (void)Fail();
            return std::nullopt;
        }
        _state = State::Taken;
        return std::move(_rows);
    }

private:
    bool Fail() noexcept
    {
        _state = State::Failed;
        _rows.clear();
        return false;
    }

    std::size_t const _advertisedRows;
    std::array<std::size_t, N> const _byteLimits;
    State _state;
    Rows _rows;
};
}
