#include "F14InitialOutPassAdmission.h"
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return EXIT_FAILURE; } } while (false)

int main()
{
    using CosmicF14::InitialOutPassAdmission;

    InitialOutPassAdmission a;
    CHECK(!a.IsOpen());
    CHECK(a.Token() == 0);
    a.Open(42);
    CHECK(a.IsOpen());
    CHECK(a.Token() == 42);

    InitialOutPassAdmission copied(a);
    CHECK(!copied.IsOpen());
    CHECK(copied.Token() == 0);
    CHECK(a.IsOpen() && a.Token() == 42);

    InitialOutPassAdmission moved(static_cast<InitialOutPassAdmission&&>(a));
    CHECK(!moved.IsOpen() && moved.Token() == 0);
    CHECK(!a.IsOpen() && a.Token() == 0);

    InitialOutPassAdmission b;
    b.Open(77);
    CHECK(b.Token() == 77);
    b.Close();
    CHECK(!b.IsOpen() && b.Token() == 0);
    b.Open(88);
    CHECK(!b.IsOpen() && b.Token() == 0);

    InitialOutPassAdmission invalid;
    invalid.Open(0);
    CHECK(!invalid.IsOpen() && invalid.Token() == 0);

    std::cout << "PASS initial OUT admission token is readable only while exact original notifier admission is open and never transfers\n";
    return EXIT_SUCCESS;
}