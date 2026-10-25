#include "conn.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace swiftsearch::http {

namespace {
constexpr std::string_view CONTINUE_100 = "HTTP/1.1 100 Continue\r\n\r\n";
}

std::string_view describe(ConnState s) noexcept {
    switch (s) {
        case ConnState::ReadingHead: return "reading-head";
        case ConnState::ReadingBody: return "reading-body";
        case ConnState::Ready: return "ready";
        case ConnState::Dispatched: return "dispatched";
        case ConnState::Writing: return "writing";
        case ConnState::DrainingRequest: return "draining-request";
        case ConnState::Closing: return "closing";
        case ConnState::Closed: return "closed";
    }
    return "unknown";
}

Connection::Connection(std::uint64_t id, const ConnLimits& limits, Clock::time_point now)
    : _id(id), _limits(&limits) {
    startHead(now);
}

void Connection::setDeadline(Clock::time_point now, std::chrono::milliseconds d) noexcept {
    _deadline = now + d;
}

void Connection::startHead(Clock::time_point now) {
    _state = ConnState::ReadingHead;
    _req.reset();
    _bodyNeeded = 0;
    clearOut();
    // An idle kept-alive connection owes nothing, so it gets the longer,
    // quieter deadline; once a byte of a head arrives the tight one applies.
    setDeadline(now, _in.empty() ? _limits->keepAliveIdleTimeout : _limits->headTimeout);
}

void Connection::clearOut() noexcept {
    _outHead.clear();
    _outBody.clear();
    _outCursor = 0;
    _outTotal = 0;
    _omitBody = false;
    _outIsInterim = false;
}

void Connection::queue(Response&& r, Clock::time_point now) {
    _outHead.clear();
    serializeHead(r, !_closeAfter, _outHead);
    _outBody = std::move(r.body);
    _omitBody = r.omitBody;
    _outCursor = 0;
    _outTotal = _outHead.size() + (_omitBody ? 0 : _outBody.size());
    _outIsInterim = false;
    setDeadline(now, _limits->writeIdleTimeout);
}

void Connection::queueInterim(std::string_view raw, Clock::time_point now) {
    _outHead.assign(raw);
    _outBody.clear();
    _outCursor = 0;
    _outTotal = _outHead.size();
    _omitBody = false;
    _outIsInterim = true;
    setDeadline(now, _limits->bodyIdleTimeout);
}

std::array<std::span<const std::uint8_t>, 2> Connection::pending() const noexcept {
    std::span<const std::uint8_t> a;
    std::span<const std::uint8_t> b;
    const std::size_t h = _outHead.size();
    if (_outCursor < h) {
        a = {reinterpret_cast<const std::uint8_t*>(_outHead.data()) + _outCursor,
             h - _outCursor};
        if (!_omitBody && !_outBody.empty()) b = {_outBody.data(), _outBody.size()};
    } else if (!_omitBody) {
        const std::size_t off = _outCursor - h;
        if (off < _outBody.size()) b = {_outBody.data() + off, _outBody.size() - off};
    }
    return {a, b};
}

void Connection::onWritten(std::size_t n, Clock::time_point now) noexcept {
    // The cursor advances; the buffers are NEVER erased from the front. Doing
    // that would move the remainder on every partial write, which for a 64 MB
    // response leaving in 64 KB pieces is about 32 GB of memmove to send 64 MB.
    _outCursor += n;
    if (!outDrained()) {
        setDeadline(now, _limits->writeIdleTimeout);
        return;
    }

    if (_outIsInterim) {
        // That was a 100 Continue. The request is still arriving.
        clearOut();
        setDeadline(now, _limits->bodyIdleTimeout);
        return;
    }

    if (_state == ConnState::DrainingRequest) {
        // Keep discarding until the budget or the linger deadline runs out;
        // closing with bytes still queued would send an RST and destroy the
        // response we just wrote.
        return;
    }
    if (_state != ConnState::Writing) return;

    ++_requestsServed;
    if (_closeAfter) {
        _state = ConnState::Closing;
        return;
    }
    clearOut();
    _keepAlive = true;
    startHead(now);
    // A pipelining client's next request is ALREADY in the buffer, and the
    // kernel will never report new readability for bytes it has handed over.
    // Parsing now rather than waiting for POLLIN is the difference between
    // keep-alive working and hanging forever.
    if (!_in.empty()) parseLoop(now);
}

void Connection::takeBodyFromInput() {
    if (!_req) return;
    const std::uint64_t have = _req->body.size();
    if (have >= _bodyNeeded) return;
    const std::size_t want = static_cast<std::size_t>(_bodyNeeded - have);
    const std::size_t take = std::min(want, _in.size());
    _req->body.insert(_req->body.end(), _in.begin(), _in.begin() + take);
    // One erase per request, not per read: everything after this point is
    // appended straight to the body. See onBytes.
    _in.erase(_in.begin(), _in.begin() + take);
}

void Connection::finishRequest(Clock::time_point now) {
    _state = ConnState::Ready;
    setDeadline(now, _limits->requestTotalTimeout);
}

