#pragma once
/**
 * Supervise: the top of the program. Owns the store (SwiftSearch), the HTTP
 * server in front of it, and the limits both of them run under.
 *
 * The dock (src/http/) already parses requests and writes responses, and its
 * seam is StoreBinding. Supervise fills that binding from SwiftSearch, so it
 * never touches HTTP bytes itself. What it adds on top of the dock:
 *   - MEMORY CAP  bytes of cached values the store may keep in RAM. Every value
 *                 is on disk regardless; the cap only bounds the cache.
 *   - CPU CAP     worker threads in the store's pool, and the busy percentages
 *                 it backs off at. The HTTP request threads are fixed at start.
 *   - MOVE CAP    one POST /v1/prepare/<key> may load at most movePercent of
 *                 the memory cap, so a single request cannot flush the cache.
 *   - COMPACTION  once the store has done no reads or writes for compactIdle,
 *                 and its dead-byte counter says something has died, a
 *                 background thread rewrites the segments that are at least
 *                 compactDeadPercent dead. A request arriving mid-run makes it
 *                 stop after the batch it is on; it resumes the next idle window.
 *   - GET/PUT /v1/admin/limits to read and change those caps while running.
 *
 * ADMIN IS LOOPBACK ONLY. There is no authentication, so the admin route only
 * answers when the server is bound to a loopback address. Bound anywhere else
 * it is a 404, and the caps can only be set with flags at startup.
 *
 * Store errors are written to the Logger handed in and answered with a
 * StoreStatus. Nothing here throws past a request.
 *
 * SwiftSearch lives in swiftSearch.cpp, which has no include guard and is
 * already pulled into main.cpp by call.hpp, so it is only forward declared here.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bstd/store/log.hpp"
#include "compact.hpp"
#include "http/message.hpp"
#include "http/server.hpp"
#include "http/store_binding.hpp"
#include "var_type.hpp"

class SwiftSearch;

/// Everything Supervise starts with. The caps can be changed later through
/// Supervise::CAP_*; the store path and the server settings cannot.
struct SupervisorOptions {
    std::filesystem::path storePath = "swiftsearch_data";
    /// Bytes of cached values the store may keep in RAM. 64 MiB is SwiftSearch's own default.
    std::size_t memCapBytes = 64ull * 1024ull * 1024ull;
    /// One MOVE may load at most this percent of memCapBytes. 1 to 100.
    std::size_t movePercent = 25;
    /// Worker threads in the store's pool. 0 = one per hardware thread.
    std::size_t cpuThreads = 0;
    /// The pool halves its parallelism at cpuHigh percent busy and adds a thread below cpuLow.
    /// Only acts where CPU usage can be read (Linux); the thread count holds everywhere.
    std::size_t cpuHigh = 85;
    std::size_t cpuLow = 60;
    /// Compaction waits for this long with no reads or writes. 0 turns automatic compaction off.
    std::chrono::seconds compactIdle{std::chrono::minutes(10)};
    /// A segment is rewritten once at least this percent of its bytes are dead. 1 to 100.
    std::size_t compactDeadPercent = 50;
    /// Segment files roll over at this size. Fixed once the store is open.
    std::uint64_t segmentLimitBytes = 256ull * 1024ull * 1024ull;
    /// Bind address, port and request threads. Fixed once the server starts.
    swiftsearch::http::ServerOptions server;
};


class Supervise{
    public:
    using Error = std::optional<std::string>;

    /// The caps as they stand, which is what GET /v1/admin/limits reports.
    struct Limits{
        std::size_t memCapBytes = 0;
        std::size_t movePercent = 0;
        std::size_t cpuThreads = 0; //as asked for; 0 = one per hardware thread
        std::size_t cpuHigh = 0;
        std::size_t cpuLow = 0;
        std::uint64_t compactIdleSecs = 0; //0 = automatic compaction off
        std::size_t compactDeadPercent = 0;
    };

    static constexpr std::string_view ADMIN_LIMITS_PATH = "/v1/admin/limits";
    static constexpr std::size_t MAX_CPU_THREADS = 1024; //a typo should not try to start a million threads
    static constexpr std::chrono::seconds MAX_COMPACT_IDLE{30ll * 24 * 60 * 60}; //30 days


    private:
    bstd::store::Logger& _log;
    const bool _admin_enabled;
    const std::size_t _request_threads; //resolved the way the server resolves it, for reporting

    std::unique_ptr<SwiftSearch> _main;
    swiftsearch::http::StoreBinding _binding;

    mutable std::mutex _limits_mtx; //serializes every CAP_* and guards _limits
    Limits _limits;
    std::atomic<std::size_t> _move_percent; //read by every MOVE, so kept outside the lock

    //the compactor: a thread that wakes now and then and calls compactIfIdle. Started by start(),
    //stopped by stop() or the destructor, always before _main goes away
    std::mutex _compactor_mtx; //guards _compactor_stop for the condition variable
    std::condition_variable _compactor_cv;
    bool _compactor_stop = false;
    std::atomic<bool> _stopping{false}; //read by a running compaction between batches
    std::thread _compactor;
    std::mutex _compact_run_mtx; //one compactIfIdle at a time; guards _compacted_at
    std::optional<std::uint64_t> _compacted_at; //activityCount() when the last run finished

    //declared last so it is destroyed first: its threads call handle(), which uses everything above
    std::unique_ptr<swiftsearch::http::Server> _server;


    /** @attention binding adapters, one per thing the dock can ask the store for */
    swiftsearch::http::StoreResult<swiftsearch::http::Fetched> GET(std::string_view key);
    swiftsearch::http::StoreResult<swiftsearch::http::Stored> PUT(std::string_view key,
        std::span<const std::uint8_t> payload, VAR_TYPE type);
    swiftsearch::http::StoreResult<bool> DELETE(std::string_view key);
    swiftsearch::http::StoreResult<void> MOVE(std::string_view key);

    swiftsearch::http::Response adminGet() const;
    swiftsearch::http::Response adminPut(std::string_view query);

    //the Logger throws when it cannot open its file; a failed log line must not fail a request
    void note(std::string_view message, bstd::store::Logger::severity level) noexcept;

    void compactorLoop();
    void stopCompactor() noexcept;


    public:
    /**
     * Opens (creating if needed) the store at options.storePath and applies the caps.
     * Does not start serving; call start() for that.
     * @throws if the store cannot be opened or a cap is out of range.
     */
    Supervise(SupervisorOptions options, bstd::store::Logger& log);
    ~Supervise();

    Supervise(const Supervise&) = delete;
    Supervise& operator=(const Supervise&) = delete;

    /// Binds and starts serving, then starts the compactor.
    /// @return the bound port, or an errno (see Server::start).
    std::expected<std::uint16_t, int> start();
    /// Blocks until requestStop() is called. Then call stop().
    void wait();
    /// Stops the compactor (a run in progress gives way after its batch), drains in-flight
    /// requests and joins the request threads.
    void stop();
    /// Safe from any thread, a request handler, or a signal handler.
    void requestStop() noexcept;

    /// Every request comes through here: the admin route first, the dock for the rest.
    /// Public so tests can drive it without a socket.
    swiftsearch::http::Response handle(const swiftsearch::http::Request& req);

    /// New memory cap. Cached values over it are dropped at once; they stay on disk.
    Error CAP_MEM(std::size_t bytes);
    /// New CPU cap: pool size (0 = one per hardware thread) and busy thresholds, low < high <= 100.
    /// Replaces the store's pool; reads already queued finish first.
    Error CAP_CPU(std::size_t threads, std::size_t high, std::size_t low);
    /// New share of the memory cap one MOVE may load, 1 to 100.
    Error CAP_MOVE(std::size_t percent);
    /// New compaction rule: run after `idle` with no reads or writes (0 = never, up to
    /// MAX_COMPACT_IDLE), on segments at least `deadPercent` dead (1 to 100).
    Error CAP_COMPACT(std::chrono::seconds idle, std::size_t deadPercent);

    /**
     * Compacts the store if it is due, as of `now`. Due means compaction is on, the store
     * has had no reads or writes for the idle time, its dead-byte counter is above 0, and
     * something has happened since the last run that finished. The run stops after the
     * batch it is on if a request arrives or Supervise is stopping.
     * The compactor thread calls this; it is public so tests can hand in their own clock.
     * @return what the run did, or nullopt if it was not due (or failed, which is logged).
     */
    std::optional<swiftsearch::CompactReport> compactIfIdle(std::chrono::steady_clock::time_point now);

    Limits limits() const;
    /// The most one MOVE may load right now, in bytes.
    std::uint64_t moveMaxBytes() const;
    bool adminEnabled() const noexcept{
        return _admin_enabled;
    }
    /// What startup replay had to cut off or skip. Also written to the log.
    const std::vector<std::string>& Warnings() const;

    /// "128M" -> 134217728. Plain bytes, or a K, M or G suffix (powers of 1024, either case).
    /// nullopt for anything else, or a value that does not fit.
    static std::optional<std::uint64_t> parseByteSize(std::string_view s) noexcept;
    /// "10m" -> 600 seconds. Plain seconds, or an s, m or h suffix (either case).
    /// nullopt for anything else, or a value that does not fit.
    static std::optional<std::chrono::seconds> parseDuration(std::string_view s) noexcept;
    /// 127.x.x.x, ::1 and localhost.
    static bool isLoopback(std::string_view address) noexcept;
};
