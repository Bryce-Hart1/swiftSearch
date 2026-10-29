#include "supervise.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "http/json_write.hpp"
#include "http/routes.hpp"
#include "http/url.hpp"
#include "sha256.hpp"
#include "swiftSearch.cpp"

namespace http = swiftsearch::http;
using Logger = bstd::store::Logger;


namespace{

//nullopt when the parameter is absent, an error when it is there but is not a number
std::expected<std::optional<std::uint64_t>, std::string> numberParam(const std::string_view query,
    const std::string_view name, const bool byte_size){
    const auto raw = http::queryParam(query, name);
    if(!raw.has_value()){
        return std::optional<std::uint64_t>{};
    }
    const auto value = byte_size ? Supervise::parseByteSize(*raw) : http::parseDecimal(*raw);
    if(!value.has_value()){
        return std::unexpected("bad ?" + std::string(name));
    }
    return value;
}

//the first parameter name in `query` that is not one of `known`, if any. A misspelt
//?memory=0 that quietly changes nothing is worse than a 400
std::optional<std::string_view> unknownParam(std::string_view query, std::span<const std::string_view> known){
    while(!query.empty()){
        const std::size_t amp = query.find('&');
        const std::string_view piece = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        if(piece.empty()){
            continue;
        }
        const std::string_view name = piece.substr(0, piece.find('='));
        bool found = false;
        for(const auto k : known){
            found = found || k == name;
        }
        if(!found){
            return name;
        }
    }
    return std::nullopt;
}

std::uint64_t u64(const std::size_t n){
    return static_cast<std::uint64_t>(n);
}

}  // namespace


/* SETUP ------------------------------------------------------------------------------ */

Supervise::Supervise(SupervisorOptions options, Logger& log):
    _log(log),
    _admin_enabled(options.admin && isLoopback(options.server.bindAddress)),
    _request_threads(options.server.requestThreads != 0 ? options.server.requestThreads
                                                        : bstd::syst::threadCount().value_or(4)),
    _main(std::make_unique<SwiftSearch>(options.storePath, options.segmentLimitBytes)),
    _move_percent(0){
    for(const auto& warning : _main->Warnings()){
        note(warning, Logger::WARNING);
    }

    //the same checks a runtime change gets, so a bad flag fails here rather than later
    if(const auto e = CAP_MOVE(options.movePercent)){
        throw std::invalid_argument(*e);
    }
    if(const auto e = CAP_CPU(options.cpuThreads, options.cpuHigh, options.cpuLow)){
        throw std::invalid_argument(*e);
    }
    if(const auto e = CAP_MEM(options.memCapBytes)){
        throw std::invalid_argument(*e);
    }
    if(const auto e = CAP_COMPACT(options.compactIdle, options.compactDeadPercent)){
        throw std::invalid_argument(*e);
    }

    //list and putBox stay unbound, so box routes answer 501 until SwiftSearch has a prefix scan
    _binding.get = [this](std::string_view key){ return GET(key); };
    _binding.put = [this](std::string_view key, std::span<const std::uint8_t> payload, VAR_TYPE type){
        return PUT(key, payload, type);
    };
    _binding.remove = [this](std::string_view key){ return DELETE(key); };
    _binding.prepare = [this](std::string_view key){ return MOVE(key); };

    if(options.admin && !_admin_enabled){
        note("admin route off: bound to " + options.server.bindAddress + ", which is not loopback",
             Logger::WARNING);
    }
    _server = std::make_unique<http::Server>(std::move(options.server),
        [this](const http::Request& r){ return handle(r); });
}


//here, where SwiftSearch is a complete type. The compactor uses _main, so it goes before any member does
Supervise::~Supervise(){
    stopCompactor();
}


std::expected<std::uint16_t, int> Supervise::start(){
    const auto port = _server->start();
    if(port.has_value() && !_compactor.joinable()){
        _compactor = std::thread([this]{ compactorLoop(); });
    }
    return port;
}

void Supervise::wait(){
    _server->wait();
}

void Supervise::stop(){
    stopCompactor();
    _server->stop();
}

void Supervise::requestStop() noexcept{
    _server->requestStop();
}

const std::vector<std::string>& Supervise::Warnings() const{
    return _main->Warnings();
}


/* REQUESTS --------------------------------------------------------------------------- */

