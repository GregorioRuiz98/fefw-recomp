// SPDX-License-Identifier: GPL-3.0-or-later
// Android LM (ILogService / ILogger), adapted from the pinned nxrt logfault.cpp.
// Command ids and layouts: https://switchbrew.org/wiki/Log_services (LogPacketHeader, LogPacketFlags, LogDataChunkKey, ILogger),
// Adapted from Ryujinx (MIT, Copyright (c) Ryujinx Team and Contributors): src/Ryujinx.Horizon/LogManager/Ipc/LmLogger.cs (the
// message assembly and the TLV reading, with the size-driven numeric fields and the 5000-character cap), src/Ryujinx.Horizon/
// LogManager/Types/LogPacket.cs (the line fields). Original behaviour differences
// are noted below. Windows fatal services are not included in this adapter.
//
// lm: OpenLogger(pid) returns an ILogger. Log() takes one LogPacket (a 0x18-byte header, then TLV chunks: LEB128 type, LEB128 size,
// data). A packet with the Head flag starts a message, the Tail flag completes it; text chunks are concatenated. Each completed
// message becomes one line in the runtime log, tag "guestlog", level from the severity.


#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nxrt/ipc.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"

namespace nxrt::services {

namespace {
template<class F> std::shared_ptr<ipc::Service> make_iface(const char* name,F configure) {
    auto service=std::make_shared<ipc::Service>(name); configure(*service); return service;
}

// ---- LogPacketHeader (Switchbrew Log services) -----------------------------------------------------------------------------------
constexpr size_t kHeaderSize = 0x18;          // 0x0 pid u64, 0x8 tid u64, 0x10 flags u8, 0x11 pad, 0x12 severity u8, 0x13 verbosity, 0x14 payload u32
constexpr uint8_t kFlagHead = 0x1;            // LogPacketFlags::Head
constexpr uint8_t kFlagTail = 0x2;            // LogPacketFlags::Tail
constexpr size_t kMessageLimit = 5000;        // Ryujinx LmLogger MessageLengthLimit: a longer message is cut and completed
constexpr size_t kMaxBuffer = 1u << 20;       // a Log buffer above 1 MiB is refused (no legal packet is that large)

// LogDataChunkKey (Switchbrew Log services). Start/Stop and the session/time fields are read and skipped.
enum : uint64_t {
    kChunkSessionBegin = 0,
    kChunkSessionEnd = 1,
    kChunkTextLog = 2,
    kChunkLineNumber = 3,
    kChunkFileName = 4,
    kChunkFunctionName = 5,
    kChunkModuleName = 6,
    kChunkThreadName = 7,
    kChunkPacketDropCount = 8,
    kChunkUserSystemClock = 9,
    kChunkProcessName = 10,
};

// LEB128 unsigned. False when the data ends inside the number or the number is longer than 64 bits.
bool read_uleb(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
    out = 0;
    unsigned shift = 0;
    for (;;) {
        if (p >= end || shift >= 64) return false;
        const uint8_t b = *p++;
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
}

// A numeric chunk: little-endian, as many bytes as the chunk has (the first 8 count).
uint64_t read_le(const uint8_t* d, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n && i < 8; ++i) v |= static_cast<uint64_t>(d[i]) << (8 * i);
    return v;
}

// Trailing blanks and NULs removed, control characters made printable (a line stays one line).
std::string clean(const std::string& in) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) {
        if (c == '\n') s += "\\n";
        else if (c == '\r') s += "\\r";
        else if (static_cast<unsigned char>(c) < 0x20 && c != '\t') s += '.';
        else s += c;
    }
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\0')) --e;
    s.resize(e);
    return s;
}

// severity (nn::diag::LogSeverity: Trace 0, Info 1, Warn 2, Error 3, Fatal 4) -> runtime log level. Fatal and unknown values log as Error.
LogLevel level_of(uint8_t severity) {
    switch (severity) {
        case 0: return LogLevel::Trace;
        case 1: return LogLevel::Info;
        case 2: return LogLevel::Warn;
        default: return LogLevel::Error;
    }
}

// One message while its packets arrive.
struct Message {
    bool open = false;
    uint8_t severity = 1;
    std::string text, file, function, module, thread;
    bool has_line = false;
    uint64_t line = 0;
    uint64_t drop_count = 0;
};

// Writes one line for a completed message: "[module] file:line function: text" with the fields that are present, then
// " (thread T, N packets dropped)" when those are present. A message with no text and no fields writes nothing.
void emit(const Message& m) {
    const std::string text = clean(m.text);
    std::string s;
    if (!m.module.empty()) s += "[" + clean(m.module) + "] ";
    std::string loc = clean(m.file);
    if (m.has_line) {
        const std::string n = std::to_string(m.line);
        if (loc.empty()) loc = "line " + n;
        else loc += ":" + n;
    }
    if (!loc.empty()) {
        s += loc;
        if (!m.function.empty()) s += " " + clean(m.function);
        s += ": ";
    } else if (!m.function.empty()) {
        s += clean(m.function) + ": ";
    }
    s += text;
    std::string extra;
    if (!m.thread.empty()) extra += "thread " + clean(m.thread);
    if (m.drop_count) {
        if (!extra.empty()) extra += ", ";
        extra += std::to_string(m.drop_count) + " packets dropped";
    }
    if (!extra.empty()) s += " (" + extra + ")";
    if (s.empty()) {
        NXLOG_D("guestlog", "empty message from the guest, nothing written");
        return;
    }
    logf(level_of(m.severity), "guestlog", "%s", s.c_str());
}

