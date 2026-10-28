/**
 * Entry point.
 *
 * `serve` starts the HTTP dock (src/http/). The store behind it is a
 * **throwaway in-memory map built right here in main**, not a component: the
 * real get/put/delete live in SwiftSearch and are being written separately.
 * That is the whole point of StoreBinding being a struct of std::functions --
 * the dock runs and can be exercised end to end before the storage engine
 * exists, and swapping this map for the real thing is a change to these lambdas
 * and nothing else.
 *
 * Nothing here persists. Restarting the process loses everything, and `list` is
 * unbound, so box listings answer 501 until there is a prefix scan behind them.
 *
 * The V1 interactive boot() loop is NOT restored: it drives the parked search
 * engine in legacy/. The batch command line designed in CLI.md arrives with the
 * storage stages.
 */

#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <print>
#include <string>
#include <string_view>

#include "constants.hpp"
#include "http/routes.hpp"
#include "http/server.hpp"
#include "call.hpp"

namespace {

using namespace swiftsearch;
using namespace swiftsearch::http;

// Exit codes from CLI.md section 3.6.
constexpr int EXIT_USAGE = 1;
constexpr int EXIT_INTERNAL = 3;

/// Set before the handler is installed, read only by it. A signal handler may
/// touch a lock-free atomic and call write(), which is all requestStop does.
std::atomic<Server*> g_server{nullptr};

void onSignal(int) {
    if (Server* s = g_server.load(std::memory_order_acquire)) s->requestStop();
}

/// The throwaway store. See the file comment: this is a stand-in, and it is
/// deliberately the least interesting code in the project.
class MemStore {
public:
    StoreBinding binding() {
        StoreBinding b;
        b.get = [this](std::string_view k) -> StoreResult<Fetched> {
            std::lock_guard<std::mutex> lock(_m);
            const auto it = _items.find(std::string(k));
            if (it == _items.end()) return std::unexpected(StoreStatus::NotFound);
            Fetched f;
            f.bytes = it->second.bytes;
            f.var_type = it->second.type;
            f.digest = it->second.digest;
            f.timestamp_ns = it->second.when;
            return f;
        };
        b.put = [this](std::string_view k, std::span<const std::uint8_t> p,
                       VAR_TYPE t) -> StoreResult<Stored> {
            std::lock_guard<std::mutex> lock(_m);
            const std::string key(k);
            const bool isNew = !_items.contains(key);
            Item item;
            item.bytes.assign(p.begin(), p.end());
            item.type = t;
            item.digest = sha256(p);
            item.when = nowNanos();
            // Encoded through the real record format even though it is thrown
            // away, so the dock is exercised against the actual Envelope path
            // rather than a shortcut: an oversized key or payload fails here
            // exactly as it would against a segment file.
            try {
                //the stand-in has no start date, so the record's seconds offset is 0
                (void)Envelope::value(key, item.bytes, 0, t).encode();
            } catch (const std::length_error&) {
                return std::unexpected(StoreStatus::Unsupported);
            }
            _items[key] = std::move(item);
            Stored s;
            s.digest = _items[key].digest;
            s.size = p.size();
            s.created = isNew;
            return s;
        };
        b.remove = [this](std::string_view k) -> StoreResult<bool> {
            std::lock_guard<std::mutex> lock(_m);
            return _items.erase(std::string(k)) > 0;
        };
        // `list` and `putBox` are left UNBOUND on purpose, so the box routes
        // answer 501 rather than pretending a box is empty. They start working
        // the moment the real store has a prefix scan.
        return b;
    }

private:
    struct Item {
        Bytes bytes;
        VAR_TYPE type = VAR_TYPE::BIN;
        Digest digest{};
        std::int64_t when = 0;
    };
    std::mutex _m;
    std::map<std::string, Item> _items;
};

void usage() {
    using namespace std;
    println(cout, "usage: swiftSearch serve [options]");
    println(cout, "");
    println(cout, "  serve                  run the HTTP dock");
    println(cout, "");
    println(cout, "Options:");
    println(cout, "      --bind ADDR        address to listen on (default: 127.0.0.1)");
    println(cout, "  -p, --port N           port to listen on (default: 8080)");
    println(cout, "  -j, --threads N        request worker threads (default: CPU count)");
    println(cout, "  -h, --help             this text");
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
    ServerOptions opt;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto next = [&](std::string_view& dest) {
            if (i + 1 >= argc) return false;
            dest = argv[++i];
            return true;
        };
        std::string_view value;
        if (a == "--bind") {
            if (!next(value)) { std::println(stderr, "--bind needs an address"); return EXIT_USAGE; }
            opt.bindAddress = std::string(value);
        } else if (a == "-p" || a == "--port") {
            unsigned long n = 0;
            if (!next(value) || !parseUint(value, n) || n > 65535) {
                std::println(stderr, "--port needs a number from 0 to 65535");
                return EXIT_USAGE;
            }
            opt.port = static_cast<std::uint16_t>(n);
        } else if (a == "-j" || a == "--threads") {
            unsigned long n = 0;
            if (!next(value) || !parseUint(value, n) || n == 0) {
                std::println(stderr, "--threads needs a positive number");
                return EXIT_USAGE;
            }
            opt.requestThreads = static_cast<std::size_t>(n);
        } else {
            std::println(stderr, "unknown option: {}", a);
            return EXIT_USAGE;
        }
    }

    // Must happen before start(), which refuses to run otherwise: a client
    // vanishing mid-response would kill the process on SIGPIPE.
    Server::ignoreSigpipe();

    const std::string bindShown = opt.bindAddress;
    MemStore store;
    const StoreBinding binding = store.binding();
    Server server(std::move(opt), [&binding](const Request& r) {
        return route(r, binding);
    });

    const auto port = server.start();
    if (!port) {
        std::println(stderr, "could not start: {}", std::strerror(port.error()));
        if (port.error() == EPERM) {
            std::println(stderr, "(SIGPIPE is still SIG_DFL)");
        }
        return EXIT_INTERNAL;
    }

    g_server.store(&server, std::memory_order_release);
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    // No SA_RESTART, so an in-flight poll() returns EINTR promptly instead of
    // waiting out its timeout before noticing the stop.
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    print::Logo();
    std::println("listening on http://{}:{}", bindShown, *port);
    std::println("  GET/HEAD/PUT/DELETE /v1/blobs/<key>");
    std::println("  GET/PUT             /v1/boxes/<key>   (501: no prefix scan yet)");
    std::println("  GET                 /healthz");
    std::println("");
    std::println("storage is an in-memory map in main.cpp; nothing persists.");
    std::println("ctrl-c to stop.");

    // Blocks here for the life of the process. The signal handler above calls
    // requestStop(), which is what makes the reactor thread return and this
    // wait finish; stop() then drains in-flight requests and joins the pool.
    server.wait();
    server.stop();
    g_server.store(nullptr, std::memory_order_release);
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
