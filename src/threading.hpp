#pragma once
/**
 * Threading helpers for swiftSearch.
 *
 * This header deliberately does NOT include constants.hpp. constants.hpp constructs a
 * bstd::store::Logger at namespace scope, whose constructor opens a file and throws if it
 * cannot, all before main(). Anything that includes this header (the tests included) would
 * inherit that, so the helpers here report problems through return values and exceptions
 * instead of the global logbook.
 *
 * Contents:
 *   - joinThreads()           join a vector of raw std::threads
 *   - ThreadLimitPolicy       the numbers that drive the pool's adaptation
 *   - ThreadPoolOptions       everything the pool constructor accepts
 *   - AdaptiveThreadPool      a task pool whose parallelism follows CPU usage
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "bstd/system/hardware.hpp"
#include "bstd/system/periodic.hpp"


/**
 * Joins every thread in `threadVec`, in order.
 *
 * Every thread is attempted even if an earlier one fails, so one bad thread cannot leave the
 * rest running. A thread that is not joinable (already joined, or never started) counts as a
 * failure, because `std::thread::join` throws for it.
 *
 * @param threadVec threads to join. Left in place, the caller decides whether to clear it.
 * @returns how many threads could not be joined. `0` means every join succeeded.
 */
inline std::size_t joinThreads(std::vector<std::thread>& threadVec){
    std::size_t failures = 0;
    for(std::thread& thread : threadVec){
        try{
            thread.join();
        }catch(const std::exception&){
            ++failures;
        }
    }
    return failures;
}


namespace swiftsearch {

namespace detail {

//which AdaptiveThreadPool (if any) owns the calling thread, so a task can be stopped from
//calling shutdown() or waitIdle() on its own pool, both of which would deadlock
inline thread_local const void* currentPool = nullptr;

}  // namespace detail


/**
 * The numbers that decide how far the pool backs off or ramps up. Kept apart from the pool so
 * the decision itself (AdaptiveThreadPool::nextLimit) is a pure function that is easy to test.
 */
struct ThreadLimitPolicy {
    /** Lowest parallelism the pool will ever drop to. Values below 1 are treated as 1, the pool never stops. */
    std::size_t minThreads = 1;

    /** Highest parallelism the pool will ever reach. */
    std::size_t maxThreads = 1;

    /** At or above this system wide CPU busy percentage the pool backs off (halves its limit). */
    double highBusyPercent = 85.0;

    /** Below this system wide CPU busy percentage the pool ramps up (adds one to its limit). */
    double lowBusyPercent = 60.0;
};


/**
 * Everything AdaptiveThreadPool's constructor accepts. The defaults suit a general purpose
 * pool, so `AdaptiveThreadPool pool;` is valid.
 */
struct ThreadPoolOptions {
    /**
     * Number of worker threads, and the ceiling for parallelism.
     * `0` means "one per hardware thread" (bstd::syst::threadCount()), or 1 if that is unknown.
     * Raise it above the core count for work that mostly waits on disk rather than computing.
     */
    std::size_t maxThreads = 0;

    /** The floor for parallelism. Values below 1 are raised to 1, and values above maxThreads are lowered to it. */
    std::size_t minThreads = 1;

    /** Parallelism allowed at start. `0` means maxThreads. Clamped into [minThreads, maxThreads]. */
    std::size_t initialThreads = 0;

    /**
     * Cap on tasks waiting in the queue. `0` means unbounded.
     * When the queue is full, submit() blocks and trySubmit() refuses, which is how the pool
     * pushes back on a producer that outruns it.
     */
    std::size_t maxQueued = 0;

    /** At or above this system wide CPU busy percentage the pool halves its parallelism. */
    double highBusyPercent = 85.0;

    /** Below this system wide CPU busy percentage the pool adds one to its parallelism. */
    double lowBusyPercent = 60.0;

    /**
     * How often the background governor samples the CPU and adjusts parallelism.
     * `0` disables the governor thread entirely: parallelism then only changes when
     * adjustNow() is called, which makes behavior fully deterministic for tests.
     *
     * Each reading covers the time since the previous one, so the governor's first tick is
     * skipped (its window would be a few microseconds) and the first adjustment happens one
     * full `tick` after construction. Very short ticks give noisy readings, because the
     * kernel counts CPU time in whole jiffies (typically 10 ms).
     */
    std::chrono::milliseconds tick{500};