http::Response Supervise::handle(const http::Request& req){
    try{
        const http::Target t = http::splitTarget(req.target);
        //when admin is off the path falls through to the dock, which answers "no such route"
        if(_admin_enabled && t.path == ADMIN_LIMITS_PATH){
            switch(req.method){
                case http::Method::Get:
                    return adminGet();
                case http::Method::Put:
                    return adminPut(t.query);
                default:{
                    http::Response r = http::Response::error(http::Status::MethodNotAllowed,
                                                             "method not allowed here");
                    r.set("Allow", "GET, PUT");
                    return r;
                }
            }
        }
        return http::route(req, _binding);
    }catch(const std::exception& e){
        note(std::string("request failed: ") + e.what(), Logger::HIGH);
        return http::Response::error(http::Status::InternalServerError, "internal error");
    }
}


/*
 * The existence checks in PUT and DELETE are a separate step from the write, so another
 * writer can slip in between. The worst that does is a 201 that should have been a 200,
 * or a 404 for a key that was created at that same moment. The data is never wrong.
 */

http::StoreResult<http::Fetched> Supervise::GET(const std::string_view key){
    try{
        auto value = _main->GetTyped(key);
        if(!value.has_value()){
            if(!_main->residency(key).has_value()){
                return std::unexpected(http::StoreStatus::NotFound);
            }
            //the key is in the table but its record would not read back: bad CRC or a missing segment
            note("GET " + std::string(key) + ": stored record could not be read", Logger::HIGH);
            return std::unexpected(http::StoreStatus::Corrupt);
        }
        http::Fetched out;
        out.digest = swiftsearch::sha256(value->bytes);
        out.var_type = value->type;
        out.bytes = std::move(value->bytes);
        return out;
    }catch(const std::exception& e){
        note("GET " + std::string(key) + ": " + e.what(), Logger::HIGH);
        return std::unexpected(http::StoreStatus::Internal);
    }
}


http::StoreResult<http::Stored> Supervise::PUT(const std::string_view key,
    const std::span<const std::uint8_t> payload, const VAR_TYPE type){
    try{
        const bool existed = _main->residency(key).has_value();
        http::Stored out;
        out.digest = swiftsearch::sha256(payload);
        out.size = payload.size();
        out.created = !existed;

        const auto e = _main->Put(std::string(key), swiftsearch::Bytes(payload.begin(), payload.end()), type);
        if(e.has_value()){
            //the dock validates keys and caps bodies before they get here, so this is rare
            if(*e == "invalid key" || *e == "key or value is over the size limit"){
                note("PUT " + std::string(key) + ": " + *e, Logger::LOW);
                return std::unexpected(http::StoreStatus::Unsupported);
            }
            note("PUT " + std::string(key) + ": " + *e, Logger::HIGH);
            return std::unexpected(http::StoreStatus::Internal);
        }
        return out;
    }catch(const std::exception& e){
        note("PUT " + std::string(key) + ": " + e.what(), Logger::HIGH);
        return std::unexpected(http::StoreStatus::Internal);
    }
}


http::StoreResult<bool> Supervise::DELETE(const std::string_view key){
    try{
        if(!_main->residency(key).has_value()){
            return false; //404, and no tombstone for a key that was never there
        }
        if(const auto e = _main->Delete(key)){
            note("DELETE " + std::string(key) + ": " + *e, Logger::HIGH);
            return std::unexpected(http::StoreStatus::Internal);
        }
        return true;
    }catch(const std::exception& e){
        note("DELETE " + std::string(key) + ": " + e.what(), Logger::HIGH);
        return std::unexpected(http::StoreStatus::Internal);
    }
}


http::StoreResult<void> Supervise::MOVE(const std::string_view key){
    try{
        //the size comes from the table, so a value over the cap is refused before any disk read
        const auto size = _main->valueSize(key);
        if(!size.has_value()){
            return std::unexpected(http::StoreStatus::NotFound);
        }
        if(*size > moveMaxBytes()){
            return std::unexpected(http::StoreStatus::TooLarge);
        }
        if(const auto e = _main->prepare(key)){
            if(!_main->residency(key).has_value()){
                return std::unexpected(http::StoreStatus::NotFound); //deleted while it was loading
            }
            note("MOVE " + std::string(key) + ": " + *e, Logger::HIGH);
            return std::unexpected(http::StoreStatus::Internal);
        }
        return {};
    }catch(const std::exception& e){
        note("MOVE " + std::string(key) + ": " + e.what(), Logger::HIGH);
        return std::unexpected(http::StoreStatus::Internal);
    }
}


/* LIMITS ----------------------------------------------------------------------------- */

Supervise::Error Supervise::CAP_MEM(const std::size_t bytes){
    try{
        std::lock_guard<std::mutex> lock(_limits_mtx);
        _main->setCacheBudget(bytes);
        _limits.memCapBytes = bytes;
    }catch(const std::exception& e){
        return std::string("could not change the memory cap: ") + e.what();
    }
    note("memory cap set to " + std::to_string(bytes) + " bytes", Logger::MED);
    return std::nullopt;
}


