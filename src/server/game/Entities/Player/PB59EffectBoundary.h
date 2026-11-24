#pragma once

namespace PB59
{
// Synchronous serialized-owner protocol only. The verifier MUST first test a
// retained lifetime sentinel before dereferencing native owner/item objects.
// This does not pin them, restore effects or authorize inventory mutation.
class EffectBoundary final
{
public:
    using Verify = bool (*)(void*);
    EffectBoundary(void const* owner, void* context, Verify verify) noexcept
        : _owner(owner), _context(context), _verify(verify), _previous(_active)
    {
        if (auto* outer = Find(owner))
        {
            outer->_failed = true;
            _failed = true;
        }
        _failed = _failed || !owner || !verify;
        _active = this;
    }
    ~EffectBoundary() { _active = _previous; }
    EffectBoundary(EffectBoundary const&) = delete;
    EffectBoundary& operator=(EffectBoundary const&) = delete;

    // No frame means an ordinary native call: preserve existing behavior.
    // Matching uses pointer identity only, never dereferences the owner.
    static bool Check(void const* owner) noexcept
    {
        auto* frame = Find(owner);
        if (!frame)
            return true;
        if (frame->_failed || frame->_checking)
        {
            frame->_failed = true;
            return false;
        }
        frame->_checking = true;
        try
        {
            bool const verified = frame->_verify(frame->_context);
            frame->_checking = false;
            if (verified && !frame->_failed)
                return true;
        }
        catch (...)
        {
        }
        frame->_checking = false;
        frame->_failed = true;
        return false;
    }
    [[nodiscard]] bool Failed() const noexcept { return _failed; }
private:
    static EffectBoundary* Find(void const* owner) noexcept
    {
        for (auto* frame = _active; frame; frame = frame->_previous)
            if (frame->_owner == owner)
                return frame;
        return nullptr;
    }
    inline static thread_local EffectBoundary* _active = nullptr;
    void const* _owner;
    void* _context;
    Verify _verify;
    EffectBoundary* _previous;
    bool _failed = false;
    bool _checking = false;
};
}