    /**
     * Where CPU usage comes from: returns system wide busy percent 0..100, or nullopt for "no
     * reading". Empty means the default (AdaptiveThreadPool::defaultCpuSource()), which reads
     * /proc/stat and so only works on Linux.
     * Called from one thread at a time, but not necessarily the same thread each time.
     */
    std::function<std::optional<double>()> cpuSource{};
};


/**
 * A pool of worker threads whose PARALLELISM follows how busy the CPU is.
 *
 * Why it exists: a fixed pool sized to the core count is greedy. When something else on the
 * machine is busy (a build, a video encode, the OS), a greedy pool makes everything slower for
 * everyone. This pool keeps all its threads alive but limits how many may run tasks at once,
 * and moves that limit with measured CPU usage so it leaves headroom.
 *
 * How it adapts (every `tick`, additive increase and multiplicative decrease):
 *   - CPU busy at or above highBusyPercent  ->  limit is halved (backs off fast)
 *   - CPU busy below lowBusyPercent         ->  limit grows by one (ramps up slowly)
 *   - in between, or no reading available   ->  limit is unchanged
 * The gap between the two thresholds is deliberate hysteresis, it stops the limit flapping.
 * The measurement is the whole machine's usage, including this pool's own work, so under
 * saturating load the pool settles near highBusyPercent instead of pinning every core.
 *
 * The limit is enforced exactly: a worker only takes a task when fewer than `limit` tasks are
 * running, and that check happens under the same lock as taking the task. Lowering the limit
 * never interrupts a running task, it simply stops new ones starting until enough finish.
 *
 * Reliability properties:
 *   - A task that throws does not hurt the pool. The exception travels to that task's future.
 *   - Destruction (or shutdown()) finishes every task already queued, then joins all threads.
 *   - Construction failing partway (a thread cannot be created) cleans up before rethrowing.
 *   - With no CPU reading available the pool runs at its initial parallelism, it does not guess.
 *
 * Platform: Linux only for the adaptive part. CPU usage is read from /proc/stat through
 * bstd::syst::CpuMeter. Anywhere else the default source never reports, so the pool still
 * works correctly but never adapts: it simply runs at its initial parallelism.
 *
 * Limits worth knowing:
 *   - CPU usage is a good signal for compute heavy tasks. For tasks that mostly wait on disk
 *     it is a poor one: they leave the CPU idle, so the limit will sit at its maximum.
 *   - There is one FIFO queue. There are no priorities.
 *
 * Thread safety: every public member may be called from any thread, except where noted.
 * Never call shutdown(), waitIdle() or destroy the pool from inside one of its own tasks.
 *
 * Example:
 *   swiftsearch::AdaptiveThreadPool pool;                       // one thread per core, CPU aware
 *   auto count = pool.submit([&]{ return searchFile(path); });  // std::future<...>
 *   pool.waitIdle();                                            // everything submitted is done
 *   std::size_t hits = count.get();
 */
class AdaptiveThreadPool {
public:
    /** A queued unit of work. Tasks from submit() never throw, std::packaged_task absorbs it. */
    using Task = std::function<void()>;

    /** A CPU usage source, see ThreadPoolOptions::cpuSource. */
    using CpuSource = std::function<std::optional<double>()>;

    /**
     * Creates the pool, starts all worker threads, and (unless options.tick is 0) starts the
     * governor that adapts parallelism.
     *
     * @param options pool configuration. Counts are clamped as documented on each field.
     * @throws std::invalid_argument if the thresholds are not 0 <= lowBusyPercent <
     * highBusyPercent <= 100 (or are NaN), or if `tick` is negative.
     * @throws std::system_error if a thread cannot be created. Threads already started are
     * stopped and joined first, so nothing is leaked.
     */
    explicit AdaptiveThreadPool(ThreadPoolOptions options = {})
        : _policy(makePolicy(options)),
          _maxQueued(options.maxQueued),
          _cpuSource(options.cpuSource ? std::move(options.cpuSource) : defaultCpuSource()),
          _limit(std::clamp(options.initialThreads == 0 ? _policy.maxThreads : options.initialThreads,
                            _policy.minThreads, _policy.maxThreads)) {
        try {
            _workers.reserve(_policy.maxThreads);
            for (std::size_t i = 0; i < _policy.maxThreads; ++i) {
                _workers.emplace_back([this] { workerLoop(); });
            }
            if (options.tick.count() > 0) {
                //PeriodicTask fires its first tick immediately, when a reading would span almost no
                //time at all, so that one is skipped and the first real adjustment is a full tick away
                _governor = std::make_unique<bstd::syst::PeriodicTask>(
                    options.tick, [this, skipFirst = true]() mutable {
                        if (skipFirst) {
                            skipFirst = false;
                            return;
                        }
                        adjustNow();
                    });
            }
        } catch (...) {
            //the destructor will not run for a half built object, and a joinable std::thread
            //destroyed without joining calls std::terminate, so stop the workers by hand
            stopWorkers();
            throw;
        }
    }

