#include "../../lazy_segment_tree/lazy_segment_tree.hpp"

#include <cassert>
#include <iostream>

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

    LazySegmentTree tree({1,2,3,4,5});

    tree.rangeAdd(0,3,2);
    tree.rangeAdd(2,4,5);

    assert(tree.rangeSum(0,4) == 38);
    assert(tree.rangeSum(2,3) == 21);

    cout << "PASS\n" << endl;
}

void testManyQueries()
{
    cout << "[TEST] Multiple Queries..." << endl;

    LazySegmentTree tree({2,4,6,8,10});

    assert(tree.rangeSum(0,1) == 6);
    assert(tree.rangeSum(2,4) == 24);

    tree.rangeAdd(1,3,1);

    assert(tree.rangeSum(0,4) == 33);
    assert(tree.rangeSum(1,3) == 21);

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

    cout << "======================================" << endl;
    cout << "All Lazy Segment Tree Tests Passed!" << endl;
    cout << "======================================" << endl;
}

int main()
{
    runAllTests();

    return 0;
}