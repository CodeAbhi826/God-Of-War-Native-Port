#include "ps2x_log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace ps2x
{
namespace log
{
namespace
{

// ---------------------------------------------------------------------------
// Crash ring: a fixed, lock-free, signal-safe buffer of the most recent lines.
// Each slot uses a seqlock so the crash handler can read without locking.
// ---------------------------------------------------------------------------
constexpr std::size_t kCrashLineMax = 512;
constexpr std::size_t kCrashSlots = 2048; // power of two

struct CrashSlot
{
    std::atomic<std::uint64_t> seq{0}; // even = stable, odd = being written
    char text[kCrashLineMax];
};

CrashSlot *g_crashRing = nullptr;
std::atomic<std::uint64_t> g_crashWrite{0};

char g_crashPath[1024] = {0};
std::atomic<bool> g_typeRegistered{false};

// ---------------------------------------------------------------------------
// Logger state. Leaked on purpose: it stays valid for the crash handlers and
// avoids static destruction order problems.
// ---------------------------------------------------------------------------
struct Logger
{
    std::once_flag initOnce;
    std::atomic<bool> ready{false};

    std::mutex mutex;
    Level defaultLevel = Level::Info;
    int catLevels[kCategoryCount]; // -1 = inherit the default level

    bool console = true;
    std::FILE *file = nullptr;
    std::string filePath;
    long long maxBytes = 8LL * 1024 * 1024;
    int maxFiles = 10;
    long long bytesWritten = 0;
    bool flushEveryLine = false;

    std::size_t ringCapacity = 4096;
    std::deque<std::string> ring;

    std::atomic<std::uint64_t> heartbeat{0};
    std::atomic<std::int64_t> hangMs{0};
    std::atomic<bool> hangRunning{false};
    std::atomic<bool> stopWatchdog{false};
    std::thread watchdog;
    std::function<std::string()> hangHook;

    std::uint64_t sequence = 0;
};

Logger &logger()
{
    static Logger *instance = new Logger();
    return *instance;
}

// ---------------------------------------------------------------------------
// Small helpers.
// ---------------------------------------------------------------------------
std::string to_lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

void trim(std::string &text)
{
    const auto notSpace = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    while (!text.empty() && !notSpace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && !notSpace(static_cast<unsigned char>(text.back())))
        text.pop_back();
}

std::size_t bounded_len(const char *text, std::size_t max)
{
    std::size_t n = 0;
    while (n < max && text[n] != '\0')
        ++n;
    return n;
}

void format_timestamp(char *buffer, std::size_t size)
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    std::snprintf(buffer, size, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  static_cast<int>(ms.count()));
}

std::string thread_tag()
{
    const auto id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(id & 0xffffffffu));
    return std::string(buffer);
}

bool env_flag(const char *name, bool fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return fallback;
    const char c = value[0];
    return !(c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F');
}

long env_int(const char *name, long fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return fallback;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value)
        return fallback;
    return parsed;
}

const char *const kLevelNames[] = {"trace", "debug", "info", "warn", "error", "critical", "off"};

struct CategoryName
{
    Cat cat;
    const char *name;
};

const CategoryName kCategoryNames[] = {
    {Cat::Core, "core"},       {Cat::Ee, "ee"},         {Cat::Iop, "iop"},
    {Cat::Sif, "sif"},         {Cat::Cd, "cd"},         {Cat::Gs, "gs"},
    {Cat::GsHw, "gs-hw"},      {Cat::Spu, "spu"},       {Cat::Pad, "pad"},
    {Cat::Movie, "movie"},     {Cat::Map, "map"},       {Cat::Recomp, "recomp"},
    {Cat::Override, "override"}, {Cat::Host, "host"},   {Cat::Fs, "fs"},
};

void store_crash_line(const std::string &line)
{
    if (g_crashRing == nullptr || line.empty())
        return;
    const std::uint64_t index = g_crashWrite.fetch_add(1, std::memory_order_relaxed);
    CrashSlot &slot = g_crashRing[index % kCrashSlots];
    const std::size_t n = std::min(line.size(), kCrashLineMax - 1);
    slot.seq.store(index * 2 + 1, std::memory_order_release);
    std::memcpy(slot.text, line.data(), n);
    slot.text[n] = '\0';
    slot.seq.store(index * 2 + 2, std::memory_order_release);
}

void open_file_locked(Logger &lg)
{
    if (lg.filePath.empty())
        return;
    lg.file = std::fopen(lg.filePath.c_str(), "wb");
    lg.bytesWritten = 0;
}

void rotate_locked(Logger &lg)
{
    if (lg.file != nullptr)
    {
        std::fclose(lg.file);
        lg.file = nullptr;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    for (int i = lg.maxFiles - 1; i >= 1; --i)
    {
        const std::string from = lg.filePath + "." + std::to_string(i);
        const std::string to = lg.filePath + "." + std::to_string(i + 1);
        if (fs::exists(from, ec))
        {
            fs::remove(to, ec);
            fs::rename(from, to, ec);
        }
    }
    if (fs::exists(lg.filePath, ec))
        fs::rename(lg.filePath, lg.filePath + ".1", ec);
    open_file_locked(lg);
}

#if !defined(_WIN32)
int g_signalFd = -1;

void dump_crash_ring()
{
    if (g_signalFd < 0)
        return;
    const char *header = "\n=== PS2X CRASH: last log lines ===\n";
    (void)!write(g_signalFd, header, std::strlen(header));
    if (g_crashRing != nullptr)
    {
        const std::uint64_t written = g_crashWrite.load(std::memory_order_acquire);
        const std::uint64_t count = std::min<std::uint64_t>(written, kCrashSlots);
        for (std::uint64_t i = 0; i < count; ++i)
        {
            const std::uint64_t index = written - count + i;
            CrashSlot &slot = g_crashRing[index % kCrashSlots];
            const std::uint64_t seq = slot.seq.load(std::memory_order_acquire);
            if (seq == 0 || (seq & 1u) != 0u)
                continue;
            const std::size_t n = bounded_len(slot.text, kCrashLineMax);
            if (n > 0)
                (void)!write(g_signalFd, slot.text, n);
        }
    }
    const char *footer = "=== END PS2X CRASH ===\n";
    (void)!write(g_signalFd, footer, std::strlen(footer));
}

void crash_signal_handler(int sig)
{
    if (g_crashPath[0] != '\0')
    {
        const int fd = ::open(g_crashPath, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd >= 0)
        {
            g_signalFd = fd;
            dump_crash_ring();
            ::close(fd);
            g_signalFd = -1;
        }
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void install_crash_handlers()
{
    if (!g_typeRegistered.exchange(true))
    {
        std::signal(SIGSEGV, crash_signal_handler);
        std::signal(SIGABRT, crash_signal_handler);
        std::signal(SIGBUS, crash_signal_handler);
        std::signal(SIGILL, crash_signal_handler);
        std::signal(SIGFPE, crash_signal_handler);
    }
}
#else
void install_crash_handlers()
{
    if (g_typeRegistered.exchange(true))
        return;
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *) -> LONG {
        if (g_crashPath[0] != '\0')
        {
            const int fd = _open(g_crashPath, _O_CREAT | _O_WRONLY | _O_APPEND, _S_IREAD | _S_IWRITE);
            if (fd >= 0)
            {
                const char *header = "\n=== PS2X CRASH ===\n";
                (void)!_write(fd, header, static_cast<unsigned>(std::strlen(header)));
                if (g_crashRing != nullptr)
                {
                    const std::uint64_t written = g_crashWrite.load(std::memory_order_acquire);
                    const std::uint64_t count = std::min<std::uint64_t>(written, kCrashSlots);
                    for (std::uint64_t i = 0; i < count; ++i)
                    {
                        const std::uint64_t index = written - count + i;
                        CrashSlot &slot = g_crashRing[index % kCrashSlots];
                        const std::uint64_t seq = slot.seq.load(std::memory_order_acquire);
                        if (seq == 0 || (seq & 1u) != 0u)
                            continue;
                        const std::size_t n = bounded_len(slot.text, kCrashLineMax);
                        if (n > 0)
                            (void)!_write(fd, slot.text, static_cast<unsigned>(n));
                    }
                }
                const char *footer = "=== END PS2X CRASH ===\n";
                (void)!_write(fd, footer, static_cast<unsigned>(std::strlen(footer)));
                _close(fd);
            }
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
}
#endif

void start_watchdog_if_needed();

void init_locked()
{
    Logger &lg = logger();
    for (int i = 0; i < kCategoryCount; ++i)
        lg.catLevels[i] = -1;

    // Precedence: PS2X_LOG_LEVEL (default threshold) < PS2X_LOG (everything)
    // < PS2X_LOG_CATS (per-category).
    if (const char *level = std::getenv("PS2X_LOG_LEVEL"); level != nullptr && level[0] != '\0')
    {
        Level parsed = Level::Info;
        if (parse_level(level, parsed))
            set_default_level(parsed);
    }

    if (const char *shorthand = std::getenv("PS2X_LOG"); shorthand != nullptr && shorthand[0] != '\0')
    {
        const std::string text = to_lower(shorthand);
        Level parsed = Level::Info;
        if (text == "0" || text == "off")
        {
            set_all_levels(Level::Off);
        }
        else if (text == "all" || text == "1")
        {
            set_all_levels(Level::Trace);
        }
        else if (parse_level(text, parsed))
        {
            set_all_levels(parsed);
        }
    }

    if (const char *cats = std::getenv("PS2X_LOG_CATS"); cats != nullptr && cats[0] != '\0')
    {
        std::string text(cats);
        std::size_t start = 0;
        while (start <= text.size())
        {
            const std::size_t comma = text.find(',', start);
            const std::size_t end = comma == std::string::npos ? text.size() : comma;
            std::string item = text.substr(start, end - start);
            trim(item);
            const std::size_t colon = item.find(':');
            if (colon != std::string::npos)
            {
                std::string catName = item.substr(0, colon);
                std::string levelName = item.substr(colon + 1);
                trim(catName);
                trim(levelName);
                Cat cat{};
                Level level{};
                if (parse_category(catName, cat) && parse_level(levelName, level))
                    set_category_level(cat, level);
            }
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
    }

    lg.console = env_flag("PS2X_LOG_CONSOLE", true);
    lg.flushEveryLine = env_flag("PS2X_LOG_FLUSH", false);
    lg.ringCapacity = static_cast<std::size_t>(std::max<long>(128, env_int("PS2X_LOG_RING", 4096)));
    lg.maxBytes = std::max<long>(1024L * 1024L, env_int("PS2X_LOG_MAX_MB", 8)) * 1024LL * 1024LL;
    lg.maxFiles = static_cast<int>(std::max<long>(1, env_int("PS2X_LOG_FILES", 10)));

    namespace fs = std::filesystem;
    std::error_code ec;
    std::string dir = "logs";
    if (const char *envDir = std::getenv("PS2X_LOG_DIR"); envDir != nullptr && envDir[0] != '\0')
        dir = envDir;
    if (const char *envFile = std::getenv("PS2X_LOG_FILE"); envFile != nullptr && envFile[0] != '\0')
    {
        lg.filePath = envFile;
        dir = fs::path(lg.filePath).parent_path().string();
    }
    else
    {
        lg.filePath = (fs::path(dir) / "ps2x.log").string();
    }
    if (!dir.empty())
    {
        fs::create_directories(dir, ec);
    }
    open_file_locked(lg);

    // Crash dump path lives next to the log file.
    std::string crashPath = lg.filePath.empty() ? std::string("ps2x_crash.log") : (lg.filePath + ".crash.log");
    std::snprintf(g_crashPath, sizeof(g_crashPath), "%s", crashPath.c_str());
    if (g_crashRing == nullptr)
        g_crashRing = new CrashSlot[kCrashSlots];
    if (env_flag("PS2X_CRASH_DUMP", true))
        install_crash_handlers();

    lg.hangMs.store(env_int("PS2X_HANG_MS", 0), std::memory_order_relaxed);
    start_watchdog_if_needed();

    // Set ready before the banner write: the banner goes through write(), which
    // calls ensure_init(), and re-entering call_once on the same once_flag from
    // this thread would deadlock.
    lg.ready.store(true, std::memory_order_release);

    std::string banner = "ps2x log started: level=" + std::string(level_name(lg.defaultLevel)) +
                         " file=" + (lg.filePath.empty() ? std::string("<none>") : lg.filePath) +
                         " console=" + (lg.console ? "1" : "0");
    write(Level::Info, Cat::Core, banner);
}

void ensure_init()
{
    Logger &lg = logger();
    if (lg.ready.load(std::memory_order_acquire))
        return;
    std::call_once(lg.initOnce, []() { init_locked(); });
    lg.ready.store(true, std::memory_order_release);
}

void start_watchdog_if_needed()
{
    Logger &lg = logger();
    if (lg.hangMs.load(std::memory_order_relaxed) <= 0)
        return;
    if (lg.hangRunning.exchange(true))
        return;
    lg.stopWatchdog.store(false, std::memory_order_relaxed);
    lg.watchdog = std::thread([]() {
        Logger &l = logger();
        std::uint64_t last = l.heartbeat.load(std::memory_order_relaxed);
        auto lastChange = std::chrono::steady_clock::now();
        bool reported = false;
        while (!l.stopWatchdog.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const std::uint64_t now = l.heartbeat.load(std::memory_order_relaxed);
            if (now != last)
            {
                last = now;
                lastChange = std::chrono::steady_clock::now();
                reported = false;
                continue;
            }
            if (reported)
                continue;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - lastChange)
                                     .count();
            if (elapsed < l.hangMs.load(std::memory_order_relaxed))
                continue;
            reported = true;

            std::function<std::string()> hook;
            {
                std::lock_guard<std::mutex> lock(l.mutex);
                hook = l.hangHook;
            }
            std::string extra;
            if (hook)
            {
                try
                {
                    extra = hook();
                }
                catch (...)
                {
                    extra = "<hang report hook threw>";
                }
            }
            write(Level::Critical, Cat::Core,
                  "HANG WATCHDOG: no host progress for " + std::to_string(elapsed) + " ms");
            if (!extra.empty())
                write(Level::Critical, Cat::Core, "HANG WATCHDOG state:\n" + extra);
            dump_now("hang");
        }
    });
}

std::string ring_filename(const char *reason)
{
    Logger &lg = logger();
    char stamp[32];
    const std::time_t tt = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
                  tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    std::filesystem::path base(lg.filePath.empty() ? "ps2x.log" : lg.filePath);
    std::filesystem::path dir = base.parent_path();
    std::string name = "dump-" + std::string(reason) + "-" + stamp + ".log";
    return (dir.empty() ? std::filesystem::path(name) : dir / name).string();
}

} // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

const char *level_name(Level level)
{
    const int index = static_cast<int>(level);
    if (index < 0 || index >= static_cast<int>(sizeof(kLevelNames) / sizeof(kLevelNames[0])))
        return "?";
    return kLevelNames[index];
}

const char *category_name(Cat cat)
{
    for (const auto &entry : kCategoryNames)
    {
        if (entry.cat == cat)
            return entry.name;
    }
    return "?";
}

bool parse_level(std::string_view text, Level &out)
{
    const std::string lower = to_lower(text);
    for (int i = 0; i < static_cast<int>(sizeof(kLevelNames) / sizeof(kLevelNames[0])); ++i)
    {
        if (lower == kLevelNames[i])
        {
            out = static_cast<Level>(i);
            return true;
        }
    }
    return false;
}

bool parse_category(std::string_view text, Cat &out)
{
    const std::string lower = to_lower(text);
    for (const auto &entry : kCategoryNames)
    {
        if (lower == entry.name)
        {
            out = entry.cat;
            return true;
        }
    }
    return false;
}

void init()
{
    ensure_init();
}

void shutdown()
{
    Logger &lg = logger();
    lg.stopWatchdog.store(true, std::memory_order_relaxed);
    if (lg.watchdog.joinable())
        lg.watchdog.join();
    std::lock_guard<std::mutex> lock(lg.mutex);
    if (lg.file != nullptr)
    {
        std::fflush(lg.file);
        std::fclose(lg.file);
        lg.file = nullptr;
    }
}

void set_default_level(Level level)
{
    Logger &lg = logger();
    lg.defaultLevel = level;
}

void set_category_level(Cat cat, Level level)
{
    Logger &lg = logger();
    const int index = static_cast<int>(cat);
    if (index < 0 || index >= kCategoryCount)
        return;
    lg.catLevels[index] = static_cast<int>(level);
}

void set_all_levels(Level level)
{
    set_default_level(level);
    for (int i = 0; i < kCategoryCount; ++i)
        set_category_level(static_cast<Cat>(i), level);
}

Level default_level()
{
    ensure_init();
    return logger().defaultLevel;
}

Level category_level(Cat cat)
{
    ensure_init();
    Logger &lg = logger();
    const int index = static_cast<int>(cat);
    if (index < 0 || index >= kCategoryCount)
        return lg.defaultLevel;
    if (lg.catLevels[index] >= 0)
        return static_cast<Level>(lg.catLevels[index]);
    return lg.defaultLevel;
}

bool should_log(Cat cat, Level level)
{
    ensure_init();
    if (level >= Level::Critical)
        return true; // critical lines are never filtered
    if (level >= Level::Off)
        return false;
    return level >= category_level(cat);
}

bool enabled()
{
    return default_level() < Level::Off;
}

void write(Level level, Cat cat, std::string_view message)
{
    ensure_init();
    Logger &lg = logger();
    if (level < Level::Critical)
    {
        if (level >= Level::Off)
            return;
        // Re-check: callers usually gate with should_log(), but keep it safe.
        const int index = static_cast<int>(cat);
        Level threshold = lg.defaultLevel;
        if (index >= 0 && index < kCategoryCount && lg.catLevels[index] >= 0)
            threshold = static_cast<Level>(lg.catLevels[index]);
        if (level < threshold)
            return;
    }

    char timestamp[24];
    format_timestamp(timestamp, sizeof(timestamp));

    std::string line;
    line.reserve(message.size() + 64);
    line += '[';
    line += timestamp;
    line += "] [";
    line += level_name(level);
    line += "] [";
    line += category_name(cat);
    line += "] [t";
    line += thread_tag();
    line += "] ";
    line += message;
    if (line.empty() || line.back() != '\n')
        line += '\n';

    store_crash_line(line);

#if defined(__ANDROID__)
    {
        int priority = ANDROID_LOG_INFO;
        switch (level)
        {
        case Level::Trace:
        case Level::Debug:
            priority = ANDROID_LOG_DEBUG;
            break;
        case Level::Warn:
            priority = ANDROID_LOG_WARN;
            break;
        case Level::Error:
            priority = ANDROID_LOG_ERROR;
            break;
        case Level::Critical:
            priority = ANDROID_LOG_FATAL;
            break;
        default:
            priority = ANDROID_LOG_INFO;
            break;
        }
        __android_log_print(priority, "PS2X", "%s", line.c_str());
    }
#endif

    std::lock_guard<std::mutex> lock(lg.mutex);
    if (lg.console)
        std::fwrite(line.data(), 1, line.size(), stderr);
    if (lg.file != nullptr)
    {
        std::fwrite(line.data(), 1, line.size(), lg.file);
        lg.bytesWritten += static_cast<long long>(line.size());
        if (lg.maxBytes > 0 && lg.bytesWritten >= lg.maxBytes)
            rotate_locked(lg);
    }
    if (lg.flushEveryLine || level >= Level::Warn)
    {
        if (lg.console)
            std::fflush(stderr);
        if (lg.file != nullptr)
            std::fflush(lg.file);
    }

    lg.ring.emplace_back(line);
    while (lg.ring.size() > lg.ringCapacity)
        lg.ring.pop_front();
}

void writef(Level level, Cat cat, const char *format, ...)
{
    if (!should_log(cat, level))
        return;
    char stack[1024];
    std::va_list args;
    va_start(args, format);
    const int needed = std::vsnprintf(stack, sizeof(stack), format, args);
    va_end(args);
    if (needed < 0)
        return;
    if (static_cast<std::size_t>(needed) < sizeof(stack))
    {
        write(level, cat, std::string_view(stack, static_cast<std::size_t>(needed)));
        return;
    }
    std::string heap(static_cast<std::size_t>(needed) + 1, '\0');
    va_start(args, format);
    std::vsnprintf(heap.data(), heap.size(), format, args);
    va_end(args);
    write(level, cat, std::string_view(heap.data(), static_cast<std::size_t>(needed)));
}

std::string format_ring_dump(std::size_t max_lines)
{
    ensure_init();
    Logger &lg = logger();
    std::lock_guard<std::mutex> lock(lg.mutex);
    std::string out;
    const std::size_t total = lg.ring.size();
    const std::size_t count = std::min(total, max_lines);
    out.reserve(count * 96);
    for (std::size_t i = total - count; i < total; ++i)
        out += lg.ring[i];
    return out;
}

void dump_now(const char *reason)
{
    ensure_init();
    Logger &lg = logger();
    const std::string path = ring_filename(reason == nullptr ? "dump" : reason);
    std::ofstream file(path, std::ios::out | std::ios::app);
    if (!file)
        return;
    std::function<std::string()> hook;
    {
        std::lock_guard<std::mutex> lock(lg.mutex);
        hook = lg.hangHook;
    }
    file << "=== ps2x dump (" << (reason == nullptr ? "dump" : reason) << ") ===\n";
    if (hook)
    {
        try
        {
            file << hook();
            if (file.tellp() > 0)
                file << '\n';
        }
        catch (...)
        {
        }
    }
    file << format_ring_dump(4096);
    file << "=== end ps2x dump ===\n";
}

void heartbeat()
{
    logger().heartbeat.fetch_add(1, std::memory_order_relaxed);
}

void set_hang_report_hook(std::function<std::string()> hook)
{
    ensure_init();
    Logger &lg = logger();
    std::lock_guard<std::mutex> lock(lg.mutex);
    lg.hangHook = std::move(hook);
}

void set_hang_timeout_ms(std::int64_t ms)
{
    ensure_init();
    Logger &lg = logger();
    lg.hangMs.store(ms, std::memory_order_relaxed);
    start_watchdog_if_needed();
}

std::int64_t hang_timeout_ms()
{
    ensure_init();
    return logger().hangMs.load(std::memory_order_relaxed);
}

} // namespace log
} // namespace ps2x
