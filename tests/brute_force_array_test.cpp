#include <valseg/brute_force_array.hpp>

#include <cassert>
#include <iostream>
#include <vector>

using std::cout;
using std::endl;
using valseg::BruteForceArray;

void testInitialization()
{
    cout << "[TEST] Initialization..." << endl;

    BruteForceArray arr({1,2,3,4,5});

    assert(arr.versionCount() == 1);
    assert(arr.size() == 5);

    assert(arr.rangeSum(0,0,4) == 15);

    cout << "PASS\n" << endl;
}

void testSingleRangeUpdate()
{
    cout << "[TEST] Single Range Update..." << endl;

    BruteForceArray arr({1,2,3,4,5});

    std::size_t v1 = arr.rangeAdd(0,1,3,5);

    assert(v1 == 1);

    // Version 0 unchanged
    assert(arr.rangeSum(0,0,4) == 15);

    // Version 1
    assert(arr.rangeSum(v1,0,4) == 30);

    assert(arr.rangeSum(v1,1,3) == 24);

    cout << "PASS\n" << endl;
}

void testMultipleVersions()
{
    cout << "[TEST] Multiple Versions..." << endl;

    BruteForceArray arr({1,2,3,4,5});

    std::size_t v1 = arr.rangeAdd(0,0,2,10);

    std::size_t v2 = arr.rangeAdd(v1,2,4,-2);

    // Version 0

    assert(arr.rangeSum(0,0,4) == 15);

    // Version 1

    assert(arr.rangeSum(v1,0,4) == 45);

    // Version 2

    assert(arr.rangeSum(v2,0,4) == 39);

    cout << "PASS\n" << endl;
}

void testHistoricalQueries()
{
    cout << "[TEST] Historical Queries..." << endl;

    BruteForceArray arr({10,20,30,40});

    std::size_t v1 = arr.rangeAdd(0,0,3,5);

    std::size_t v2 = arr.rangeAdd(v1,1,2,10);

    // Old versions must remain unchanged

    assert(arr.rangeSum(0,0,3) == 100);

    assert(arr.rangeSum(v1,0,3) == 120);

    assert(arr.rangeSum(v2,0,3) == 140);

    cout << "PASS\n" << endl;
}

void testNegativeUpdates()
{
    cout << "[TEST] Negative Updates..." << endl;

    BruteForceArray arr({5,5,5,5});

    std::size_t v1 = arr.rangeAdd(0,0,3,-2);

    assert(arr.rangeSum(v1,0,3) == 12);

    cout << "PASS\n" << endl;
}

void testSingleElementUpdate()
{
    cout << "[TEST] Single Element Update..." << endl;

    BruteForceArray arr({1,2,3});

    std::size_t v1 = arr.rangeAdd(0,1,1,10);

    assert(arr.rangeSum(v1,1,1) == 12);

    assert(arr.rangeSum(0,1,1) == 2);

    cout << "PASS\n" << endl;
}

void testWholeArrayUpdate()
{
    cout << "[TEST] Whole Array Update..." << endl;

    BruteForceArray arr({1,1,1,1});

    std::size_t v1 = arr.rangeAdd(0,0,3,3);

    assert(arr.rangeSum(v1,0,3) == 16);

    cout << "PASS\n" << endl;
}

void testVersionIsolation()
{
    cout << "[TEST] Version Isolation..." << endl;

    BruteForceArray arr({1,2,3});

    std::size_t v1 = arr.rangeAdd(0,0,0,10);

    std::size_t v2 = arr.rangeAdd(v1,1,1,20);

    // Ensure Version 1 was not modified after Version 2

    assert(arr.rangeSum(v1,0,2) == 16);

    assert(arr.rangeSum(v2,0,2) == 36);

    cout << "PASS\n" << endl;
}

void runAllTests()
{
    testInitialization();

    testSingleRangeUpdate();

    testMultipleVersions();

    testHistoricalQueries();

    testNegativeUpdates();

    testSingleElementUpdate();

    testWholeArrayUpdate();

    testVersionIsolation();

    cout << "======================================" << endl;
    cout << "All deterministic tests passed!" << endl;
    cout << "======================================" << endl;
}

int main()
{
    runAllTests();

    return 0;
}