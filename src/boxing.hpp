#pragma once
/**
 * What SwiftSearch::Box and Unbox report. They live apart from swiftSearch.cpp
 * so supervise.hpp, which only forward declares SwiftSearch, can name them.
 *
 * A BOX is a key prefix, the same notion record.hpp and box_meta.hpp use: the box
 * `beach` is every key under `beach/`. Boxing two things re-keys everything in
 * them under the new name, and unboxing re-keys one layer back out.
 *
 *   box(beach_videos, beach_photo_231, beach)
 *       beach_videos/a.mp4     ->  beach/beach_videos/a.mp4
 *       beach_photo_231        ->  beach/beach_photo_231
 *   unbox(beach)
 *       beach/beach_videos/a.mp4  ->  beach_videos/a.mp4
 *       beach/beach_photo_231     ->  beach_photo_231
 *
 * Unboxing is one layer deep: beach_videos is still a box afterwards.
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace swiftsearch {

/// Why a box or unbox did nothing (or, for Internal, did not finish).
enum class BoxFailure : std::uint8_t {
    Invalid = 0,  ///< the arguments make no sense: a bad key, two names for one thing, a name inside what it holds
    NotFound,     ///< an operand has nothing stored at or under it
    Conflict,     ///< a key the move would create is already taken
    Internal,     ///< a disk or read failure; whatever was written is undone where it could be
};

struct BoxError {
    BoxFailure kind = BoxFailure::Internal;
    std::string message;  ///< a sentence naming the key at fault, fit for a client. For Internal: detail for the log only
};

struct BoxReport {
    std::size_t moved = 0;  ///< keys re-keyed
    /// Box: true when a new layer was made, false when the name was an existing
    /// box that the other operand was added to. Unbox: always false.
    bool created = false;
};

}  // namespace swiftsearch
