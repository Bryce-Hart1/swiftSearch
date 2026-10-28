#pragma once
/**
 * One connection's state machine. A byte transducer with a clock.
 *
 * THERE ARE NO FILE DESCRIPTORS AND NO SYSCALLS IN HERE, and there must never
 * be any. Bytes come in through onBytes, bytes go out through pending() and
 * onWritten, and the only thing the reactor translates is want() into poll
 * events. That is the same separation record.hpp draws between a format and the
 * file it lives in, and it is what lets every transition be tested by feeding
 * byte slices with no socket anywhere.
 *
 * THE CLOCK IS A PARAMETER, never read from inside. Every entry point takes
 * `now`. That is the whole fake-clock mechanism: no clock interface, no
 * injection, no virtual call. The reactor passes steady_clock::now(); a test
 * passes whatever it likes. Envelope::value takes its seconds offset as a
 * parameter for the same reason.
 *
 * WANT IS A BITMASK, AND IT IS NEVER EMPTY. POLLHUP, POLLERR and POLLNVAL are
 * reported in revents whether or not they were asked for, so a state that wants
 * nothing still gets woken -- and a loop that then finds nothing to do spins at
 * 100% CPU. Every live state therefore keeps Read (which is also how EOF and
 * errors arrive) and adds Write only while there are bytes queued. The 413
 * drain genuinely needs both at once, which a single-value enum could not say.
 */

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "bytes.hpp"
#include "message.hpp"
#include "parse.hpp"

namespace swiftsearch::http {

using Clock = std::chrono::steady_clock;

enum class ConnState : std::uint8_t {
    ReadingHead = 0,
    ReadingBody,
    /// The request is complete and waiting to be handed to a worker.
    Ready,
    /// A worker owns the request. The connection owns nothing of it.
    Dispatched,
    Writing,
    /// An early response is queued AND inbound body bytes must be discarded.
    /// The 413-without-Expect case, and the only state that needs Read and
    /// Write simultaneously.
    DrainingRequest,
    /// Flush what is queued, then shut down the write side and close.
    Closing,
    Closed,
};

std::string_view describe(ConnState s) noexcept;

enum class Want : std::uint8_t {
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Dispatch = 1u << 2,
    Close = 1u << 3,
};

constexpr Want operator|(Want a, Want b) noexcept {
    return static_cast<Want>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr bool has(Want set, Want bit) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(bit)) != 0;
}

struct ConnLimits {
    HeadLimits head{};

    /// Absolute, from the first byte of a head. The head is capped at 8 KB, so
    /// no honest client needs long, and this is the slow-loris bound.
    std::chrono::milliseconds headTimeout{10'000};
    /// IDLE, reset by every byte. A total deadline cannot both deter a slow
    /// loris and allow a legitimate 64 MB upload over a slow link, so the body
    /// gets an idle deadline and a generous absolute backstop instead.
    std::chrono::milliseconds bodyIdleTimeout{20'000};
    std::chrono::milliseconds requestTotalTimeout{600'000};
    /// Idle, reset by every byte written. A client that opens a window and then
    /// stops reading is pinning up to 64 MB of this server's memory.
    std::chrono::milliseconds writeIdleTimeout{20'000};
    /// Idle between requests on a kept-alive connection. Expiry closes
    /// silently: no request is outstanding, so nothing is owed a response.
    std::chrono::milliseconds keepAliveIdleTimeout{15'000};

    /// Forces periodic recycling of the fd and both buffers.
    std::uint32_t maxRequestsPerConnection = 1000;

    /// How many bytes of a rejected body to read and throw away before giving
    /// up, so the client can finish writing and then read its own 413.
    std::uint64_t drainBudget = 1u << 20;
    std::chrono::milliseconds lingerTimeout{1'000};

    /// Pipelined bytes buffered while dispatched, before Read is withdrawn.
    std::size_t pipelineCap = 64u * 1024u;
};

class Connection {
public:
    Connection(std::uint64_t id, const ConnLimits& limits, Clock::time_point now);

    // --- bytes in ----------------------------------------------------------
    void onBytes(std::span<const std::uint8_t> in, Clock::time_point now);
    /// read() returned 0: the peer closed its write side. NOT necessarily
    /// death -- a client may half-close after its request and still be waiting
    /// to read the response.
    void onPeerReadClosed(Clock::time_point now);
    void onSocketError(int err) noexcept;

    // --- bytes out ---------------------------------------------------------
    /// What is waiting to go out, as up to two spans so the reactor can writev
    /// the head and the body without copying either into one buffer.
    std::array<std::span<const std::uint8_t>, 2> pending() const noexcept;
    void onWritten(std::size_t n, Clock::time_point now) noexcept;

    // --- the handoff -------------------------------------------------------
    /// Moves the completed request out. Valid only while has(want(), Dispatch).
    Request takeRequest(Clock::time_point now);
    void onResponse(Response&& r, Clock::time_point now);
    /// The pool refused the work. 503 and close.
    void onDispatchRefused(Clock::time_point now);

    // --- time --------------------------------------------------------------
    Clock::time_point deadline() const noexcept { return _deadline; }
    void onDeadline(Clock::time_point now);

    /// Shutdown has begun: finish the current request, force Connection: close,
    /// and do not start another.
    void onDrainRequested(Clock::time_point now);

    // --- what the reactor needs to know ------------------------------------
    Want want() const noexcept;
    ConnState state() const noexcept { return _state; }
    std::uint64_t id() const noexcept { return _id; }
    std::uint32_t requestsServed() const noexcept { return _requestsServed; }
    bool peerReadClosed() const noexcept { return _peerReadClosed; }

private:
    void queue(Response&& r, Clock::time_point now);
    void queueInterim(std::string_view raw, Clock::time_point now);
    void clearOut() noexcept;
    bool outDrained() const noexcept { return _outCursor >= _outTotal; }
    void startHead(Clock::time_point now);
    void parseLoop(Clock::time_point now);
    void takeBodyFromInput();
    void finishRequest(Clock::time_point now);
    void setDeadline(Clock::time_point now, std::chrono::milliseconds d) noexcept;

    std::uint64_t _id;
    const ConnLimits* _limits;
    ConnState _state = ConnState::ReadingHead;
    int _sockError = 0;

    Bytes _in;
    std::optional<Request> _req;
    std::uint64_t _bodyNeeded = 0;
    bool _peerReadClosed = false;

    std::string _outHead;
    Bytes _outBody;
    std::size_t _outCursor = 0;
    std::size_t _outTotal = 0;
    bool _omitBody = false;
    /// True while the queued bytes are a 100 Continue rather than a response,
    /// so draining them does not end the request.
    bool _outIsInterim = false;

    std::uint64_t _discardBudget = 0;
    Clock::time_point _lingerUntil{};

    bool _keepAlive = true;
    bool _closeAfter = false;
    bool _draining = false;
    std::uint32_t _requestsServed = 0;
    Clock::time_point _deadline{};
};

}  // namespace swiftsearch::http