    AdaptiveThreadPool(const AdaptiveThreadPool&) = delete;
    AdaptiveThreadPool& operator=(const AdaptiveThreadPool&) = delete;
    AdaptiveThreadPool(AdaptiveThreadPool&&) = delete;
    AdaptiveThreadPool& operator=(AdaptiveThreadPool&&) = delete;

    /**
     * Same as shutdown(): runs every queued task to completion, then joins all threads.
     * Blocks for as long as that takes.
     * @attention Must not run on one of the pool's own threads (a task destroying its own pool).
     */
    ~AdaptiveThreadPool() {
        try {
            shutdown();
        } catch (...) {
        }
    }

    /**
     * Queues `work` and returns a future for its result.
     *
     * If the queue is bounded (ThreadPoolOptions::maxQueued) and full, this BLOCKS until there
     * is room. Do not call it from inside a task of a bounded pool, since every worker could end
     * up blocked here with nobody left to drain the queue. Use trySubmit() there.
     *
     * A future that is never read is fine, nothing blocks on it. An exception thrown by `work`
     * is stored in the future and is only seen if someone calls get().
     *
     * @tparam F a callable taking no arguments. Bind arguments with a lambda capture.
     * Move-only callables are accepted.
     * @param work the work to run on a pool thread.
     * @returns a future that becomes ready with `work`'s return value (or its exception).
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F>
    auto submit(F&& work) -> std::future<std::invoke_result_t<std::decay_t<F>&>> {
        using Result = std::invoke_result_t<std::decay_t<F>&>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(work));
        std::future<Result> result = task->get_future();
        if (!enqueue([task] { (*task)(); }, /*block=*/true)) {
            throw std::runtime_error("AdaptiveThreadPool::submit: pool has been shut down");
        }
        return result;
    }

    /**
     * Non-blocking submit. Queues `work` only if there is room right now.
     *
     * @tparam F a callable taking no arguments.
     * @param work the work to run on a pool thread.
     * @returns the task's future, or std::nullopt if the bounded queue is full or the pool has
     * been shut down. On nullopt `work` was NOT queued and will never run, so retry or drop it.
     */
    template <typename F>
    auto trySubmit(F&& work) -> std::optional<std::future<std::invoke_result_t<std::decay_t<F>&>>> {
        using Result = std::invoke_result_t<std::decay_t<F>&>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(work));
        std::future<Result> result = task->get_future();
        if (!enqueue([task] { (*task)(); }, /*block=*/false)) {
            return std::nullopt;
        }
        return result;
    }

    /**
     * Blocks until the queue is empty AND no task is running.
     *
     * "Idle" is a moment, not a state: if other threads keep submitting, this can return while
     * new work is already arriving. It is meant for "everything I submitted is done".
     *
     * @throws std::logic_error if called from one of the pool's own tasks (it would wait on itself).
     */
    void waitIdle() {
        if (detail::currentPool == this) {
            throw std::logic_error("AdaptiveThreadPool::waitIdle: called from one of its own tasks");
        }
        std::unique_lock<std::mutex> lock(_mutex);
        _idle.wait(lock, [this] { return _queue.empty() && _running == 0; });
    }

    /**
     * Stops accepting work, finishes everything already queued, and joins every thread.
     *
     * Draining uses full parallelism (the limit is lifted to maxThreads) because the governor
     * is stopped first and nothing is left to be polite to. Safe to call repeatedly and from
     * several threads at once, later callers wait for the first to finish. After this,
     * submit() throws and trySubmit() returns nullopt.
     *
     * @throws std::logic_error if called from one of the pool's own tasks (a thread cannot join itself).
     */
    void shutdown() {
        if (detail::currentPool == this) {
            throw std::logic_error("AdaptiveThreadPool::shutdown: called from one of its own tasks");
        }
        stopWorkers();
    }

    /**
     * Takes one CPU reading and adjusts the parallelism limit from it, right now, on the
     * calling thread. The background governor calls this every tick. Call it yourself when
     * ThreadPoolOptions::tick is 0, or to react sooner than the next tick.
     *
     * The CPU reading covers the time since the previous call (to the source, from any caller), so
     * calling this far more often than `tick` yields noisy readings.
     *
     * Raising the limit wakes idle workers immediately. Lowering it never interrupts a running task.
     * Does nothing once the pool is shutting down. Never throws, a failing CPU source counts as "no reading".
     */
    void adjustNow() {
        std::optional<double> busy;
        {
            //a stateful sensor must not be read by two threads at once (governor and a manual caller)
            std::lock_guard<std::mutex> sensorLock(_sensorMutex);
            try {
                busy = _cpuSource();
            } catch (...) {
                busy = std::nullopt;
            }
        }
        bool raised = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopping) {
                return;
            }
            _lastBusy = busy;
            const std::size_t next = nextLimit(_limit, _policy, busy);
            raised = next > _limit;
            _limit = next;
        }
        if (raised) {
            _workReady.notify_all();
        }
    }

    /**
     * The decision at the heart of the pool, as a pure function with no threads or clocks.
     *
     * @param current the present parallelism limit. Clamped into the policy's range first.
     * @param policy  floor, ceiling and the two CPU thresholds.
     * @param busyPercent system wide CPU busy percentage 0..100, or nullopt if unknown (NaN counts as unknown).
     * @returns the new limit, always within [max(1, minThreads), maxThreads]:
     *   - busyPercent >= highBusyPercent -> current / 2 (at least the floor)
     *   - busyPercent <  lowBusyPercent  -> current + 1 (at most the ceiling)
     *   - otherwise, or unknown          -> current
     */
    static std::size_t nextLimit(std::size_t current, const ThreadLimitPolicy& policy,
                                 std::optional<double> busyPercent) noexcept {
        const std::size_t floor = std::max<std::size_t>(1, policy.minThreads);
        const std::size_t ceiling = std::max(floor, policy.maxThreads);
        current = std::clamp(current, floor, ceiling);
        if (!busyPercent.has_value() || std::isnan(*busyPercent)) {
            return current;
        }
        if (*busyPercent >= policy.highBusyPercent) {
            return std::max(floor, current / 2);
        }
        if (*busyPercent < policy.lowBusyPercent) {
            return std::min(ceiling, current + 1);
        }
        return current;
    }

    /**
     * The default CPU usage source: bstd::syst::CpuMeter over /proc/stat. Every call returns an
     * independent meter, so two pools never disturb each other's measurement window.
     *
     * Linux only. On other platforms the returned source always reports nullopt.
     * @returns a callable giving busy percent 0..100 since its previous call, or nullopt when
     * nothing can be measured (no /proc/stat, or the counters have not advanced yet).
     */
    static CpuSource defaultCpuSource() {
        auto meter = std::make_shared<bstd::syst::CpuMeter>();
        return [meter]() -> std::optional<double> { return meter->usagePercent(); };
    }

    /** @returns how many tasks may run at once right now. Moves with CPU usage, always in [minThreads(), maxThreads()]. */
    std::size_t activeLimit() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _limit;
    }

    /** @returns the parallelism ceiling, which is also the number of worker threads. Fixed for the pool's life. */
    std::size_t maxThreads() const noexcept { return _policy.maxThreads; }

    /** @returns the parallelism floor. Fixed for the pool's life, never below 1. */
    std::size_t minThreads() const noexcept { return _policy.minThreads; }

    /** @returns how many tasks are waiting, not counting ones already running. */
    std::size_t queued() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

    /** @returns how many tasks are running at this moment. */
    std::size_t running() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _running;
    }

    /** @returns the CPU reading the governor last acted on, or nullopt if it has had none yet. */
    std::optional<double> lastCpuPercent() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lastBusy;
    }

