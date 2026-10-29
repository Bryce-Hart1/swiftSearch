/**
 * Entry point.
 *
 * `serve` starts Supervise (supervise.hpp), which opens the SwiftSearch store in
 * --store and puts the HTTP dock (src/http/) in front of it. Everything stored
 * is on disk and survives a restart.
 *
 * `list` is still unbound, so box listings answer 501 until there is a prefix
 * scan behind them.
 *
 * The V1 interactive boot() loop is NOT restored: it drives the parked search
 * engine in legacy/. The batch command line designed in CLI.md arrives with the
 * storage stages.
 */

#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

#include "constants.hpp"
#include "http/routes.hpp"
#include "http/server.hpp"
#include "supervise.hpp"
#include "call.hpp"

namespace {

using namespace swiftsearch;
using namespace swiftsearch::http;

// Exit codes from CLI.md section 3.6.
constexpr int EXIT_USAGE = 1;
constexpr int EXIT_NO_PATH = 2;
constexpr int EXIT_INTERNAL = 3;

/// Set before the handler is installed, read only by it. A signal handler may
/// touch a lock-free atomic and call write(), which is all requestStop does.
std::atomic<Supervise*> g_supervisor{nullptr};

void onSignal(int) {
    if (Supervise* s = g_supervisor.load(std::memory_order_acquire)) s->requestStop();
}

void usage() {
    using namespace std;
    println(cout, "usage: swiftSearch serve [options]");
    println(cout, "");
    println(cout, "  serve                  run the store behind the HTTP dock");
    println(cout, "");
    println(cout, "Options:");
    println(cout, "      --store DIR        folder the store lives in (default: swiftsearch_data)");
    println(cout, "      --bind ADDR        address to listen on (default: 127.0.0.1)");
    println(cout, "  -p, --port N           port to listen on (default: 8080)");
    println(cout, "  -j, --threads N        request worker threads (default: CPU count)");
    println(cout, "      --mem-cap SIZE     bytes of cached values kept in RAM, e.g. 512K, 64M, 2G (default: 64M)");
    println(cout, "      --move-percent N   most of the memory cap one prepare may load, 1-100 (default: 25)");
    println(cout, "      --cpu-threads N    store worker threads, 0 = CPU count (default: 0)");
    println(cout, "      --cpu-high P       busy percent the store backs off at (default: 85)");
    println(cout, "      --cpu-low P        busy percent the store ramps up below (default: 60)");
    println(cout, "      --compact-idle T   compact after this long with no reads or writes, e.g. 90s, 10m, 1h;");
    println(cout, "                         0 = never (default: 10m)");
    println(cout, "      --compact-dead-percent N");
    println(cout, "                         rewrite segments at least this percent dead, 1-100 (default: 50)");
    println(cout, "  -h, --help             this text");
    println(cout, "");
    println(cout, "  The caps can be changed while running with PUT /v1/admin/limits,");
    println(cout, "  e.g. ?mem=128M&cpu_threads=4&compact_idle=5m. That route only answers on a loopback bind.");
    println(cout, "");
    println(cout, "  The default bind address is loopback. There is NO authentication,");
    println(cout, "  so --bind 0.0.0.0 exposes every stored blob to the whole network.");
}

bool parseUint(std::string_view s, unsigned long& out) {
    if (s.empty()) return false;
    unsigned long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        if (v > (ULONG_MAX - static_cast<unsigned long>(c - '0')) / 10) return false;
        v = v * 10 + static_cast<unsigned long>(c - '0');
    }
    out = v;
    return true;
}

