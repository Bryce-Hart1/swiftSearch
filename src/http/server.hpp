#pragma once
/**
 * The reactor: one thread owning every socket, with request handling dispatched
 * to a thread pool.
 *
 * poll() rather than epoll or kqueue because it is POSIX and this is developed
 * on macOS and deployed on Linux. One code path, no #ifdef. For the same
 * reason the wakeup is a self-pipe rather than eventfd, and accept()+fcntl
 * rather than accept4(): every Linux-only convenience here has a portable
 * equivalent that is just as good at this scale.
 *
 * THE HANDOFF IS AN OWNERSHIP TRANSFER, NOT SHARING. The reactor owns every
 * Connection for its whole life and a worker never sees one. What crosses the
 * thread boundary is a Job holding a moved-out Request (body included, so up to
 * 64 MB moves rather than copies) and an fd plus a generation id. The worker
 * writes only Job::response, then pushes the Job under one mutex and rings the
 * self-pipe. No Connection field is ever touched by two threads, in any state,
 * and there is deliberately no per-connection mutex: "one thread touches this"
 * is a stronger guarantee than "accesses are serialized", and it is free.
 *
 * THE GENERATION ID IS NOT PARANOIA. If the reactor abandons a dispatched
 * connection (peer reset, shutdown deadline) the fd is closed, and the kernel
 * may hand the same fd number to the next accept() before the job finishes.
 * Matching a completion by fd alone would then write one client's response into
 * another client's socket, which is a data leak rather than a glitch.
 *
 * THE LOAD SIGNAL BELONGS TO THE BACKGROUND POOL, NOT THIS ONE. Section 8 of
 * the design notes wants an in-flight request count as the load signal, and
 * ThreadPoolOptions::cpuSource accepts any synthetic 0-100 value. But feeding
 * it to the REQUEST pool is a positive-feedback collapse: the governor halves
 * the limit at >= 85, so parallelism would be shed exactly when requests pile
 * up, which grows the queue, which raises the signal. Re-reading section 8, its
 * "workers check a flag between chunks and drop back down" is about scrub and
 * compaction -- background work backing off when foreground arrives. So the
 * request pool runs at a fixed limit with its governor switched off, and
 * LoadSignal is built now and handed to the background pool when there is one.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "conn.hpp"
#include "message.hpp"
#include "threading.hpp"

namespace swiftsearch::http {

/// In-flight requests, mapped onto the 0-100 "busy percent" an
/// AdaptiveThreadPool governor expects.
class LoadSignal {
public:
    /// `saturatedAt` is the in-flight count that reports 100. Two per worker
    /// thread -- every worker busy plus a roughly full queue -- puts the pool's
    /// 60/85 thresholds at a legible number of requests.
    explicit LoadSignal(std::uint64_t saturatedAt) noexcept
        : _saturatedAt(saturatedAt == 0 ? 1 : saturatedAt) {}

    void requestStarted() noexcept;
    void requestFinished() noexcept;
    std::uint64_t inFlight() const noexcept { return _cur.load(std::memory_order_relaxed); }

    /// The high-water mark since the previous call, then resets it. Deliberately
    /// the same "since you last asked" contract as bstd::syst::CpuMeter, so a
    /// 10 ms burst between two 500 ms ticks is still visible instead of being
    /// sampled away. AdaptiveThreadPool serializes cpuSource calls under its
    /// own sensor mutex, which is what makes read-and-reset safe.
    std::optional<double> samplePeakPercent() noexcept;

    AdaptiveThreadPool::CpuSource asCpuSource();

    /// Pure, so the thresholds can be asserted at compile time the way
    /// AdaptiveThreadPool::nextLimit's are.
    static constexpr double busyPercent(std::uint64_t peak,
                                        std::uint64_t saturatedAt) noexcept {
        if (saturatedAt == 0) return 100.0;
        if (peak >= saturatedAt) return 100.0;
        return 100.0 * static_cast<double>(peak) / static_cast<double>(saturatedAt);
    }

private:
    std::atomic<std::uint64_t> _cur{0};
    std::atomic<std::uint64_t> _peak{0};
    std::uint64_t _saturatedAt;
};

using Handler = std::function<Response(const Request&)>;

struct ServerOptions {
    /// Loopback by default. There is no authentication, and a hand-rolled
    /// server holding every photo you own should not be reachable from the LAN
    /// because someone forgot a flag.
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;

    /// 0 derives it from RLIMIT_NOFILE, leaving headroom for segment files.
    std::size_t maxConnections = 0;
    /// accept() calls per poll iteration. Uncapped, a connection flood starves
    /// every established connection of service.
    std::size_t acceptBurst = 32;
    std::size_t readChunk = 64u * 1024u;
    /// Bytes read from one connection per iteration, so one fast uploader
    /// cannot monopolise the reactor thread.
    std::size_t perConnReadBudget = 256u * 1024u;

    ConnLimits conn{};
    std::size_t requestThreads = 0;  // 0 => hardware concurrency
    std::size_t maxQueuedRequests = 0;  // 0 => 4 per thread
    std::chrono::milliseconds shutdownGrace{5'000};
};

class Server {
public:
    Server(ServerOptions options, Handler handler);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Binds, listens and starts the reactor thread. Returns the bound port,
    /// which is what makes `port = 0` usable in tests: the kernel picks a free
    /// one and this reports which.
    ///
    /// Fails with errno, or with EPERM when SIGPIPE is still SIG_DFL -- see
    /// ignoreSigpipe().
    std::expected<std::uint16_t, int> start();

    /// Safe from ANY thread, including a request handler and a signal handler:
    /// one relaxed store and one write() to the self-pipe, both async-signal-
    /// safe. A handler that wants to stop the server MUST use this, because
    /// AdaptiveThreadPool::shutdown throws if called from one of its own tasks.
    void requestStop() noexcept;

    /// Blocks until the reactor thread exits on its own -- which happens when
    /// requestStop() is called, from a signal handler or from anywhere else.
    /// This is what a `serve` command sits in; it does NOT request the stop.
    ///
    /// Call it from the thread that owns the Server, and then call stop().
    /// Never from a handler or from the reactor thread, both of which would be
    /// waiting on themselves.
    void wait();

    /// Blocks until the reactor has drained and the pool has joined. Must not
    /// be called from a handler or from the reactor thread.
    void stop();

    std::uint64_t inFlight() const noexcept;
    /// A snapshot taken by the reactor thread, so it may be one iteration old.
    std::size_t connectionCount() const noexcept;
    std::uint64_t requestsHandled() const noexcept;

    /// The signal to hand a FUTURE background pool as its cpuSource. It is not
    /// wired to the request pool, for the reason at the top of this file.
    LoadSignal& loadSignal() noexcept;

    /// Ignores SIGPIPE process-wide. Writing to a peer that has gone away
    /// otherwise kills the process, and macOS has no MSG_NOSIGNAL, so this is
    /// the portable fix. It is exposed rather than done silently because
    /// changing process-wide signal disposition is not a library's call to
    /// make; start() refuses to run until it has happened.
    static void ignoreSigpipe() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace swiftsearch::http
