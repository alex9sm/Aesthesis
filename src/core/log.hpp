#pragma once

namespace logger {

    enum class Level { Trace, Debug, Info, Warn, Error, Fatal };

    void set_level(Level level);

    void trace(const char* fmt, ...);
    void debug(const char* fmt, ...);
    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);
    void fatal(const char* fmt, ...);

}
