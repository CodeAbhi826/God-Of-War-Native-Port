#ifndef PS2X_LOG_H
#define PS2X_LOG_H

// ps2x_log.h - level/category based logging for the PS2X runtime.
//
// Design follows the NFSMW (REX) logging subsystem: per-category loggers,
// levels, a rotating file sink, a console sink, an Android logcat sink, an
// always-on in-memory ring that survives a crash, a crash dumper and a hang
// watchdog that prints the guest CPU/thread state when the host stops making
// progress.
//
// Critical lines are never dropped: they ignore the level filter and are
// flushed to every sink, and they are always kept in the crash ring.
//
// Environment configuration (all optional):
//   PS2X_LOG          shorthand: 0|off|1|all or a level name (default: info)
//   PS2X_LOG_LEVEL    default level: trace|debug|info|warn|error|critical|off
//   PS2X_LOG_CATS     per-category overrides, e.g. "ee:trace,cd:debug,gs:warn"
//   PS2X_LOG_FILE     explicit log file path
//   PS2X_LOG_DIR      directory for the log file (default: logs)
//   PS2X_LOG_CONSOLE  1/0, mirror to stderr (default 1)
//   PS2X_LOG_RING     ring entries kept in memory for dumps (default 4096)
//   PS2X_LOG_MAX_MB   rotating file size in MB (default 8)
//   PS2X_LOG_FILES    rotated files kept (default 10)
//   PS2X_LOG_FLUSH    1 = flush after every line (default 0; warn+ always flush)
//   PS2X_HANG_MS      hang watchdog threshold in ms (0 = disabled, default 0)
//   PS2X_CRASH_DUMP   1/0, install crash handlers (default 1)
//
// Usage:
//   PS2X_LOG_INFO(::ps2x::log::Cat::Cd, "open " << path);
//   PS2X_LOG_TRACE(::ps2x::log::Cat::Ee, "pc=0x" << std::hex << pc);
//   PS2X_LOG_CRITICAL(::ps2x::log::Cat::Core, "cannot continue");

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>

namespace ps2x
{
namespace log
{

enum class Level : int
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6
};

enum class Cat : int
{
    Core = 0,
    Ee,
    Iop,
    Sif,
    Cd,
    Gs,
    GsHw,
    Spu,
    Pad,
    Movie,
    Map,
    Recomp,
    Override,
    Host,
    Fs,
    Count
};

constexpr int kCategoryCount = static_cast<int>(Cat::Count);

// Lifecycle. init() is idempotent and reads the environment. It is also
// triggered lazily by the first should_log()/write() call, so it is safe to log
// before main().
void init();
void shutdown();

// Configuration.
void set_default_level(Level level);
void set_category_level(Cat cat, Level level);
void set_all_levels(Level level);
Level default_level();
Level category_level(Cat cat);
bool should_log(Cat cat, Level level);
bool enabled();

// Output.
void write(Level level, Cat cat, std::string_view message);
void writef(Level level, Cat cat, const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// Introspection.
const char *level_name(Level level);
const char *category_name(Cat cat);
bool parse_level(std::string_view text, Level &out);
bool parse_category(std::string_view text, Cat &out);

// Crash / hang support.
std::string format_ring_dump(std::size_t max_lines);
void dump_now(const char *reason);
void heartbeat();
void set_hang_report_hook(std::function<std::string()> hook);
void set_hang_timeout_ms(std::int64_t ms);
std::int64_t hang_timeout_ms();

} // namespace log
} // namespace ps2x

// Generic logging macro. The message is built with operator<< so existing
// "stream style" call sites keep working.
#define PS2X_LOG_AT(level, cat, expr)                                                              \
    do                                                                                             \
    {                                                                                              \
        if (::ps2x::log::should_log((cat), (level)))                                               \
        {                                                                                          \
            std::ostringstream _ps2x_log_stream;                                                   \
            _ps2x_log_stream << expr;                                                              \
            ::ps2x::log::write((level), (cat), _ps2x_log_stream.str());                            \
        }                                                                                          \
    } while (0)

// printf-style variant, avoids the temporary stream in hot paths.
#define PS2X_LOGF(level, cat, ...)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (::ps2x::log::should_log((cat), (level)))                                               \
            ::ps2x::log::writef((level), (cat), __VA_ARGS__);                                      \
    } while (0)

#define PS2X_LOG_TRACE(cat, expr) PS2X_LOG_AT(::ps2x::log::Level::Trace, cat, expr)
#define PS2X_LOG_DEBUG(cat, expr) PS2X_LOG_AT(::ps2x::log::Level::Debug, cat, expr)
#define PS2X_LOG_INFO(cat, expr) PS2X_LOG_AT(::ps2x::log::Level::Info, cat, expr)
#define PS2X_LOG_WARN(cat, expr) PS2X_LOG_AT(::ps2x::log::Level::Warn, cat, expr)
#define PS2X_LOG_ERROR(cat, expr) PS2X_LOG_AT(::ps2x::log::Level::Error, cat, expr)
#define PS2X_LOG_CRITICAL(cat, expr) PS2X_LOG_AT(::ps2x::log::Level::Critical, cat, expr)

#endif // PS2X_LOG_H
