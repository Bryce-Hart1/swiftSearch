#include "server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace swiftsearch::http {
namespace {

/// EAGAIN and EWOULDBLOCK are equal on Linux and macOS but POSIX does not
/// require it, so the test lives in one place.
constexpr bool wouldBlock(int e) noexcept { return e == EAGAIN || e == EWOULDBLOCK; }

/// Out of descriptors, in any of the ways it is reported.
constexpr bool outOfFds(int e) noexcept {
    return e == EMFILE || e == ENFILE || e == ENOBUFS || e == ENOMEM;
}

/// A connection the kernel gave up on between SYN and accept(). Skipping it is
/// correct; treating it as fatal would let any client stop the server.
constexpr bool transientAcceptError(int e) noexcept {
    return e == EINTR || e == ECONNABORTED || e == EPROTO || e == ENETDOWN ||
           e == EHOSTUNREACH || e == ENOPROTOOPT || e == ETIMEDOUT;
}

/// Owns a descriptor and closes it exactly once.
///
/// close() is never retried on EINTR. On Linux the descriptor is already gone
/// when it returns, so retrying can close an unrelated fd that another thread
/// opened into the same slot -- a genuine double-close footgun.
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) noexcept : _fd(fd) {}
    Fd(Fd&& o) noexcept : _fd(o._fd) { o._fd = -1; }
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) {
            reset();
            _fd = o._fd;
            o._fd = -1;
        }
        return *this;
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }

    int get() const noexcept { return _fd; }
    bool valid() const noexcept { return _fd >= 0; }
    int release() noexcept {
        const int f = _fd;
        _fd = -1;
        return f;
    }
    void reset() noexcept {
        if (_fd >= 0) ::close(_fd);
        _fd = -1;
    }

private:
    int _fd = -1;
};

bool setNonBlockingCloexec(int fd) noexcept {
    // pipe2 and accept4 would do this atomically but are Linux-only, and
    // nothing here forks, so the only thing their atomicity buys is unavailable
    // on macOS for no benefit.
    const int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl < 0 || ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return false;
    const int fd_fl = ::fcntl(fd, F_GETFD, 0);
    if (fd_fl < 0 || ::fcntl(fd, F_SETFD, fd_fl | FD_CLOEXEC) < 0) return false;
    return true;
}

/// The doorbell. One byte means "something happened"; it is not a counter, so a
/// full pipe is not an error -- it means a wakeup is already pending, which is
/// exactly the coalescing wanted.
class Waker {
public:
    bool open() noexcept {
        int fds[2];
        if (::pipe(fds) != 0) return false;
        _r = Fd(fds[0]);
        _w = Fd(fds[1]);
        return setNonBlockingCloexec(_r.get()) && setNonBlockingCloexec(_w.get());
    }

    int readFd() const noexcept { return _r.get(); }

    /// Callable from any thread, and async-signal-safe: write() is on the POSIX
    /// safe list and this touches nothing else.
    void wake() const noexcept {
        if (!_w.valid()) return;
        const char b = 0;
        for (;;) {
            const ssize_t n = ::write(_w.get(), &b, 1);
            if (n >= 0) return;
            if (errno == EINTR) continue;
            return;  // EAGAIN: already pending. EPIPE: shutting down.
        }
    }

    void drain() const noexcept {
        char buf[256];
        for (;;) {
            const ssize_t n = ::read(_r.get(), buf, sizeof buf);
            if (n > 0) continue;
            if (n < 0 && errno == EINTR) continue;
            return;
        }
    }

private:
    Fd _r;
    Fd _w;
};

