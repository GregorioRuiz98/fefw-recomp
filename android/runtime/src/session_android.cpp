// SPDX-License-Identifier: GPL-3.0-or-later
#include "nxrt/android_session.h"
#include "nxrt/module.h"
#include "nxrt/memory.h"
#include "nxrt/dispatch.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <stdexcept>

namespace nxrt {
namespace {
class ReadMapping {
public:
    explicit ReadMapping(int descriptor) {
        struct stat info{};
        if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)
            || info.st_size < 0x100 || info.st_size > 1024 * 1024 * 1024)
            throw std::runtime_error("A module must be a readable local NSO file between 256 bytes and 1 GiB");
        size_ = static_cast<size_t>(info.st_size);
        data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, descriptor, 0);
        if (data_ == MAP_FAILED) throw std::runtime_error("Cannot map a module from the selected folder provider");
    }
    ~ReadMapping() { if (data_ != MAP_FAILED) munmap(data_, size_); }
    ReadMapping(const ReadMapping&) = delete;
    ReadMapping& operator=(const ReadMapping&) = delete;
    const void* data() const { return data_; }
    size_t size() const { return size_; }
private:
    void* data_ = MAP_FAILED;
    size_t size_ = 0;
};
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\') result += '\\';
        if (character >= 32) result += static_cast<char>(character);
    }
    return result + '"';
}
struct SessionCleanup {
    ~SessionCleanup() { modules_unload_all(); hle_clear(); mem::shutdown(); }
};
}
std::recursive_mutex& android_runtime_mutex() { static std::recursive_mutex lock; return lock; }

std::string android_inspect_modules(const std::array<int, 6>& descriptors) {
    std::lock_guard lock(android_runtime_mutex());
    if (mem::initialised() || !modules().empty() || fn_count() || hle_count())
        return "{\"status\":\"BUSY\",\"game_runtime_available\":false}";
    SessionCleanup cleanup;
    unsigned loaded = 0;
    std::ostringstream details;
    std::string error;
    size_t checked = 0, deferred = 0;
    try {
        if (!mem::init()) throw std::runtime_error("Cannot initialize guest memory; this backend requires 4 KB host pages");
        constexpr const char* names[] = {"main", "rtld", "sdk", "subsdk0", "subsdk1", "subsdk2"};
        constexpr uint64_t bases[] = {0x7100000000ull, 0x7700000000ull, 0x7500000000ull,
                                     0x7600000000ull, 0x7610000000ull, 0x7620000000ull};
        for (unsigned i = 0; i < descriptors.size(); ++i) {
            ReadMapping input(descriptors[i]);
            Module* image = module_load_nso_memory(input.data(), input.size(), {.name=names[i], .base=bases[i]}, &error);
            if (!image) throw std::runtime_error(std::string(names[i]) + ": " + error);
            if (loaded++) details << ',';
            details << "{\"name\":" << quote(names[i]) << ",\"base\":" << image->base()
                    << ",\"mapped_bytes\":" << image->size() << ",\"symbols\":" << image->symbols().size() << '}';
        }
        if (!module_link_all()) throw std::runtime_error("Module relocation failed; inspect the native runtime log");
        for (const auto* image : modules()) {
            checked += image->stats().total;
            deferred += image->stats().deferred_tlsdesc;
            if (image->stats().out_of_range || image->stats().unhandled)
                throw std::runtime_error(image->name() + ": unsupported relocations="
                    + std::to_string(image->stats().unhandled) + ", out of range="
                    + std::to_string(image->stats().out_of_range));
        }
    } catch (const std::exception& failure) { error = failure.what(); }
    const size_t unresolved = hle_stub_count();
    modules_unload_all(); hle_clear(); mem::shutdown();
    std::ostringstream report;
    report << "{\"status\":" << quote(error.empty() ? "PASS" : "FAIL")
           << ",\"module_loader_available\":true,\"game_runtime_available\":false,\"guest_code_executed\":false"
           << ",\"modules_loaded\":" << loaded << ",\"unresolved_imports\":" << unresolved
           << ",\"relocations_checked\":" << checked << ",\"deferred_tls_descriptors\":" << deferred
           << ",\"guest_linker_executed\":false"
           << ",\"cleanup_verified\":" << (!mem::initialised() && modules().empty() ? "true" : "false")
           << ",\"error\":" << quote(error) << ",\"modules\":[" << details.str() << "]}";
    return report.str();
}
}  // namespace nxrt