Supervise::Error Supervise::CAP_CPU(const std::size_t threads, const std::size_t high, const std::size_t low){
    if(threads > MAX_CPU_THREADS){
        return "cpu threads must be 0 to " + std::to_string(MAX_CPU_THREADS);
    }
    if(low >= high || high > 100){
        return std::string("cpu thresholds need low < high <= 100");
    }
    {
        std::lock_guard<std::mutex> lock(_limits_mtx);
        if(const auto e = _main->setCpuLimits(threads, high, low)){
            return e;
        }
        _limits.cpuThreads = threads;
        _limits.cpuHigh = high;
        _limits.cpuLow = low;
    }
    note("cpu cap set to " + (threads == 0 ? std::string("auto") : std::to_string(threads))
         + " threads, busy " + std::to_string(low)
         + "-" + std::to_string(high) + "%", Logger::MED);
    return std::nullopt;
}


Supervise::Error Supervise::CAP_MOVE(const std::size_t percent){
    if(percent == 0 || percent > 100){
        return std::string("move percent must be 1 to 100");
    }
    {
        std::lock_guard<std::mutex> lock(_limits_mtx);
        _move_percent.store(percent);
        _limits.movePercent = percent;
    }
    note("move cap set to " + std::to_string(percent) + "% of the memory cap", Logger::MED);
    return std::nullopt;
}


Supervise::Error Supervise::CAP_COMPACT(const std::chrono::seconds idle, const std::size_t dead_percent){
    if(idle < std::chrono::seconds::zero() || idle > MAX_COMPACT_IDLE){
        return "compact idle must be 0 (off) to " + std::to_string(MAX_COMPACT_IDLE.count()) + " seconds";
    }
    if(dead_percent == 0 || dead_percent > 100){
        return std::string("compact dead percent must be 1 to 100");
    }
    {
        std::lock_guard<std::mutex> lock(_limits_mtx);
        _limits.compactIdleSecs = static_cast<std::uint64_t>(idle.count());
        _limits.compactDeadPercent = dead_percent;
    }
    _compactor_cv.notify_all(); //a shorter idle time takes effect now, not after the wait already running
    if(idle == std::chrono::seconds::zero()){
        note("automatic compaction off", Logger::MED);
    }else{
        note("compaction set to run after " + std::to_string(idle.count()) + "s idle, on segments at least "
             + std::to_string(dead_percent) + "% dead", Logger::MED);
    }
    return std::nullopt;
}


/* COMPACTION ------------------------------------------------------------------------- */

std::optional<swiftsearch::CompactReport> Supervise::compactIfIdle(const std::chrono::steady_clock::time_point now){
    const Limits caps = limits();
    if(caps.compactIdleSecs == 0){
        return std::nullopt; //switched off
    }
    if(now - _main->lastActivity() < std::chrono::seconds(caps.compactIdleSecs)){
        return std::nullopt; //not idle long enough
    }
    if(_main->deadBytes() == 0){
        return std::nullopt; //nothing has been overwritten or deleted, so nothing to win back
    }

    std::lock_guard<std::mutex> lock(_compact_run_mtx);
    const std::uint64_t since = _main->activityCount();
    if(_compacted_at == since){
        return std::nullopt; //the last run finished and nothing has happened since
    }
    const auto report = _main->compact(caps.compactDeadPercent, [this, since]{
        return _stopping.load() || _main->activityCount() != since;
    });
    if(!report.has_value()){
        _compacted_at = since; //a full disk will still be full in 30 seconds; wait for new work to try again
        note("compaction failed: " + report.error(), Logger::HIGH);
        return std::nullopt;
    }
    noteCompaction(*report, "compaction", "for a request; the next idle window picks it up");
    if(!report->stopped){
        _compacted_at = since;
    }
    return *report;
}


std::expected<swiftsearch::CompactReport, std::string> Supervise::compactAll(){
    std::lock_guard<std::mutex> lock(_compact_run_mtx); //waits out an idle run rather than failing beside it
    const std::uint64_t since = _main->activityCount();
    auto report = _main->compact(0, [this]{ return _stopping.load(); });
    if(!report.has_value()){
        note("full compaction failed: " + report.error(), Logger::HIGH);
        return report;
    }
    noteCompaction(*report, "full compaction", "because Supervise is stopping");
    if(!report->stopped){
        _compacted_at = since;
    }
    return report;
}