std::size_t deriveMaxConnections(std::size_t requested) noexcept {
    if (requested != 0) return requested;
    rlimit rl{};
    if (::getrlimit(RLIMIT_NOFILE, &rl) != 0) return 128;
    // Raising the soft limit to the hard limit needs no privilege.
    if (rl.rlim_cur < rl.rlim_max) {
        rlimit want = rl;
        want.rlim_cur = rl.rlim_max;
        if (::setrlimit(RLIMIT_NOFILE, &want) == 0) rl = want;
    }
    const std::uint64_t cur = static_cast<std::uint64_t>(rl.rlim_cur);
    // Headroom for segment files, the listener, the waker pair and stdio.
    return cur > 64 ? static_cast<std::size_t>(cur - 32) : 32;
}

}  // namespace

// --- LoadSignal -------------------------------------------------------------

void LoadSignal::requestStarted() noexcept {
    const std::uint64_t cur = _cur.fetch_add(1, std::memory_order_relaxed) + 1;
    // std::atomic has no fetch_max, so the high-water mark needs a CAS loop.
    std::uint64_t seen = _peak.load(std::memory_order_relaxed);
    while (seen < cur &&
           !_peak.compare_exchange_weak(seen, cur, std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
    }
}

void LoadSignal::requestFinished() noexcept {
    _cur.fetch_sub(1, std::memory_order_relaxed);
}

std::optional<double> LoadSignal::samplePeakPercent() noexcept {
    const std::uint64_t peak = _peak.exchange(0, std::memory_order_relaxed);
    return busyPercent(peak, _saturatedAt);
}

AdaptiveThreadPool::CpuSource LoadSignal::asCpuSource() {
    // Captures by pointer the way defaultCpuSource captures its meter: the
    // Server outlives any pool it hands this to.
    return [this]() -> std::optional<double> { return samplePeakPercent(); };
}

// --- the reactor ------------------------------------------------------------

class Server::Impl {
public:
    Impl(ServerOptions o, Handler h)
        : _opt(std::move(o)),
          _handler(std::move(h)),
          _load(2 * std::max<std::size_t>(1, poolThreads(_opt))),
          _maxConns(deriveMaxConnections(_opt.maxConnections)),
          _pool(makePoolOptions(_opt)) {}

    ~Impl() { stop(); }

    std::expected<std::uint16_t, int> start() {
        // A conservative Content-Type and a careful state machine are both
        // worthless if a vanished client can kill the process instead.
        struct sigaction cur{};
        if (::sigaction(SIGPIPE, nullptr, &cur) == 0 && cur.sa_handler == SIG_DFL) {
            return std::unexpected(EPERM);
        }
        if (!_waker.open()) return std::unexpected(errno);

        Fd listener(::socket(AF_INET, SOCK_STREAM, 0));
        if (!listener.valid()) return std::unexpected(errno);

        int on = 1;
        // Before bind, or sockets left in TIME_WAIT make a restart fail with
        // EADDRINUSE for up to two minutes. Deliberately NOT SO_REUSEPORT,
        // which would silently let two instances share the port and turn a
        // config mistake into an unreproducible mystery.
        if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) != 0) {
            return std::unexpected(errno);
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(_opt.port);
        if (::inet_pton(AF_INET, _opt.bindAddress.c_str(), &addr.sin_addr) != 1) {
            return std::unexpected(EINVAL);
        }
        if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            return std::unexpected(errno);
        }
        if (::listen(listener.get(), SOMAXCONN) != 0) return std::unexpected(errno);
        if (!setNonBlockingCloexec(listener.get())) return std::unexpected(errno);

        sockaddr_in bound{};
        socklen_t blen = sizeof bound;
        if (::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&bound), &blen) != 0) {
            return std::unexpected(errno);
        }
        _boundPort = ntohs(bound.sin_port);
        _listener = std::move(listener);

        // The reserve fd exists so that, if accept() ever does hit EMFILE,
        // there is a descriptor to free up in order to accept the pending
        // connection and tell it 503 rather than leaving it hanging.
        _reserve = Fd(::open("/dev/null", O_RDONLY | O_CLOEXEC));

        _running = true;
        _thread = std::thread([this] { loop(); });
        return _boundPort;
    }

    void requestStop() noexcept {
        _stopRequested.store(true, std::memory_order_relaxed);
        _waker.wake();
    }

    /// Joins the reactor thread without asking it to stop. join() leaves the
    /// thread non-joinable, so the join inside stop() is skipped afterwards
    /// rather than attempted twice.
    void wait() {
        if (_thread.joinable()) _thread.join();
    }

    void stop() {
        std::lock_guard<std::mutex> once(_stopMutex);
        if (!_running) return;
        requestStop();
        if (_thread.joinable()) _thread.join();
        // Explicitly, and before anything the workers touch is destroyed. The
        // member order below also makes the pool die first, but relying only on
        // that would make this correct by accident.
        try {
            _pool.shutdown();
        } catch (...) {
        }
        _running = false;
    }

    LoadSignal& load() noexcept { return _load; }
    std::size_t connCount() const noexcept {
        return _connCount.load(std::memory_order_relaxed);
    }
    std::uint64_t handled() const noexcept {
        return _handled.load(std::memory_order_relaxed);
    }

