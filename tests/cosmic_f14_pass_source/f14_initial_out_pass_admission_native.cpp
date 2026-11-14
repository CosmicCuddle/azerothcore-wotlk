// CosmicCuddle F14: native regression for actual Core14 admission helper.
#include "F14InitialOutPassAdmission.h"
#include <cstdlib>
#include <iostream>
#include <utility>

using CosmicF14::InitialOutPassAdmission;
static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::cerr << "FAIL line " << __LINE__ << ": " << #expr << '\n'; return EXIT_FAILURE; } } while (false)
int main()
{
    InitialOutPassAdmission fresh;
    CHECK(!fresh.IsOpen());
    fresh.Open(1);
    CHECK(fresh.IsOpen());
    fresh.Close();
    CHECK(!fresh.IsOpen());
    fresh.Open(2);
    CHECK(!fresh.IsOpen());
    InitialOutPassAdmission zero;
    zero.Open(0);
    CHECK(!zero.IsOpen());
    zero.Open(3);
    CHECK(!zero.IsOpen());
    InitialOutPassAdmission duplicate;
    duplicate.Open(4);
    CHECK(duplicate.IsOpen());
    duplicate.Open(4);
    CHECK(!duplicate.IsOpen());
    duplicate.Open(5);
    CHECK(!duplicate.IsOpen());
    InitialOutPassAdmission different;
    different.Open(6);
    different.Open(7);
    CHECK(!different.IsOpen());

    InitialOutPassAdmission source;
    source.Open(8);
    InitialOutPassAdmission copied(source);
    CHECK(source.IsOpen());
    CHECK(!copied.IsOpen());
    copied.Open(9);
    CHECK(!copied.IsOpen());
    InitialOutPassAdmission assigned;
    assigned.Open(10);
    assigned = source;
    CHECK(!assigned.IsOpen());
    CHECK(source.IsOpen());
    assigned.Open(11);
    CHECK(!assigned.IsOpen());

    InitialOutPassAdmission moved(std::move(source));
    CHECK(!source.IsOpen());
    CHECK(!moved.IsOpen());
    moved.Open(12);
    CHECK(!moved.IsOpen());
    source.Open(13);
    CHECK(!source.IsOpen());
    InitialOutPassAdmission moveFrom;
    moveFrom.Open(14);
    InitialOutPassAdmission moveTo;
    moveTo.Open(15);
    moveTo = std::move(moveFrom);
    CHECK(!moveTo.IsOpen());
    CHECK(!moveFrom.IsOpen());

    InitialOutPassAdmission emptyPass;
    emptyPass.Close();
    emptyPass.Open(16);
    CHECK(!emptyPass.IsOpen());
    InitialOutPassAdmission independent;
    independent.Open(17);
    CHECK(independent.IsOpen());
    CHECK(!emptyPass.IsOpen());
    std::cout << "PASS actual InitialOutPassAdmission: " << checks << " checks\n";
    return EXIT_SUCCESS;
}