void Supervise::noteCompaction(const swiftsearch::CompactReport& report, const std::string_view what,
                               const std::string_view stopped_because){
    for(const auto& problem : report.problems){
        note(std::string(what) + ": " + problem, Logger::HIGH);
    }
    const std::string removed = std::to_string(report.segmentsRemoved)
        + (report.segmentsRemoved == 1 ? " segment" : " segments");
    if(report.stopped){
        note(std::string(what) + " stopped after removing " + removed + ", " + std::string(stopped_because),
             Logger::MED);
    }else if(report.segmentsRemoved > 0){
        note(std::string(what) + " removed " + removed + " (" + std::to_string(report.bytesRemoved)
             + " bytes), copying " + std::to_string(report.bytesCopied) + " bytes forward", Logger::MED);
    }
}


void Supervise::compactorLoop(){
    using namespace std::chrono_literals;
    std::unique_lock<std::mutex> lock(_compactor_mtx);
    while(!_compactor_stop){
        //wake often enough to start within a quarter of the idle time of it coming due
        const std::chrono::seconds idle(limits().compactIdleSecs);
        const auto every = idle == 0s ? 30s : std::clamp<std::chrono::seconds>(idle / 4, 1s, 30s);
        _compactor_cv.wait_for(lock, every);
        if(_compactor_stop){
            break;
        }
        lock.unlock();
        try{
            (void)compactIfIdle(std::chrono::steady_clock::now());
        }catch(const std::exception& e){
            note(std::string("compaction failed: ") + e.what(), Logger::HIGH);
        }
        lock.lock();
    }
}


void Supervise::stopCompactor() noexcept{
    _stopping.store(true);
    {
        std::lock_guard<std::mutex> lock(_compactor_mtx);
        _compactor_stop = true;
    }
    _compactor_cv.notify_all();
    try{
        if(_compactor.joinable()){
            _compactor.join();
        }
    }catch(const std::exception& e){
        note(std::string("could not join the compactor: ") + e.what(), Logger::HIGH);
    }
}


Supervise::Limits Supervise::limits() const{
    std::lock_guard<std::mutex> lock(_limits_mtx);
    return _limits;
}


std::uint64_t Supervise::moveMaxBytes() const{
    const std::uint64_t cap = _main->cacheBudget();
    const std::uint64_t pct = _move_percent.load();
    return cap / 100 * pct + cap % 100 * pct / 100; //cap * pct / 100 without the overflow
}


http::Response Supervise::adminGet() const{
    const Limits now = limits();
    std::string out;
    http::Json j(out);
    j.obj()
        .field("mem_cap", u64(now.memCapBytes))
        .field("mem_used", u64(_main->cacheBytes()))
        .field("move_percent", u64(now.movePercent))
        .field("move_max", moveMaxBytes())
        .field("cpu_threads", u64(_main->cpuThreads()))
        .field("cpu_high", u64(now.cpuHigh))
        .field("cpu_low", u64(now.cpuLow))
        .field("request_threads", u64(_request_threads))
        .field("keys", u64(_main->Size()))
        .field("dead_bytes", _main->deadBytes())
        .field("disk_bytes", _main->diskBytes())
        .field("segments", u64(_main->segmentCount()))
        .field("compact_idle", now.compactIdleSecs)
        .field("compact_dead_percent", u64(now.compactDeadPercent))
        .endObj();
    return http::Response::json(http::Status::Ok, std::move(out));
}


