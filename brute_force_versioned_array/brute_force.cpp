#include "brute_force.hpp"

#include <iostream>
#include <numeric>

/*
=========================================================
Constructors
=========================================================
*/

BruteForceVersionedArray::BruteForceVersionedArray()
{
}

BruteForceVersionedArray::BruteForceVersionedArray(
    const std::vector<ValueType>& initial)
{
    initialize(initial);
}

/*
=========================================================
Initialization
=========================================================
*/

void BruteForceVersionedArray::initialize(
    const std::vector<ValueType>& initial)
{
    versions.clear();
    versions.push_back(initial);
}

/*
=========================================================
Version Management
=========================================================
*/

std::size_t BruteForceVersionedArray::createVersion(
    std::size_t baseVersion)
{
    validateVersion(baseVersion);

    versions.push_back(versions[baseVersion]);

    return versions.size() - 1;
}

/*
=========================================================
Range Update
=========================================================
*/

std::size_t BruteForceVersionedArray::rangeAdd(
    std::size_t baseVersion,
    std::size_t left,
    std::size_t right,
    ValueType value)
{
    validateVersion(baseVersion);
    validateRange(baseVersion, left, right);

    std::size_t newVersion = createVersion(baseVersion);

    for (std::size_t i = left; i <= right; ++i)
    {
        versions[newVersion][i] += value;
    }

    return newVersion;
}

/*
=========================================================
Range Query
=========================================================
*/

BruteForceVersionedArray::ValueType
BruteForceVersionedArray::rangeSum(
    std::size_t version,
    std::size_t left,
    std::size_t right) const
{
    validateVersion(version);
    validateRange(version, left, right);

    ValueType sum = 0;

    for (std::size_t i = left; i <= right; ++i)
    {
        sum += versions[version][i];
    }

    return sum;
}

/*
=========================================================
Accessors
=========================================================
*/

const std::vector<BruteForceVersionedArray::ValueType>&
BruteForceVersionedArray::getVersion(
    std::size_t version) const
{
    validateVersion(version);

    return versions[version];
}

std::size_t BruteForceVersionedArray::versionCount() const
{
    return versions.size();
}

std::size_t BruteForceVersionedArray::size() const
{
    if (versions.empty())
    {
        return 0;
    }

    return versions.front().size();
}

/*
=========================================================
Debug Utilities
=========================================================
*/

void BruteForceVersionedArray::printVersion(
    std::size_t version) const
{
    validateVersion(version);

    std::cout << "Version "
              << version
              << " : ";

    for (ValueType value : versions[version])
    {
        std::cout << value << " ";
    }

    std::cout << std::endl;
}

/*
=========================================================
Validation
=========================================================
*/

void BruteForceVersionedArray::validateVersion(
    std::size_t version) const
{
    if (version >= versions.size())
    {
        throw std::out_of_range(
            "Invalid version number.");
    }
}

void BruteForceVersionedArray::validateRange(
    std::size_t version,
    std::size_t left,
    std::size_t right) const
{
    if (versions.empty())
    {
        throw std::runtime_error(
            "Versioned array has not been initialized.");
    }

    const std::size_t n = versions[version].size();

    if (left > right)
    {
        throw std::invalid_argument(
            "Left index is greater than right index.");
    }

    if (right >= n)
    {
        throw std::out_of_range(
            "Range exceeds array size.");
    }
}