private:
    /** Validates `o` and turns it into the policy the pool runs on. */
    static ThreadLimitPolicy makePolicy(const ThreadPoolOptions& o) {
        const double low = o.lowBusyPercent;
        const double high = o.highBusyPercent;
        //written as !(a && b) so NaN, which fails every comparison, is rejected too
        if (!(low >= 0.0 && low < high && high <= 100.0)) {
            throw std::invalid_argument(
                "ThreadPoolOptions: need 0 <= lowBusyPercent < highBusyPercent <= 100");
        }
        if (o.tick.count() < 0) {
            throw std::invalid_argument("ThreadPoolOptions: tick must not be negative");
        }
        ThreadLimitPolicy policy;
        policy.maxThreads = o.maxThreads != 0 ? o.maxThreads : bstd::syst::threadCount().value_or(1);
        policy.maxThreads = std::max<std::size_t>(1, policy.maxThreads);
        policy.minThreads = std::clamp<std::size_t>(o.minThreads, 1, policy.maxThreads);
        policy.highBusyPercent = high;
        policy.lowBusyPercent = low;
        return policy;
    }

    /**
     * Adds a task to the queue.
     * @param block if the queue is bounded and full: wait for room (true) or give up (false).
     * @returns false if the pool is shutting down, or (non-blocking only) the queue is full.
     */
    bool enqueue(Task task, bool block) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_stopping) {
                return false;
            }
            if (_maxQueued != 0 && _queue.size() >= _maxQueued) {
                if (!block) {
                    return false;
                }
                _spaceReady.wait(lock, [this] { return _stopping || _queue.size() < _maxQueued; });
                if (_stopping) {
                    return false;
                }
            }
            _queue.push_back(std::move(task));
        }
        //any one worker will do: they all wait on the same condition, so none is "the wrong one"
        _workReady.notify_one();
        return true;
    }

    /** Body of every worker thread: take a task whenever fewer than `_limit` are running, run it, repeat. */
    void workerLoop() {
        detail::currentPool = this;
        std::unique_lock<std::mutex> lock(_mutex);
        for (;;) {
            _workReady.wait(lock, [this] {
                return (!_queue.empty() && _running < _limit) || (_stopping && _queue.empty());
            });
            if (_queue.empty()) {
                break;  //the wait only ends with an empty queue when the pool is stopping
            }
            Task task = std::move(_queue.front());
            _queue.pop_front();
            ++_running;  //same lock as the pop, so `_running` can never lag the queue
            _spaceReady.notify_one();
            lock.unlock();
            try {
                task();
            } catch (...) {
                //submit() tasks cannot throw, this only guards the invariant that a worker never dies
            }
            task = nullptr;  //destroy captured state outside the lock, its destructors are user code
            lock.lock();
            --_running;
            if (_queue.empty() && _running == 0) {
                _idle.notify_all();
            }
        }
        detail::currentPool = nullptr;
    }

    /**
     * Stops the governor, drains the queue, joins the workers. Shared by shutdown(), the
     * destructor, and constructor failure. Idempotent and serialized by `_shutdownMutex`.
     */
    void stopWorkers() {
        std::lock_guard<std::mutex> serial(_shutdownMutex);
        //The governor goes first and with `_mutex` NOT held: a tick in flight takes `_mutex`,
        //and resetting joins that tick, so holding it here would deadlock.
        _governor.reset();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopping = true;
            _limit = _policy.maxThreads;  //drain at full speed, nothing is left to be polite to
        }
        _workReady.notify_all();
        _spaceReady.notify_all();
        for (std::thread& worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        _workers.clear();
    }

    //fixed after construction
    const ThreadLimitPolicy _policy;
    const std::size_t _maxQueued;
    CpuSource _cpuSource;  //guarded by _sensorMutex once the pool is running

    std::mutex _sensorMutex;

    //everything below this line up to `_workers` is guarded by `_mutex`
    mutable std::mutex _mutex;
    std::condition_variable _workReady;  //a worker may have something to do, or the pool is stopping
    std::condition_variable _spaceReady;  //a bounded queue gained room (or the pool is stopping)
    std::condition_variable _idle;        //queue empty and nothing running
    std::deque<Task> _queue;
    std::size_t _running = 0;
    std::size_t _limit;
    bool _stopping = false;
    std::optional<double> _lastBusy;

    std::mutex _shutdownMutex;
    std::vector<std::thread> _workers;

    //declared last so it is destroyed first: its thread calls adjustNow(), which touches the members above
    std::unique_ptr<bstd::syst::PeriodicTask> _governor;
};

}  // namespace swiftsearch
