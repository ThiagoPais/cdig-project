#include "commands.h"

#include <stdlib.h>
#include <string.h>

#include "SweepConfig.h"

namespace {
constexpr float kMaxStartPercent = sweepcfg::kCapPercent;

bool isSpace(char c) { return c == ' ' || c == '\t'; }

Command make(CmdType type, float percent = 0.0f, uint32_t count = 0) {
    Command c;
    c.type = type;
    c.startPercent = percent;
    c.count = count;
    return c;
}

bool isWord(const char* s, const char* word) {
    const size_t n = strlen(word);
    return strncmp(s, word, n) == 0 && (s[n] == '\0' || isSpace(s[n]));
}

// `prbs load N` and `prbs run`; every other line that starts with the word `prbs` is BadPrbs.
Command parsePrbs(const char* args) {
    while (isSpace(*args)) ++args;
    if (strcmp(args, "run") == 0) return make(CmdType::PrbsRun);
    if (isWord(args, "load")) {
        const char* arg = args + 4;
        while (isSpace(*arg)) ++arg;
        if (*arg == '\0') return make(CmdType::BadPrbs);
        size_t digits = 0;
        uint32_t value = 0;
        for (const char* p = arg; *p != '\0'; ++p, ++digits) {
            if (*p < '0' || *p > '9') return make(CmdType::BadPrbs);
            if (digits < 9) value = value * 10 + static_cast<uint32_t>(*p - '0');
        }
        return make(CmdType::PrbsLoad, 0.0f, digits > 9 ? UINT32_MAX : value);
    }
    return make(CmdType::BadPrbs);
}

Command parse(char* line) {
    while (isSpace(*line)) ++line;
    size_t n = strlen(line);
    while (n > 0 && isSpace(line[n - 1])) line[--n] = '\0';

    if (strcmp(line, "stop") == 0) return make(CmdType::Stop);
    if (strcmp(line, "results") == 0) return make(CmdType::Results);
    if (strcmp(line, "temp") == 0) return make(CmdType::Temp);
    if (isWord(line, "prbs")) return parsePrbs(line + 4);
    if (strncmp(line, "start", 5) == 0 && (line[5] == '\0' || isSpace(line[5]))) {
        const char* arg = line + 5;
        while (isSpace(*arg)) ++arg;
        if (*arg == '\0') return make(CmdType::Start, 0.0f);
        char* end = nullptr;
        const float value = strtof(arg, &end);
        if (end == arg || *end != '\0' || !(value >= 0.0f) || value > kMaxStartPercent) {
            return make(CmdType::BadStart);
        }
        return make(CmdType::Start, value);
    }
    return make(CmdType::Unknown);
}
}  // namespace

bool LineBuffer::push(char c, Command* out) {
    if (c == '\r') return false;
    if (c != '\n') {
        if (len_ < kCapacity - 1) {
            buf_[len_++] = c;
        } else {
            overflow_ = true;
        }
        return false;
    }
    buf_[len_] = '\0';
    const bool tooLong = overflow_;
    len_ = 0;
    overflow_ = false;
    if (tooLong) {
        *out = make(CmdType::Unknown);
        return true;
    }
    const char* p = buf_;
    while (isSpace(*p)) ++p;
    if (*p == '\0') return false;  // empty or whitespace-only lines are ignored
    *out = parse(buf_);
    return true;
}