void Connection::parseLoop(Clock::time_point now) {
    if (_state != ConnState::ReadingHead) return;

    // Parsed with the body cap lifted, then enforced below. Only this layer
    // knows whether Expect: 100-continue was sent, and that decides whether an
    // oversized body is ever transmitted at all -- so the parser cannot make
    // the call, and parsing twice to ask it would be worse.
    HeadLimits relaxed = _limits->head;
    relaxed.maxBodyBytes = std::numeric_limits<std::uint64_t>::max();

    auto head = parseRequestHead(_in, relaxed);
    if (!head) {
        const ParseStatus st = head.error().status;
        if (isIncomplete(st)) {
            setDeadline(now, _limits->headTimeout);
            return;
        }
        _closeAfter = true;
        Response r = Response::fatal(statusFor(st), describe(st));
        if (st == ParseStatus::BadMethod) {
            // RFC 9110 requires Allow on a 405. The route layer normally fills
            // it with what that particular path accepts, but an unimplemented
            // method is rejected by the parser, which has not chosen a route
            // yet -- so this is the methods the SERVER implements at all, which
            // is the honest answer from here and still actionable.
            r.set("Allow", "GET, HEAD, PUT, DELETE");
        }
        queue(std::move(r), now);
        _state = ConnState::Closing;
        return;
    }

    if (head->contentLength > _limits->head.maxBodyBytes) {
        _closeAfter = true;
        queue(Response::fatal(Status::ContentTooLarge,
                              describe(ParseStatus::BodyTooLarge)), now);
        _in.erase(_in.begin(),
                  _in.begin() + static_cast<std::ptrdiff_t>(head->headLength));
        if (head->expectContinue) {
            // The clean path: withhold the 100 Continue and the body is never
            // put on the wire. curl sends Expect automatically above 1 KB.
            _state = ConnState::Closing;
        } else {
            // The client is already sending. Discard against a budget while the
            // 413 goes out, so it can finish writing and then read the reply.
            // Closing on it now would send an RST, and an RST makes the peer
            // discard its receive buffer -- including the 413.
            _state = ConnState::DrainingRequest;
            _discardBudget = _limits->drainBudget;
            _lingerUntil = now + _limits->lingerTimeout;
        }
        return;
    }

    _keepAlive = head->keepAlive;
    _req = Request::own(*head, {});
    _bodyNeeded = head->contentLength;
    _in.erase(_in.begin(), _in.begin() + static_cast<std::ptrdiff_t>(head->headLength));

    if (_bodyNeeded == 0) {
        finishRequest(now);
        return;
    }

    _state = ConnState::ReadingBody;
    _req->body.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(_bodyNeeded, 1u << 20)));
    if (head->expectContinue) {
        // The complement of withholding it above: if the body IS wanted, the
        // continue must actually be sent, or a conforming client waits out its
        // own timeout before sending anything.
        queueInterim(CONTINUE_100, now);
    }
    takeBodyFromInput();
    if (_req->body.size() >= _bodyNeeded) finishRequest(now);
    else setDeadline(now, _limits->bodyIdleTimeout);
}

void Connection::onBytes(std::span<const std::uint8_t> in, Clock::time_point now) {
    if (in.empty()) return;

    switch (_state) {
        case ConnState::ReadingHead:
            _in.insert(_in.end(), in.begin(), in.end());
            parseLoop(now);
            return;

        case ConnState::ReadingBody: {
            // Appended straight to the body, never staged in _in, so a large
            // upload costs one copy per read rather than a growing shuffle.
            const std::uint64_t have = _req ? _req->body.size() : 0;
            const std::size_t want = static_cast<std::size_t>(_bodyNeeded - have);
            const std::size_t take = std::min(want, in.size());
            if (_req) _req->body.insert(_req->body.end(), in.begin(), in.begin() + take);
            if (take < in.size()) {
                // Pipelined: these bytes belong to the next request.
                _in.insert(_in.end(), in.begin() + take, in.end());
            }
            if (_req && _req->body.size() >= _bodyNeeded) finishRequest(now);
            else setDeadline(now, _limits->bodyIdleTimeout);
            return;
        }

        case ConnState::Ready:
        case ConnState::Dispatched:
            // A pipelined next request. Buffered, not parsed: one request is in
            // flight at a time, because HTTP/1.1 requires responses in request
            // order and handling two concurrently would need a reorder buffer.
            //
            // Past the cap the bytes are DISCARDED and the connection is marked
            // to close, rather than withdrawing Read. Withdrawing it would
            // leave this connection in the poll set wanting nothing, and poll
            // reports POLLHUP/POLLERR whether they were requested or not -- so
            // it would wake, find nothing to do, and spin at 100% CPU.
            if (_in.size() + in.size() > _limits->pipelineCap) {
                _closeAfter = true;
                return;
            }
            _in.insert(_in.end(), in.begin(), in.end());
            return;

        case ConnState::DrainingRequest: {
            const std::uint64_t drop =
                std::min<std::uint64_t>(_discardBudget, in.size());
            _discardBudget -= drop;
            if (_discardBudget == 0 || now >= _lingerUntil) {
                _state = ConnState::Closing;
            }
            return;
        }

        case ConnState::Writing:
        case ConnState::Closing:
        case ConnState::Closed:
            // Nothing can be owed to these bytes. Discarding them is correct;
            // buffering them would be an unbounded queue for a request that is
            // never going to be read.
            return;
    }
}

