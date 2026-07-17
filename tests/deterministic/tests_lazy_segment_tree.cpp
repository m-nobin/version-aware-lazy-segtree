#include "../../lazy_segment_tree/lazy_segment_tree.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <stdexcept>

using std::cout;
using std::endl;

void testInitialization()
{
    cout << "[TEST] Initialization..." << endl;

    LazySegmentTree tree({1, 2, 3, 4, 5});

    assert(tree.size() == 5);
    assert(tree.rangeSum(0, 4) == 15);

    cout << "PASS\n" << endl;
}

void testSingleRangeUpdate()
{
    cout << "[TEST] Single Range Update..." << endl;

    LazySegmentTree tree({1, 2, 3, 4, 5});

    tree.rangeAdd(1, 3, 5);

    assert(tree.rangeSum(0, 4) == 30);
    assert(tree.rangeSum(1, 3) == 24);

    cout << "PASS\n" << endl;
}

void testMultipleUpdates()
{
    cout << "[TEST] Multiple Updates..." << endl;

    LazySegmentTree tree({1, 2, 3, 4, 5});

    tree.rangeAdd(0, 2, 10);
    tree.rangeAdd(2, 4, -2);

    assert(tree.rangeSum(0, 4) == 39);

    cout << "PASS\n" << endl;
}

void testWholeArrayUpdate()
{
    cout << "[TEST] Whole Array Update..." << endl;

    LazySegmentTree tree({1, 1, 1, 1});

    tree.rangeAdd(0, 3, 3);

    assert(tree.rangeSum(0, 3) == 16);

    cout << "PASS\n" << endl;
}

void testSingleElementUpdate()
{
    cout << "[TEST] Single Element Update..." << endl;

    LazySegmentTree tree({5, 5, 5});

    tree.rangeAdd(1, 1, 7);

    assert(tree.rangeSum(1, 1) == 12);
    assert(tree.rangeSum(0, 2) == 22);

    cout << "PASS\n" << endl;
}

void testNegativeUpdates()
{
    cout << "[TEST] Negative Updates..." << endl;

    LazySegmentTree tree({10, 10, 10, 10});

    tree.rangeAdd(0, 3, -3);

    assert(tree.rangeSum(0, 3) == 28);

    cout << "PASS\n" << endl;
}

void testOverlappingUpdates()
{
    cout << "[TEST] Overlapping Updates..." << endl;

    LazySegmentTree tree({1, 2, 3, 4, 5});

    tree.rangeAdd(0, 3, 2);
    tree.rangeAdd(2, 4, 5);

    assert(tree.rangeSum(0, 4) == 38);
    assert(tree.rangeSum(2, 3) == 21);

    cout << "PASS\n" << endl;
}

void testManyQueries()
{
    cout << "[TEST] Multiple Queries..." << endl;

    LazySegmentTree tree({2, 4, 6, 8, 10});

    assert(tree.rangeSum(0, 1) == 6);
    assert(tree.rangeSum(2, 4) == 24);

    tree.rangeAdd(1, 3, 1);

    assert(tree.rangeSum(0, 4) == 33);
    assert(tree.rangeSum(1, 3) == 21);

    cout << "PASS\n" << endl;
}

/*
=========================================================
Edge Case Tests
=========================================================
*/

void testZeroDeltaUpdate()
{
    cout << "[TEST] Zero Delta Update..." << endl;

    LazySegmentTree tree({1, 2, 3, 4, 5});

    tree.rangeAdd(0, 4, 0);

    assert(tree.rangeSum(0, 4) == 15);
    assert(tree.rangeSum(1, 3) == 9);

    cout << "PASS\n" << endl;
}

void testSingleElementArray()
{
    cout << "[TEST] Single Element Array..." << endl;

    LazySegmentTree tree({42});

    assert(tree.rangeSum(0, 0) == 42);

    tree.rangeAdd(0, 0, 8);

    assert(tree.rangeSum(0, 0) == 50);

    cout << "PASS\n" << endl;
}

void testEmptyTree()
{
    cout << "[TEST] Empty Tree..." << endl;

    LazySegmentTree tree;

    bool exceptionThrown = false;

    try
    {
        tree.rangeSum(0, 0);
    }
    catch (const std::runtime_error&)
    {
        exceptionThrown = true;
    }

    assert(exceptionThrown);

    exceptionThrown = false;

    try
    {
        tree.rangeAdd(0, 0, 5);
    }
    catch (const std::runtime_error&)
    {
        exceptionThrown = true;
    }

    assert(exceptionThrown);

    cout << "PASS\n" << endl;
}

void testLargeValues()
{
    cout << "[TEST] Large Values..." << endl;

    constexpr std::size_t n = 1000;
    constexpr long long value = 1000000000LL;

    std::vector<long long> data(n, value);

    LazySegmentTree tree(data);

    assert(tree.rangeSum(0, n - 1) == value * n);

    tree.rangeAdd(0, n - 1, value);

    assert(tree.rangeSum(0, n - 1) == value * n * 2);

    cout << "PASS\n" << endl;
}

void testOutOfRangeAccess()
{
    cout << "[TEST] Out-of-Range Access..." << endl;

    LazySegmentTree tree({1, 2, 3});

    bool exceptionThrown = false;

    try
    {
        tree.rangeSum(0, 3);
    }
    catch (const std::out_of_range&)
    {
        exceptionThrown = true;
    }

    assert(exceptionThrown);

    exceptionThrown = false;

    try
    {
        tree.rangeAdd(0, 5, 1);
    }
    catch (const std::out_of_range&)
    {
        exceptionThrown = true;
    }

    assert(exceptionThrown);

    cout << "PASS\n" << endl;
}

void runAllTests()
{
    testInitialization();
    testSingleRangeUpdate();
    testMultipleUpdates();
    testWholeArrayUpdate();
    testSingleElementUpdate();
    testNegativeUpdates();
    testOverlappingUpdates();
    testManyQueries();

    // Edge cases
    testZeroDeltaUpdate();
    testSingleElementArray();
    testEmptyTree();
    testLargeValues();
    testOutOfRangeAccess();

    cout << "======================================" << endl;
    cout << "All Lazy Segment Tree Tests Passed!" << endl;
    cout << "======================================" << endl;
}

int main()
{
    runAllTests();

    return 0;
}