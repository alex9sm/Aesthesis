#include "log.hpp"
#include "types.hpp"
#include "string.hpp"

extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* str);

namespace logger {

    namespace {
        Level min_level = Level::Trace;
    }

    void set_level(Level level) {
        min_level = level;
    }

    static void write(Level level, const char* prefix, const char* fmt, va_list args) {

        if (level < min_level) return;

        char buffer[2048];
        char* ptr = buffer;
        usize remaining = sizeof(buffer);
        usize prefix_len = str::length(prefix);
        str::copy(ptr, prefix, remaining);
        ptr += prefix_len;
        remaining -= prefix_len;
        int written = str::format_v(ptr, remaining, fmt, args);
        if (written > 0) {
            ptr += written;
            remaining -= written;
        }

        if (remaining > 2) {
            *ptr++ = '\n';
            *ptr = '\0';
        }

        OutputDebugStringA(buffer);

    }

    void trace(const char* fmt, ...) {

        va_list args;
        va_start(args, fmt);
        write(Level::Trace, "[TRACE] ", fmt, args);
        va_end(args);

    }

    void debug(const char* fmt, ...) {

        va_list args;
        va_start(args, fmt);
        write(Level::Debug, "[DEBUG] ", fmt, args);
        va_end(args);

    }

    void info(const char* fmt, ...) {

        va_list args;
        va_start(args, fmt);
        write(Level::Info, "[INFO] ", fmt, args);
        va_end(args);

    }

    void warn(const char* fmt, ...) {

        va_list args;
        va_start(args, fmt);
        write(Level::Warn, "[WARN] ", fmt, args);
        va_end(args);

    }

    void error(const char* fmt, ...) {

        va_list args;
        va_start(args, fmt);
        write(Level::Error, "[ERROR] ", fmt, args);
        va_end(args);

    }

    void fatal(const char* fmt, ...) {

        va_list args;
        va_start(args, fmt);
        write(Level::Fatal, "[FATAL] ", fmt, args);
        va_end(args);

    }

}
