/*
 ArenaForge - C++ application layer
 Tiny JSON writer (no dependencies). Used to hand state to the web front end.
 */
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace arena {

class Json {
public:
    Json& beginObject() { sep(); s_ += '{'; close_.push_back('}'); comma_ = false; return *this; }
    Json& beginArray()  { sep(); s_ += '['; close_.push_back(']'); comma_ = false; return *this; }
    Json& end()         { s_ += close_.back(); close_.pop_back(); comma_ = true; return *this; }

    Json& key(const std::string& k) { sep(); quote(k); s_ += ':'; comma_ = false; return *this; }
    Json& text(const std::string& v) { sep(); quote(v); comma_ = true; return *this; }
    Json& i(long long v) { sep(); s_ += std::to_string(v); comma_ = true; return *this; }
    Json& d(double v, int prec = 2) {
        sep();
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", prec, v);
        s_ += buf;
        comma_ = true;
        return *this;
    }
    Json& b(bool v) { sep(); s_ += v ? "true" : "false"; comma_ = true; return *this; }
    Json& nul() { sep(); s_ += "null"; comma_ = true; return *this; }

    Json& kvs(const std::string& k, const std::string& v) { return key(k).text(v); }
    Json& kvi(const std::string& k, long long v) { return key(k).i(v); }
    Json& kvd(const std::string& k, double v, int prec = 2) { return key(k).d(v, prec); }
    Json& kvb(const std::string& k, bool v) { return key(k).b(v); }

    const std::string& result() const { return s_; }

private:
    void sep() { if (comma_) s_ += ','; }
    void quote(const std::string& v) {
        s_ += '"';
        for (unsigned char c : v) {
            switch (c) {
                case '"':  s_ += "\\\""; break;
                case '\\': s_ += "\\\\"; break;
                case '\n': s_ += "\\n"; break;
                case '\r': s_ += "\\r"; break;
                case '\t': s_ += "\\t"; break;
                default:
                    if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); s_ += buf; }
                    else s_ += static_cast<char>(c);
            }
        }
        s_ += '"';
    }

    std::string s_;
    std::vector<char> close_;
    bool comma_ = false;
};

} // namespace arena
