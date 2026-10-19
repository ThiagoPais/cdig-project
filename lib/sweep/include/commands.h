#pragma once
#include <stdint.h>

// Serial command parser (plan D9). Lines end with '\n'; '\r' is ignored; commands are
// case-sensitive; leading/trailing spaces are trimmed; empty lines produce nothing.
//   start        -> Start, startPercent = 0
//   start N      -> Start, startPercent = N (0..SWEEP_CAP_PERCENT), otherwise BadStart
//   stop | results | temp
//   anything else (including a line longer than the buffer) -> Unknown
enum class CmdType { None, Start, Stop, Results, Temp, Unknown, BadStart };

struct Command {
    CmdType type;
    float startPercent;
};

class LineBuffer {
public:
    // Feeds one character. Returns true when a non-empty line completed; the parsed
    // command is then stored in *out.
    bool push(char c, Command* out);

private:
    static constexpr uint8_t kCapacity = 32;
    char buf_[kCapacity];
    uint8_t len_ = 0;
    bool overflow_ = false;
};
