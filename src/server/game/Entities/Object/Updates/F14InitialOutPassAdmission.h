/* CosmicCuddle F14: one-shot, non-transferable admission for initial OUT.
 * Negative Core prerequisite only. No actor rights, ledger, ACK or packets.
 */
#ifndef COSMIC_F14_INITIAL_OUT_PASS_ADMISSION_H
#define COSMIC_F14_INITIAL_OUT_PASS_ADMISSION_H
#include <cstdint>
namespace CosmicF14
{
    class InitialOutPassAdmission
    {
    public:
        InitialOutPassAdmission() = default;
        // UpdateData may be copied/moved by ordinary Core code. Such an object
        // must never inherit authority from the original notifier.
        InitialOutPassAdmission(InitialOutPassAdmission const&) noexcept : _sealed(true) {}
        InitialOutPassAdmission& operator=(InitialOutPassAdmission const&) noexcept
        {
            Close();
            return *this;
        }
        InitialOutPassAdmission(InitialOutPassAdmission&& other) noexcept : _sealed(true)
        {
            other.Close();
        }
        InitialOutPassAdmission& operator=(InitialOutPassAdmission&& other) noexcept
        {
            Close();
            other.Close();
            return *this;
        }
        void Open(std::uint64_t token) noexcept
        {
            if (_sealed || _token != 0 || token == 0)
            {
                Close();
                return;
            }
            _token = token;
        }
        void Close() noexcept
        {
            _token = 0;
            _sealed = true;
        }
        bool IsOpen() const noexcept { return !_sealed && _token != 0; }
    private:
        std::uint64_t _token = 0;
        bool _sealed = false;
    };
}
#endif
