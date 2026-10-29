#pragma once
/**
 * What one SwiftSearch::compact() run did. It lives apart from swiftSearch.cpp
 * so supervise.hpp, which only forward declares SwiftSearch, can name it.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace swiftsearch {

struct CompactReport {
    std::size_t segmentsRemoved = 0;
    std::uint64_t bytesRemoved = 0;     ///< size of the segment files deleted
    std::uint64_t bytesCopied = 0;      ///< live records and tombstones written forward
    bool stopped = false;               ///< should_stop said so before every chosen segment was done
    std::vector<std::string> problems;  ///< segments kept because a record would not read, and why
};

}  // namespace swiftsearch
