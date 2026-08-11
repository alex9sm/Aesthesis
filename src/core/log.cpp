#include "log.hpp"
#include "types.hpp"
#include "string.hpp"
#include <stdio.h>

// The only OS-specific code in core/. Everything else runs on the C runtime.
#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* str);
#endif

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
        constexpr usize CAP = sizeof(buffer);

        usize len = str::copy(buffer, prefix, CAP);

        int written = str::format_v(buffer + len, CAP - len, fmt, args);
        if (written > 0) {
            len += (usize)written;
            // format_v reports the untruncated length; clamp to what it wrote.
            if (len > CAP - 2) len = CAP - 2;
        }

        buffer[len++] = '\n';
        buffer[len] = '\0';

        fputs(buffer, stderr);

#if defined(_WIN32)
        OutputDebugStringA(buffer);
#endif

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
