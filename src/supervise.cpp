#include "supervise.hpp"

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
    _admin_enabled(isLoopback(options.server.bindAddress)),
    _request_threads(options.server.requestThreads != 0 ? options.server.requestThreads
                                                        : bstd::syst::threadCount().value_or(4)),
    _main(std::make_unique<SwiftSearch>(options.storePath)),
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

    //list and putBox stay unbound, so box routes answer 501 until SwiftSearch has a prefix scan
    _binding.get = [this](std::string_view key){ return GET(key); };
    _binding.put = [this](std::string_view key, std::span<const std::uint8_t> payload, VAR_TYPE type){
        return PUT(key, payload, type);
    };
    _binding.remove = [this](std::string_view key){ return DELETE(key); };
    _binding.prepare = [this](std::string_view key){ return MOVE(key); };

    if(!_admin_enabled){
        note("admin route off: bound to " + options.server.bindAddress + ", which is not loopback",
             Logger::WARNING);
    }
    _server = std::make_unique<http::Server>(std::move(options.server),
        [this](const http::Request& r){ return handle(r); });
}


Supervise::~Supervise() = default; //here, where SwiftSearch is a complete type


std::expected<std::uint16_t, int> Supervise::start(){
    return _server->start();
}

void Supervise::wait(){
    _server->wait();
}

void Supervise::stop(){
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
        .endObj();
    return http::Response::json(http::Status::Ok, std::move(out));
}


http::Response Supervise::adminPut(const std::string_view query){
    constexpr std::string_view KNOWN[] = {"mem", "move_percent", "cpu_threads", "cpu_high", "cpu_low"};
    if(const auto bad = unknownParam(query, KNOWN)){
        return http::Response::error(http::Status::BadRequest, "unknown parameter: " + std::string(*bad));
    }

    const auto mem = numberParam(query, "mem", true);
    const auto move = numberParam(query, "move_percent", false);
    const auto threads = numberParam(query, "cpu_threads", false);
    const auto high = numberParam(query, "cpu_high", false);
    const auto low = numberParam(query, "cpu_low", false);
    for(const auto* p : {&mem, &move, &threads, &high, &low}){
        if(!p->has_value()){
            return http::Response::error(http::Status::BadRequest, p->error());
        }
    }
    if(!*mem && !*move && !*threads && !*high && !*low){
        return http::Response::error(http::Status::BadRequest,
            "nothing to change: give ?mem=, ?move_percent=, ?cpu_threads=, ?cpu_high= or ?cpu_low=");
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


bool Supervise::isLoopback(const std::string_view address) noexcept{
    return address == "localhost" || address == "::1" || address.starts_with("127.");
}