void Connection::onPeerReadClosed(Clock::time_point now) {
    _peerReadClosed = true;
    switch (_state) {
        case ConnState::ReadingHead:
            // Nothing buffered is a clean keep-alive shutdown and owes no
            // response; a partial head is a truncated request, which also owes
            // nothing because there is nobody left to tell.
            _state = ConnState::Closed;
            return;
        case ConnState::ReadingBody:
            _closeAfter = true;
            queue(Response::fatal(Status::BadRequest, "truncated body"), now);
            _state = ConnState::Closing;
            return;
        case ConnState::Ready:
        case ConnState::Dispatched:
            // A worker still owns the request. Keep waiting for it: the client
            // may be half-closed and still reading, and even if it is gone the
            // job has to complete before anything can be cleaned up.
            return;
        case ConnState::Writing:
        case ConnState::DrainingRequest:
        case ConnState::Closing:
        case ConnState::Closed:
            return;
    }
}

void Connection::onSocketError(int err) noexcept {
    _sockError = err;
    _state = ConnState::Closed;
}

Request Connection::takeRequest(Clock::time_point now) {
    _state = ConnState::Dispatched;
    setDeadline(now, _limits->requestTotalTimeout);
    if (!_req) return Request{};  // only reachable by calling this out of turn
    Request out = std::move(*_req);
    _req.reset();
    return out;
}

void Connection::onResponse(Response&& r, Clock::time_point now) {
    _closeAfter = r.closeAfter || !_keepAlive || _draining ||
                  (_requestsServed + 1) >= _limits->maxRequestsPerConnection;
    queue(std::move(r), now);
    _state = ConnState::Writing;
}

void Connection::onDispatchRefused(Clock::time_point now) {
    _closeAfter = true;
    Response r = Response::fatal(Status::ServiceUnavailable, "server is busy");
    r.set("Retry-After", "1");
    queue(std::move(r), now);
    _state = ConnState::Writing;
}

void Connection::onDrainRequested(Clock::time_point now) {
    _draining = true;
    switch (_state) {
        case ConnState::ReadingHead:
            // Idle between requests: nothing is owed, so go now.
            if (_in.empty()) _state = ConnState::Closed;
            else {
                _closeAfter = true;
                queue(Response::fatal(Status::ServiceUnavailable, "server is shutting down"),
                      now);
                _state = ConnState::Closing;
            }
            return;
        case ConnState::ReadingBody:
            // Do not read a body that is about to be refused.
            _closeAfter = true;
            queue(Response::fatal(Status::ServiceUnavailable, "server is shutting down"),
                  now);
            _state = ConnState::Closing;
            return;
        case ConnState::Ready:
        case ConnState::Dispatched:
            // Must wait: a worker owns the job and it has to finish.
            _closeAfter = true;
            return;
        case ConnState::Writing:
        case ConnState::DrainingRequest:
        case ConnState::Closing:
        case ConnState::Closed:
            _closeAfter = true;
            return;
    }
}

void Connection::onDeadline(Clock::time_point now) {
    switch (_state) {
        case ConnState::ReadingHead:
            if (_in.empty()) {
                // Keep-alive idle expiry. Silent: no request is outstanding.
                _state = ConnState::Closed;
            } else {
                _closeAfter = true;
                queue(Response::fatal(Status::RequestTimeout, "request timed out"), now);
                _state = ConnState::Closing;
            }
            return;
        case ConnState::ReadingBody:
            _closeAfter = true;
            queue(Response::fatal(Status::RequestTimeout, "request timed out"), now);
            _state = ConnState::Closing;
            return;
        case ConnState::Ready:
        case ConnState::Dispatched:
            // Deliberately no timeout while a worker owns the request. A hung
            // handler is a pool problem, and killing the connection here would
            // not free the worker that is stuck.
            setDeadline(now, _limits->requestTotalTimeout);
            return;
        case ConnState::Writing:
        case ConnState::Closing:
            // The client opened a window and stopped reading; it is pinning up
            // to 64 MB. Nothing to say to it that it would read.
            _state = ConnState::Closed;
            return;
        case ConnState::DrainingRequest:
            _state = ConnState::Closing;
            return;
        case ConnState::Closed:
            return;
    }
}

Want Connection::want() const noexcept {
    if (_state == ConnState::Closed) return Want::Close;

    // Read is always included: it is how EOF and errors arrive, and poll
    // reports POLLHUP/POLLERR regardless of the requested events anyway, so a
    // state that asked for nothing would still be woken and would then spin.
    Want w = Want::Read;
    if (!outDrained()) w = w | Want::Write;
    if (_state == ConnState::Ready) w = w | Want::Dispatch;
    if (_state == ConnState::Closing && outDrained()) w = w | Want::Close;
    return w;
}

}  // namespace swiftsearch::http