private:
    struct Job {
        int fd = -1;
        std::uint64_t connId = 0;
        Request request;
        Response response;
    };

    static std::size_t poolThreads(const ServerOptions& o) {
        if (o.requestThreads != 0) return o.requestThreads;
        return bstd::syst::threadCount().value_or(4);
    }

    static ThreadPoolOptions makePoolOptions(const ServerOptions& o) {
        const std::size_t n = poolThreads(o);
        ThreadPoolOptions p;
        p.maxThreads = n;
        // Fixed parallelism: min == max means AIMD has nothing it can change,
        // and tick 0 means no governor thread is started at all, so nothing
        // reads /proc and behaviour is identical on macOS and Linux. See the
        // load-signal note at the top of server.hpp.
        p.minThreads = n;
        p.initialThreads = n;
        p.tick = std::chrono::milliseconds{0};
        p.maxQueued = o.maxQueuedRequests != 0 ? o.maxQueuedRequests : 4 * n;
        return p;
    }

    void postCompletion(std::shared_ptr<Job> job) noexcept {
        {
            std::lock_guard<std::mutex> lock(_doneMutex);
            _done.push_back(std::move(job));
        }
        // AFTER the push, always. The reverse order loses a wakeup for a job
        // pushed between the reactor's pipe drain and its queue drain.
        _waker.wake();
    }

    void dispatch(std::shared_ptr<Job> job, Connection& conn) {
        auto submitted = _pool.trySubmit([this, job]() mutable {
            // Exactly one completion is posted on EVERY path. The future from
            // trySubmit is discarded, so an escaping exception would be stored
            // where nobody reads it and the connection would wait in Dispatched
            // forever -- a silent hang, and the easiest way to get this wrong.
            struct Post {
                Impl* self;
                std::shared_ptr<Job> job;
                ~Post() { self->postCompletion(std::move(job)); }
            } post{this, job};

            try {
                job->response = _handler(job->request);
            } catch (const std::bad_alloc&) {
                job->response = Response::fatal(Status::ServiceUnavailable,
                                                "out of memory");
            } catch (...) {
                job->response = Response::fatal(Status::InternalServerError,
                                                "handler failed");
            }
        });
        if (!submitted) {
            // The pool's bounded queue is full. 503 immediately rather than
            // buffering here, which would only relocate the queue and hide the
            // overload from the client.
            conn.onDispatchRefused(Clock::now());
            _load.requestFinished();
        }
    }

    void drainCompletions() {
        std::vector<std::shared_ptr<Job>> batch;
        {
            std::lock_guard<std::mutex> lock(_doneMutex);
            batch.swap(_done);
        }
        const auto now = Clock::now();
        for (auto& job : batch) {
            _load.requestFinished();
            _handled.fetch_add(1, std::memory_order_relaxed);
            const auto it = _conns.find(job->fd);
            // The generation check. Without it a late response could be written
            // into a different client's socket that reused the fd number.
            if (it == _conns.end() || it->second->id() != job->connId) continue;
            it->second->onResponse(std::move(job->response), now);
        }
    }

    void acceptBurst() {
        for (std::size_t i = 0; i < _opt.acceptBurst; ++i) {
            if (_conns.size() >= _maxConns) return;
            const int fd = ::accept(_listener.get(), nullptr, nullptr);
            if (fd < 0) {
                const int e = errno;
                if (wouldBlock(e)) return;
                if (transientAcceptError(e)) continue;
                if (outOfFds(e)) {
                    refuseWithoutFd();
                    // The actual spin fix, and it is needed whether or not the
                    // refusal above worked: the listener stays readable with a
                    // pending connection, so accept() would keep failing in a
                    // tight loop forever.
                    _acceptPausedUntil = Clock::now() + _acceptBackoff;
                    _acceptBackoff = std::min(_acceptBackoff * 2,
                                              std::chrono::milliseconds{1000});
                    return;
                }
                return;
            }
            Fd sock(fd);
            if (!setNonBlockingCloexec(sock.get())) continue;
            int on = 1;
            // Without this a small response can wait for an ACK of the previous
            // segment, adding tens of milliseconds to every request.
            ::setsockopt(sock.get(), IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
#ifdef SO_NOSIGPIPE
            ::setsockopt(sock.get(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
            const int raw = sock.release();
            _conns.emplace(raw, std::make_unique<Connection>(++_nextConnId, _opt.conn,
                                                             Clock::now()));
            _connCount.store(_conns.size(), std::memory_order_relaxed);
            _acceptBackoff = std::chrono::milliseconds{100};
        }
    }

    /// EMFILE: free the reserve descriptor, accept the pending connection just
    /// to tell it the truth, then close it and take the reserve back.
    void refuseWithoutFd() noexcept {
        if (!_reserve.valid()) return;
        _reserve.reset();
        const int fd = ::accept(_listener.get(), nullptr, nullptr);
        if (fd >= 0) {
            static constexpr std::string_view BUSY =
                "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n"
                "Retry-After: 1\r\nConnection: close\r\n\r\n";
            ssize_t ignored = ::write(fd, BUSY.data(), BUSY.size());
            (void)ignored;
            ::close(fd);
        }
        _reserve = Fd(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    }

    void readFrom(int fd, Connection& conn) {
        if (_readBuf.size() != _opt.readChunk) _readBuf.resize(_opt.readChunk);
        std::size_t budget = _opt.perConnReadBudget;
        while (budget > 0) {
            const std::size_t want = std::min(budget, _readBuf.size());
            const ssize_t n = ::read(fd, _readBuf.data(), want);
            if (n > 0) {
                conn.onBytes({_readBuf.data(), static_cast<std::size_t>(n)}, Clock::now());
                budget -= static_cast<std::size_t>(n);
                if (static_cast<std::size_t>(n) < want) return;  // drained
                continue;
            }
            if (n == 0) {
                // The peer closed its WRITE side. Not necessarily death: it may
                // still be waiting to read the response. This is also the
                // portable half-close detector -- POLLRDHUP is Linux-only, and
                // read()==0 is better anyway because it says where in the byte
                // stream the FIN landed.
                conn.onPeerReadClosed(Clock::now());
                return;
            }
            if (errno == EINTR) continue;
            if (wouldBlock(errno)) return;
            conn.onSocketError(errno);
            return;
        }
    }

    void writeTo(int fd, Connection& conn) {
        for (;;) {
            const auto spans = conn.pending();
            iovec iov[2];
            int cnt = 0;
            std::size_t total = 0;
            for (const auto& s : spans) {
                if (s.empty()) continue;
                iov[cnt].iov_base = const_cast<void*>(static_cast<const void*>(s.data()));
                iov[cnt].iov_len = s.size();
                total += s.size();
                ++cnt;
            }
            if (cnt == 0) return;

            const ssize_t n = ::writev(fd, iov, cnt);
            if (n > 0) {
                conn.onWritten(static_cast<std::size_t>(n), Clock::now());
                if (static_cast<std::size_t>(n) < total) return;  // socket full
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && wouldBlock(errno)) return;
            conn.onSocketError(n < 0 ? errno : EIO);
            return;
        }
    }

    /// shutdown(SHUT_WR) then a bounded read-drain, then close. Calling close()
    /// with bytes still in the receive queue makes the kernel send an RST, and
    /// an RST makes the peer discard its RECEIVE buffer -- including the error
    /// response just written. The client then reports a connection reset and
    /// never learns why.
    void closeGracefully(int fd) noexcept {
        ::shutdown(fd, SHUT_WR);
        char buf[4096];
        for (int i = 0; i < 16; ++i) {
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n > 0) continue;
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        ::close(fd);
    }

    void loop() {
        std::vector<pollfd> fds;
        std::vector<int> doomed;

        for (;;) {
            const bool stopping = _stopRequested.load(std::memory_order_relaxed);
            if (stopping && !_draining) {
                _draining = true;
                _drainDeadline = Clock::now() + _opt.shutdownGrace;
                // Stop accepting at once, so a restarting process can rebind
                // while this one finishes what it owes.
                _listener.reset();
                const auto drainNow = Clock::now();
                for (auto& entry : _conns) entry.second->onDrainRequested(drainNow);
            }
            if (_draining && (_conns.empty() || Clock::now() >= _drainDeadline)) break;

            fds.clear();
            fds.push_back(pollfd{_waker.readFd(), POLLIN, 0});

            const auto now = Clock::now();
            const bool acceptPaused = now < _acceptPausedUntil;
            if (_listener.valid() && !acceptPaused && _conns.size() < _maxConns) {
                fds.push_back(pollfd{_listener.get(), POLLIN, 0});
            }

            auto soonest = now + std::chrono::milliseconds{1000};
            for (const auto& [fd, conn] : _conns) {  // both halves are used
                // POLLIN unconditionally: poll reports POLLHUP and POLLERR
                // whether or not they were requested, so an entry asking for
                // nothing would still wake, find nothing to do, and spin.
                short ev = POLLIN;
                if (has(conn->want(), Want::Write)) ev |= POLLOUT;
                fds.push_back(pollfd{fd, ev, 0});
                soonest = std::min(soonest, conn->deadline());
            }
            if (acceptPaused) soonest = std::min(soonest, _acceptPausedUntil);

            auto waitFor = std::chrono::duration_cast<std::chrono::milliseconds>(
                soonest - Clock::now());
            if (waitFor.count() < 0) waitFor = std::chrono::milliseconds{0};
            if (waitFor > std::chrono::milliseconds{1000}) {
                waitFor = std::chrono::milliseconds{1000};
            }

            const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()),
                                     static_cast<int>(waitFor.count()));
            if (ready < 0) {
                if (errno == EINTR) continue;  // recompute the timeout; never reuse it
                break;
            }

            // The pipe FIRST, then the queue. Reversed, a job pushed between the
            // two has its wakeup byte consumed and sits unprocessed until some
            // unrelated event happens.
            if (fds[0].revents != 0) _waker.drain();
            drainCompletions();

            for (std::size_t i = 1; i < fds.size(); ++i) {
                const pollfd& p = fds[i];
                if (_listener.valid() && p.fd == _listener.get()) {
                    if (p.revents != 0) acceptBurst();
                    continue;
                }
                const auto it = _conns.find(p.fd);
                if (it == _conns.end()) continue;
                Connection& conn = *it->second;

                if (p.revents & POLLNVAL) {
                    // The fd is not open: a bookkeeping bug here, not a client
                    // problem. Ignoring it would be an endless 100% CPU loop.
                    conn.onSocketError(EBADF);
                    continue;
                }
                if (p.revents & (POLLIN | POLLHUP)) readFrom(p.fd, conn);
                if (p.revents & POLLERR) {
                    int soErr = 0;
                    socklen_t len = sizeof soErr;
                    ::getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &soErr, &len);
                    conn.onSocketError(soErr != 0 ? soErr : EIO);
                    continue;
                }
                if (p.revents & POLLOUT) writeTo(p.fd, conn);
            }

            // Deadlines, then dispatch, then reap. In that order so a request
            // that completed this iteration is dispatched before anything can
            // time it out.
            const auto afterIo = Clock::now();
            for (auto& entry : _conns) {
                if (afterIo >= entry.second->deadline()) entry.second->onDeadline(afterIo);
            }
            for (auto& entry : _conns) {
                Connection& conn = *entry.second;
                if (!has(conn.want(), Want::Dispatch)) continue;
                auto job = std::make_shared<Job>();
                job->fd = entry.first;
                job->connId = conn.id();
                job->request = conn.takeRequest(afterIo);
                _load.requestStarted();
                dispatch(std::move(job), conn);
            }
            // Anything with queued bytes gets one more write attempt, so a
            // response produced this iteration does not wait for another poll.
            for (auto& entry : _conns) {
                if (has(entry.second->want(), Want::Write)) {
                    writeTo(entry.first, *entry.second);
                }
            }

            doomed.clear();
            for (auto& entry : _conns) {
                if (has(entry.second->want(), Want::Close)) doomed.push_back(entry.first);
            }
            for (const int fd : doomed) {
                closeGracefully(fd);
                _conns.erase(fd);
            }
            if (!doomed.empty()) {
                _connCount.store(_conns.size(), std::memory_order_relaxed);
            }
        }

        for (auto& entry : _conns) closeGracefully(entry.first);
        _conns.clear();
        _connCount.store(0, std::memory_order_relaxed);
    }

    ServerOptions _opt;
    Handler _handler;
    LoadSignal _load;
    std::size_t _maxConns;

    Fd _listener;
    Fd _reserve;
    Waker _waker;

    std::unordered_map<int, std::unique_ptr<Connection>> _conns;
    std::uint64_t _nextConnId = 0;
    std::vector<std::uint8_t> _readBuf;

    std::mutex _doneMutex;
    std::vector<std::shared_ptr<Job>> _done;

    std::atomic<bool> _stopRequested{false};
    bool _draining = false;
    Clock::time_point _drainDeadline{};
    Clock::time_point _acceptPausedUntil{};
    std::chrono::milliseconds _acceptBackoff{100};

    std::atomic<std::size_t> _connCount{0};
    std::atomic<std::uint64_t> _handled{0};
    std::uint16_t _boundPort = 0;

    std::mutex _stopMutex;
    bool _running = false;
    std::thread _thread;

    // Declared LAST so it is destroyed FIRST, which joins its workers before
    // _doneMutex, _done and _waker -- all of which postCompletion touches -- go
    // away. threading.hpp uses the same trick for its _governor, for the same
    // reason. stop() also shuts it down explicitly; this makes the ordering
    // correct rather than merely usual.
    AdaptiveThreadPool _pool;
};

Server::Server(ServerOptions options, Handler handler)
    : _impl(std::make_unique<Impl>(std::move(options), std::move(handler))) {}

Server::~Server() = default;

std::expected<std::uint16_t, int> Server::start() { return _impl->start(); }
void Server::requestStop() noexcept { _impl->requestStop(); }
void Server::wait() { _impl->wait(); }
void Server::stop() { _impl->stop(); }
std::uint64_t Server::inFlight() const noexcept { return _impl->load().inFlight(); }
std::size_t Server::connectionCount() const noexcept { return _impl->connCount(); }
std::uint64_t Server::requestsHandled() const noexcept { return _impl->handled(); }
LoadSignal& Server::loadSignal() noexcept { return _impl->load(); }

void Server::ignoreSigpipe() noexcept {
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    // Unqualified: sigemptyset is a macro on macOS, so `::sigemptyset(...)`
    // expands to something that is not valid syntax.
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGPIPE, &sa, nullptr);
}

}  // namespace swiftsearch::http
