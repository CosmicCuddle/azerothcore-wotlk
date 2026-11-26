#pragma once

#include "PB59CompleteReadbackResult.h"
#include "PB59ReadbackRowDecoder.h"

namespace PB59::Recovery::ReadbackWire
{
namespace Detail
{
// Non-owning views into already consumed, owned capture rows. These views
// must not outlive the owned rows or escape into the decoded output.
template<std::size_t N>
class CapturedViews final
{
public:
    explicit CapturedViews(typename CompleteReadbackResult<N>::Rows const& owned) noexcept
    {
        if (owned.size() > MaximumOriginals)
            return;
        for (std::size_t i = 0; i < owned.size(); ++i)
        {
            for (std::size_t j = 0; j < N; ++j)
                if (owned[i][j])
                    _cells[i][j] = std::string_view(*owned[i][j]);
            _rows[i] = _cells[i];
        }
        _size = owned.size();
        _valid = true;
    }
    CapturedViews(CapturedViews const&) = delete;
    CapturedViews& operator=(CapturedViews const&) = delete;
    [[nodiscard]] Table View(std::span<std::string_view const> columns) const noexcept
    {
        if (!_valid)
            return {};
        return {columns, std::span<Row const>(_rows).first(_size)};
    }
private:
    std::array<std::array<Cell, N>, MaximumOriginals> _cells{};
    std::array<Row, MaximumOriginals> _rows{};
    std::size_t _size = 0;
    bool _valid = false;
};
}

// Diagnostic bridge, not a snapshot/receipt proof. The three captures and
// column labels must originate from the eventual trusted query producer.
// Consume all three captures even on failure; no retry using a partial batch.
[[nodiscard]] inline std::optional<InventoryReadbackRows> DecodeCompleteReadback(
    CompleteReadbackResult<4>& links, std::span<std::string_view const> linkColumns,
    CompleteReadbackResult<14>& instances, std::span<std::string_view const> instanceColumns,
    CompleteReadbackResult<4>& gifts, std::span<std::string_view const> giftColumns) noexcept
{
    auto l = links.Take();
    auto i = instances.Take();
    auto g = gifts.Take();
    if (!l || !i || !g || l->size() > MaximumOriginals || i->size() > MaximumOriginals || g->size() > MaximumOriginals)
        return std::nullopt;
    try
    {
        Detail::CapturedViews<4> lv(*l), gv(*g);
        Detail::CapturedViews<14> iv(*i);
        return DecodeRows(lv.View(linkColumns), iv.View(instanceColumns), gv.View(giftColumns), MaximumOriginals);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