http::Response Supervise::adminPut(const std::string_view query){
    constexpr std::string_view KNOWN[] = {"mem", "move_percent", "cpu_threads", "cpu_high", "cpu_low",
                                          "compact_idle", "compact_dead_percent"};
    if(const auto bad = unknownParam(query, KNOWN)){
        return http::Response::error(http::Status::BadRequest, "unknown parameter: " + std::string(*bad));
    }

    const auto mem = numberParam(query, "mem", true);
    const auto move = numberParam(query, "move_percent", false);
    const auto threads = numberParam(query, "cpu_threads", false);
    const auto high = numberParam(query, "cpu_high", false);
    const auto low = numberParam(query, "cpu_low", false);
    const auto dead = numberParam(query, "compact_dead_percent", false);
    for(const auto* p : {&mem, &move, &threads, &high, &low, &dead}){
        if(!p->has_value()){
            return http::Response::error(http::Status::BadRequest, p->error());
        }
    }
    std::optional<std::chrono::seconds> idle;
    if(const auto raw = http::queryParam(query, "compact_idle")){
        idle = parseDuration(*raw);
        if(!idle.has_value()){
            return http::Response::error(http::Status::BadRequest, "bad ?compact_idle");
        }
    }
    if(!*mem && !*move && !*threads && !*high && !*low && !*dead && !idle){
        return http::Response::error(http::Status::BadRequest,
            "nothing to change: give ?mem=, ?move_percent=, ?cpu_threads=, ?cpu_high=, ?cpu_low=, "
            "?compact_idle= or ?compact_dead_percent=");
    }

    //everything is checked before anything is applied, so a bad request changes nothing.
    //Parameters left out keep their current value
    const Limits now = limits();
    const std::uint64_t next_move = move->value_or(now.movePercent);
    const std::uint64_t next_threads = threads->value_or(now.cpuThreads);
    const std::uint64_t next_high = high->value_or(now.cpuHigh);
    const std::uint64_t next_low = low->value_or(now.cpuLow);
    if(next_move == 0 || next_move > 100){
        return http::Response::error(http::Status::BadRequest, "move_percent must be 1 to 100");
    }
    if(next_threads > MAX_CPU_THREADS){
        return http::Response::error(http::Status::BadRequest,
            "cpu_threads must be 0 to " + std::to_string(MAX_CPU_THREADS));
    }
    if(next_low >= next_high || next_high > 100){
        return http::Response::error(http::Status::BadRequest, "cpu thresholds need low < high <= 100");
    }
    const std::chrono::seconds next_idle = idle.value_or(std::chrono::seconds(now.compactIdleSecs));
    const std::uint64_t next_dead = dead->value_or(now.compactDeadPercent);
    if(next_idle > MAX_COMPACT_IDLE){
        return http::Response::error(http::Status::BadRequest,
            "compact_idle must be 0 (off) to " + std::to_string(MAX_COMPACT_IDLE.count()) + " seconds");
    }
    if(next_dead == 0 || next_dead > 100){
        return http::Response::error(http::Status::BadRequest, "compact_dead_percent must be 1 to 100");
    }

    //the pool swap is the one step that can still fail, so it goes first
    if(*threads || *high || *low){
        if(const auto e = CAP_CPU(next_threads, next_high, next_low)){
            note("admin: " + *e, Logger::HIGH);
            return http::Response::error(http::Status::InternalServerError, *e);
        }
    }
    if(*mem){
        if(const auto e = CAP_MEM(static_cast<std::size_t>(**mem))){
            note("admin: " + *e, Logger::HIGH);
            return http::Response::error(http::Status::InternalServerError, *e);
        }
    }
    if(*move){
        (void)CAP_MOVE(next_move); //range checked above
    }
    if(idle || *dead){
        (void)CAP_COMPACT(next_idle, static_cast<std::size_t>(next_dead)); //range checked above
    }
    return adminGet();
}


/* HELPERS ---------------------------------------------------------------------------- */

void Supervise::note(const std::string_view message, const Logger::severity level) noexcept{
    try{
        _log.write(message, level);
    }catch(...){
        //nowhere left to report it
    }
}


std::optional<std::uint64_t> Supervise::parseByteSize(std::string_view s) noexcept{
    std::uint64_t scale = 1;
    if(!s.empty()){
        switch(s.back()){
            case 'k': case 'K': scale = 1ull << 10; break;
            case 'm': case 'M': scale = 1ull << 20; break;
            case 'g': case 'G': scale = 1ull << 30; break;
            default: break;
        }
        if(scale != 1){
            s.remove_suffix(1);
        }
    }
    const auto n = http::parseDecimal(s);
    if(!n.has_value() || *n > std::numeric_limits<std::uint64_t>::max() / scale){
        return std::nullopt;
    }
    return *n * scale;
}


std::optional<std::chrono::seconds> Supervise::parseDuration(std::string_view s) noexcept{
    std::uint64_t scale = 1;
    if(!s.empty()){
        bool suffix = true;
        switch(s.back()){
            case 's': case 'S': scale = 1; break;
            case 'm': case 'M': scale = 60; break;
            case 'h': case 'H': scale = 60 * 60; break;
            default: suffix = false; break;
        }
        if(suffix){
            s.remove_suffix(1);
        }
    }
    constexpr std::uint64_t MAX = static_cast<std::uint64_t>(std::numeric_limits<std::chrono::seconds::rep>::max());
    const auto n = http::parseDecimal(s);
    if(!n.has_value() || *n > MAX / scale){
        return std::nullopt;
    }
    return std::chrono::seconds(static_cast<std::chrono::seconds::rep>(*n * scale));
}


bool Supervise::isLoopback(const std::string_view address) noexcept{
    return address == "localhost" || address == "::1" || address.starts_with("127.");
}