int serve(int argc, char** argv) {
    SupervisorOptions opt;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto next = [&](std::string_view& dest) {
            if (i + 1 >= argc) return false;
            dest = argv[++i];
            return true;
        };
        // A flag taking a whole number up to `max`, written to `dest`.
        const auto number = [&](std::string_view what, unsigned long max, std::size_t& dest) {
            std::string_view value;
            unsigned long n = 0;
            if (!next(value) || !parseUint(value, n) || n > max) {
                std::println(stderr, "{} needs a number from 0 to {}", what, max);
                return false;
            }
            dest = static_cast<std::size_t>(n);
            return true;
        };
        std::string_view value;
        if (a == "--store") {
            if (!next(value)) { std::println(stderr, "--store needs a folder"); return EXIT_USAGE; }
            opt.storePath = std::string(value);
        } else if (a == "--bind") {
            if (!next(value)) { std::println(stderr, "--bind needs an address"); return EXIT_USAGE; }
            opt.server.bindAddress = std::string(value);
        } else if (a == "-p" || a == "--port") {
            std::size_t n = 0;
            if (!number("--port", 65535, n)) return EXIT_USAGE;
            opt.server.port = static_cast<std::uint16_t>(n);
        } else if (a == "-j" || a == "--threads") {
            unsigned long n = 0;
            if (!next(value) || !parseUint(value, n) || n == 0) {
                std::println(stderr, "--threads needs a positive number");
                return EXIT_USAGE;
            }
            opt.server.requestThreads = static_cast<std::size_t>(n);
        } else if (a == "--mem-cap") {
            const auto bytes = next(value) ? Supervise::parseByteSize(value) : std::nullopt;
            if (!bytes) {
                std::println(stderr, "--mem-cap needs a size such as 1048576, 512K, 64M or 2G");
                return EXIT_USAGE;
            }
            opt.memCapBytes = static_cast<std::size_t>(*bytes);
        } else if (a == "--move-percent") {
            if (!number("--move-percent", 100, opt.movePercent)) return EXIT_USAGE;
        } else if (a == "--cpu-threads") {
            if (!number("--cpu-threads", Supervise::MAX_CPU_THREADS, opt.cpuThreads)) return EXIT_USAGE;
        } else if (a == "--cpu-high") {
            if (!number("--cpu-high", 100, opt.cpuHigh)) return EXIT_USAGE;
        } else if (a == "--cpu-low") {
            if (!number("--cpu-low", 100, opt.cpuLow)) return EXIT_USAGE;
        } else if (a == "--compact-idle") {
            const auto idle = next(value) ? Supervise::parseDuration(value) : std::nullopt;
            if (!idle) {
                std::println(stderr, "--compact-idle needs a time such as 600, 90s, 10m or 1h");
                return EXIT_USAGE;
            }
            opt.compactIdle = *idle;
        } else if (a == "--compact-dead-percent") {
            if (!number("--compact-dead-percent", 100, opt.compactDeadPercent)) return EXIT_USAGE;
        } else {
            std::println(stderr, "unknown option: {}", a);
            return EXIT_USAGE;
        }
    }

    // Must happen before start(), which refuses to run otherwise: a client
    // vanishing mid-response would kill the process on SIGPIPE.
    Server::ignoreSigpipe();

    const std::string bindShown = opt.server.bindAddress;
    const std::string storeShown = opt.storePath.string();
    std::unique_ptr<Supervise> supervisor;
    try {
        supervisor = std::make_unique<Supervise>(std::move(opt), print::swiftSearch_logbook);
    } catch (const std::invalid_argument& e) {
        std::println(stderr, "{}", e.what());
        return EXIT_USAGE;
    } catch (const std::exception& e) {
        std::println(stderr, "could not open the store in {}: {}", storeShown, e.what());
        return EXIT_NO_PATH;
    }
    for (const std::string& w : supervisor->Warnings()) {
        std::println(stderr, "warning: {}", w);
    }

    const auto port = supervisor->start();
    if (!port) {
        std::println(stderr, "could not start: {}", std::strerror(port.error()));
        if (port.error() == EPERM) {
            std::println(stderr, "(SIGPIPE is still SIG_DFL)");
        }
        return EXIT_INTERNAL;
    }

    g_supervisor.store(supervisor.get(), std::memory_order_release);
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    // No SA_RESTART, so an in-flight poll() returns EINTR promptly instead of
    // waiting out its timeout before noticing the stop.
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    const Supervise::Limits caps = supervisor->limits();
    print::Logo();
    std::println("listening on http://{}:{}", bindShown, *port);
    std::println("  GET/HEAD/PUT/DELETE /v1/blobs/<key>");
    std::println("  POST                /v1/prepare/<key>  (load into memory)");
    std::println("  GET/PUT             /v1/boxes/<key>    (501: no prefix scan yet)");
    std::println("  GET                 /healthz");
    if (supervisor->adminEnabled()) {
        std::println("  GET/PUT             {}", Supervise::ADMIN_LIMITS_PATH);
    } else {
        std::println("  admin route off: {} is not a loopback address", bindShown);
    }
    std::println("");
    std::println("store: {}", storeShown);
    std::println("memory cap {} bytes, one prepare up to {}%, {} store threads",
                 caps.memCapBytes, caps.movePercent, caps.cpuThreads == 0 ? std::string("auto")
                                                                          : std::to_string(caps.cpuThreads));
    if (caps.compactIdleSecs == 0) {
        std::println("automatic compaction off");
    } else {
        std::println("compaction after {}s idle, on segments at least {}% dead",
                     caps.compactIdleSecs, caps.compactDeadPercent);
    }
    std::println("ctrl-c to stop.");

    // Blocks here for the life of the process. The signal handler above calls
    // requestStop(), which is what makes the reactor thread return and this
    // wait finish; stop() then drains in-flight requests and joins the pool.
    supervisor->wait();
    supervisor->stop();
    g_supervisor.store(nullptr, std::memory_order_release);
    std::println("stopped.");
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string_view command = argv[1];
        if (command == "serve") return serve(argc, argv);
        if (command == "-h" || command == "--help") {
            usage();
            return EXIT_SUCCESS;
        }
        std::println(stderr, "unknown command: {}", command);
        usage();
        return EXIT_USAGE;
    }

    print::Logo();
    call();
    std::println("swiftSearch V2 - storage engine under construction.");
    std::println("The record format, its codec and the HTTP dock are built and tested.");
    std::println("Run `swiftSearch serve` to start the dock, or --help for options.");
    std::println("Run ./build/bin/swiftSearch_tests for the test suite.");
    return EXIT_SUCCESS;
}