// Reads the TLV chunks of one packet into the open message. A malformed chunk stops the packet (what was read stays).
void parse_chunks(Message& m, const uint8_t* p, const uint8_t* end) {
    while (p < end) {
        uint64_t type = 0, size = 0;
        if (!read_uleb(p, end, type) || !read_uleb(p, end, size) || size > static_cast<uint64_t>(end - p)) {
            NXLOG_W("guestlog", "malformed log chunk (%zu bytes left in the packet), rest of the packet ignored",
                    static_cast<size_t>(end - p));
            return;
        }
        const uint8_t* d = p;
        const size_t n = static_cast<size_t>(size);
        p += n;
        switch (type) {
            case kChunkTextLog: m.text.append(reinterpret_cast<const char*>(d), n); break;
            case kChunkLineNumber: m.line = read_le(d, n); m.has_line = true; break;
            case kChunkFileName: m.file.assign(reinterpret_cast<const char*>(d), n); break;
            case kChunkFunctionName: m.function.assign(reinterpret_cast<const char*>(d), n); break;
            case kChunkModuleName: m.module.assign(reinterpret_cast<const char*>(d), n); break;
            case kChunkThreadName: m.thread.assign(reinterpret_cast<const char*>(d), n); break;
            case kChunkPacketDropCount: m.drop_count = read_le(d, n); break;
            // SessionBegin / SessionEnd carry no text; UserSystemClock and ProcessName are read and not printed; unknown keys skipped.
            case kChunkSessionBegin:
            case kChunkSessionEnd:
            case kChunkUserSystemClock:
            case kChunkProcessName:
            default: break;
        }
    }
}

// One ILogger's message assembly. Shared by the command handlers of that logger session.
struct LoggerState {
    std::mutex mu;
    Message msg;

    void accept(const uint8_t* p, size_t n) {
        if (n < kHeaderSize) {
            NXLOG_W("guestlog", "log packet of %zu bytes is shorter than its 0x18-byte header, dropped", n);
            return;
        }
        uint32_t payload = 0;
        std::memcpy(&payload, p + 0x14, 4);
        if (n != kHeaderSize + static_cast<size_t>(payload)) {
            NXLOG_W("guestlog", "log packet size mismatch (buffer 0x%zx, header says 0x%zx), dropped", n,
                    kHeaderSize + static_cast<size_t>(payload));
            return;
        }
        const uint8_t flags = p[0x10];
        const uint8_t severity = p[0x12];
        const bool head = (flags & kFlagHead) != 0;
        bool tail = (flags & kFlagTail) != 0;
        if (head) {
            if (msg.open) NXLOG_W("guestlog", "a new message started before the previous one was completed; %zu bytes dropped",
                                  msg.text.size());
            msg = Message{};
            msg.open = true;
            msg.severity = severity;
        } else if (!msg.open) {
            NXLOG_W("guestlog", "continuation packet without a head packet; it starts a message");
            msg = Message{};
            msg.open = true;
            msg.severity = severity;
        }
        parse_chunks(msg, p + kHeaderSize, p + n);
        if (msg.text.size() >= kMessageLimit) {
            msg.text.resize(kMessageLimit);
            tail = true;
        }
        if (tail) {
            emit(msg);
            msg = Message{};
        }
    }
};

// ---- process-wide state ----------------------------------------------------------------------------------------------------------
struct State {
    std::mutex mu;
    uint32_t destination = 1;      // ILogger SetDestination mask (TMA); stored as a global value, nothing else reads it
};
State& state() {
    static State s;
    return s;
}

// ---- lm ----------------------------------------------------------------------------------------------------------------------------
uint32_t log_cmd(const std::shared_ptr<LoggerState>& st, ipc::Request& req) {
    const ipc::Buffer b = req.in_buf(0);
    if (!b.addr || b.size == 0) {
        NXLOG_W("guestlog", "Log: no message buffer");
        return 0;
    }
    if (b.size > kMaxBuffer) {
        NXLOG_W("guestlog", "Log: message buffer of 0x%llx bytes refused", static_cast<unsigned long long>(b.size));
        return 0;
    }
    std::lock_guard<std::mutex> lk(st->mu);
    if(!mem::is_readable(b.addr,b.size)) return kernel::kInvalidPointer;
    st->accept(reinterpret_cast<const uint8_t*>(b.addr), static_cast<size_t>(b.size));
    return 0;   // Switchbrew: Log always returns success
}

std::shared_ptr<ipc::Service> make_logger() {
    auto st = std::make_shared<LoggerState>();
    return make_iface("lm:ILogger", [st](ipc::Service& s) {
        s.add_cmd(0, "Log", [st](ipc::Request& req, ipc::Response&) -> uint32_t { return log_cmd(st, req); });
        s.add_cmd(1, "SetDestination", [](ipc::Request& req, ipc::Response&) -> uint32_t {
            const uint32_t mask = req.arg<uint32_t>(0);
            State& g = state();
            std::lock_guard<std::mutex> lk(g.mu);
            g.destination = mask;
            NXLOG_D("lm", "SetDestination 0x%x", mask);
            return 0;
        });
    });
}

} // namespace
void register_android_log_service() {
    ipc::register_service("lm",[] {
        return make_iface("lm",[](ipc::Service& service) {
            service.add_cmd(0,"OpenLogger",[](ipc::Request&,ipc::Response& response) -> uint32_t {
                response.push_object(make_logger()); return kernel::kOk;
            });
        });
    });
}
} // namespace nxrt::services
