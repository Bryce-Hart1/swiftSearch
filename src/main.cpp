/**
 * Entry point.
 *
 * Deliberately thin. This pass builds the on-disk record format and its codec;
 * there is no CLI yet and no storage to drive from one. The real command line is
 * designed in CLI.md and arrives with stage 1 ("hash a file and store it by its
 * hash"), which is the first point at which there is something for a subcommand
 * to do.
 *
 * This target exists now so the library/CLI seam in CMakeLists.txt stays real
 * and so `cmake --build` produces something runnable. The V1 interactive boot()
 * loop is NOT restored here: it drives the parked search engine in legacy/.
 */

#include <cstdlib>
#include <print>

#include "constants.hpp"

int main() {
    print::Logo();
    std::println("swiftSearch V2 - storage engine under construction.");
    std::println("The record format and its codec are built and tested; there is no CLI yet.");
    std::println("Run ./build/bin/swiftSearch_tests for the format test suite.");
    return EXIT_SUCCESS;
}
