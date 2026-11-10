// Android adaptation of Goldoire nxrt am.cpp at a538375a. Host lifecycle is supplied by Android.
// SPDX-License-Identifier: GPL-3.0-or-later
// appletOE and appletAE (S3c): the applet manager as the one application sees it. The application is in focus, its message
// queue holds what the console queues for it, the notification flags it sets are real state. Library applets: the software keyboard
// are not integrated in this Android bootstrap adaptation.
// Command ids and layouts follow the Switchbrew "Applet Manager services" page and ScarletNX's am.swipc.
// Adapted from ScarletNX src/nx/hle/services/am.c and am.swipc (GPL-3.0-or-later): the command table, the message queue, the lock
// accessor, the audio volumes, the save data rules and the launch parameter storage.
// Adapted from Ryujinx src/Ryujinx.HLE/HOS/Services/Am (MIT): the result codes, the LaunchParameterKind values, the preselected user
// layout (AppletAE/Storage/StorageHelper.cs MakeLaunchParams) and the GamePlay recording command id.
// Adapted from SuperMarioWonderRecomp runtime/src/svc_am.cpp (MIT): the window controller's applet resource user id.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nxrt/config.h"
#include "nxrt/kernel.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"
#include "nxrt/android_services.h"

namespace nxrt::services {

namespace {
std::atomic<bool> host_focus{true};

using Bytes = std::vector<uint8_t>;
using ipc::Handler;
using ipc::Request;
using ipc::Response;
using ipc::Service;
using ServicePtr = std::shared_ptr<Service>;

// nn::am::AppletMessage values the queue carries (Ryujinx AppletAE/AllSystemAppletProxiesService/.../AppletMessage.cs).
constexpr uint32_t kMsgFocusStateChanged = 15;
constexpr uint32_t kMsgOperationModeChanged = 30;
constexpr uint32_t kMsgPerformanceModeChanged = 31;
constexpr uint32_t kMsgStartupLogoDisappeared = 95;

// nn::am::service::FocusState: InFocus 1, OutOfFocus 2, Background 3.
constexpr uint8_t kFocusInFocus = 1;
constexpr uint8_t kFocusOutOfFocus = 2;
constexpr uint64_t kAppletResourceUserId = 0x51;

// Largest storage a guest can ask for (CreateStorage and the transfer / handle storages).
constexpr int64_t kMaxStorageSize = 0x10000000;

// Am results (module 128): result = module | description << 9 (services.h res()).
const uint32_t kNoMessages = res(128, 3);            // ResultNoMessages: 0x680
const uint32_t kInvalidOffset = res(128, 503);       // ResultInvalidOffset
const uint32_t kOutOfMemory = res(128, 600);         // MemoryAllocationFailed
// A refused library applet. The SDK (nn::applet::CreateLibraryApplet) fails without aborting only for am descriptions 701..749
// (anything else, e.g. 128-35 AppletLaunchFailed, ends in svcBreak: work/audit/crashes.md); 712 is Switchbrew's "thread stack pool
// exhausted (out of memory)", a resource refusal the caller gets back as a plain failure.
const uint32_t kLibraryAppletFailed = res(128, 712);

// Everything the applet manager keeps for the process. Replaced by fresh_state() on start-up and by am_reset().
struct AmState {
    std::deque<uint32_t> msgs;                         // the AppletMessage queue, oldest first
    ServiceEvent msg_event{false};                     // signalled while msgs is not empty
    ServiceEvent sleep_lock_event{false};
    ServiceEvent launchable_event{true};               // a library applet can always be launched
    ServiceEvent tick_event{true};                     // accumulated suspended tick changed
    ServiceEvent never_event{false};                   // the events that never fire
    bool docked = true;
    uint8_t focus = kFocusInFocus;
    bool operation_notify = false;                     // SetOperationModeChangedNotification
    bool performance_notify = false;                   // SetPerformanceModeChangedNotification
    bool vr_mode = false;
    bool vr_mode_3d = false;
    bool auto_sleep_disabled = false;
    uint32_t idle_extension = 0;
    uint64_t next_layer = 1;
    bool preselected_popped = false;
    std::vector<std::shared_ptr<Bytes>> user_channel;  // UnpopToUserChannel pushes; PopLaunchParameter(UserChannel) pops the last
    bool save_extended = false;                        // ExtendSaveData was called for the account save data
    int64_t save_normal = 0;
    int64_t save_journal = 0;
};

// One mutex for the whole applet manager: the host threads (amctl) and the guest's sessions both go through it.
std::mutex& mu() {
    static std::mutex m;
    return m;
}

// The state a fresh application starts in: in focus, docked as the profile says, and FocusStateChanged queued (Ryujinx
// AppletStateMgr queues exactly that; the operation and performance mode messages come only when the mode changes).
AmState fresh_state() {
    AmState s;
    s.docked = nxrt::config_get_str("am", "operation_mode", "handheld") == "docked";
    s.focus = host_focus ? kFocusInFocus : kFocusOutOfFocus;
    s.msgs.push_back(kMsgFocusStateChanged);
    s.msg_event.signal();
    return s;
}

AmState& st() {
    static AmState s = fresh_state();
    return s;
}

// Queues one AppletMessage and signals the message event. The caller holds mu().
void queue_message(uint32_t msg) {
    st().msgs.push_back(msg);
    st().msg_event.signal();
}

// A command the console answers without doing anything (am.swipc marks it @stub): logged at Debug, with `zero_bytes` of zero output
// so the reply has the layout the SDK reads.
Handler ignored(const char* name, size_t zero_bytes = 0) {
    return [name, zero_bytes](Request&, Response& rsp) -> uint32_t {
        NXLOG_D("am", "%s: accepted, nothing done", name);
        rsp.out.insert(rsp.out.end(), zero_bytes, uint8_t{0});
        return 0;
    };
}

// Copy handles belong to the client (HIPC copies them, the guest closes its own afterwards): nothing to close here (debug note 122).
void close_copies(const Request&) {}

// Byte range [off, off + n) lies inside a storage of `size` bytes.
bool span_ok(uint64_t size, int64_t off, uint64_t n) {
    return off >= 0 && static_cast<uint64_t>(off) <= size && n <= size - static_cast<uint64_t>(off);
}

bool storage_size_ok(int64_t size) { return size >= 0 && size <= kMaxStorageSize; }

// Appends `n` bytes of `src` (padded with zeros) to a reply: fixed-size outputs such as the display version.
void push_bytes(Response& rsp, const void* src, size_t src_len, size_t n) {
    const size_t at = rsp.out.size();
    rsp.out.resize(at + n, 0);
    if (src && src_len) std::memcpy(rsp.out.data() + at, src, src_len < n ? src_len : n);
}

ServicePtr make_accessor(std::shared_ptr<Bytes> data);

// IStorage over a host byte vector (the launch parameters, CreateStorage and the transfer / handle storages). Open is the one
// command here (IStorage's OpenTransferStorage has no accessor in this model).
class AmStorage : public Service {
public:
    explicit AmStorage(std::shared_ptr<Bytes> bytes) : Service("IStorage"), data(std::move(bytes)) {
        add_cmd(0, "Open", [d = data](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_accessor(d));
            return 0;
        });
    }
    std::shared_ptr<Bytes> data;
};

ServicePtr new_storage(std::shared_ptr<Bytes> data) { return std::make_shared<AmStorage>(std::move(data)); }

// IStorageAccessor: GetSize, Write (guest -> storage), Read (storage -> guest). Ranges past the end are refused, not cut.
ServicePtr make_accessor(std::shared_ptr<Bytes> data) {
    return make_iface("IStorageAccessor", [data](Service& s) {
        s.add_cmd(0, "GetSize", [data](Request&, Response& rsp) -> uint32_t {
            rsp.push<int64_t>(static_cast<int64_t>(data->size()));
            return 0;
        });
        s.add_cmd(10, "Write", [data](Request& req, Response&) -> uint32_t {
            const int64_t off = req.arg<int64_t>(0);
            const ipc::Buffer b = req.in_buf(0);
            if (!span_ok(data->size(), off, b.size)) return kInvalidOffset;
            if (b.size && !mem::is_readable(b.addr, b.size)) return kernel::kInvalidPointer;
            if (b.size) std::memcpy(data->data() + off, reinterpret_cast<const void*>(b.addr), b.size);
            return 0;
        });
        s.add_cmd(11, "Read", [data](Request& req, Response&) -> uint32_t {
            const int64_t off = req.arg<int64_t>(0);
            const ipc::Buffer b = req.out_buf(0);
            if (!span_ok(data->size(), off, b.size)) return kInvalidOffset;
            if (b.size && !mem::is_writable(b.addr, b.size)) return kernel::kInvalidPointer;
            if (b.size) std::memcpy(reinterpret_cast<void*>(b.addr), data->data() + off, b.size);
            return 0;
        });
    });
}

// ILockAccessor: granted whenever it is free. The event is signalled while it is free (libnx waits on it before TryLock) and
// again when it is released.
struct LockState {
    bool locked = false;
    ServiceEvent event{true};
};

ServicePtr make_lock_accessor() {
    auto l = std::make_shared<LockState>();
    return make_iface("ILockAccessor", [l](Service& s) {
        s.add_cmd(1, "TryLock", [l](Request& req, Response& rsp) -> uint32_t {
            const bool return_handle = req.arg<uint8_t>(0) != 0;
            bool acquired;
            {
                std::lock_guard<std::mutex> lk(mu());
                acquired = !l->locked;
                l->locked = true;
                l->event.clear();
            }
            rsp.push<uint8_t>(acquired ? 1 : 0);
            if (return_handle) rsp.out_copy.push_back(l->event.new_handle());
            return 0;
        });
        s.add_cmd(2, "Unlock", [l](Request&, Response&) -> uint32_t {
            {
                std::lock_guard<std::mutex> lk(mu());
                l->locked = false;
            }
            l->event.signal();
            return 0;
        });
        s.add_cmd(3, "GetEvent", [l](Request&, Response& rsp) -> uint32_t {
            rsp.out_copy.push_back(l->event.new_handle());
            return 0;
        });
        s.add_cmd(4, "IsLocked", [l](Request&, Response& rsp) -> uint32_t {
            bool locked;
            {
                std::lock_guard<std::mutex> lk(mu());
                locked = l->locked;
            }
            rsp.push<uint8_t>(locked ? 1 : 0);
            return 0;
        });
    });
}

// IAudioController: the expected volumes, kept per controller as ScarletNX keeps them. The fade of ChangeMainAppletMasterVolume is
// not played.
struct Volumes {
    float main = 1.0f;
    float library = 1.0f;
};

float clamp_volume(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

ServicePtr make_audio_controller() {
    auto v = std::make_shared<Volumes>();
    return make_iface("IAudioController", [v](Service& s) {
        s.add_cmd(0, "SetExpectedMasterVolume", [v](Request& req, Response&) -> uint32_t {
            std::lock_guard lock(mu());
            v->main = clamp_volume(req.arg<float>(0));
            v->library = clamp_volume(req.arg<float>(4));
            return 0;
        });
        s.add_cmd(1, "GetMainAppletExpectedMasterVolume", [v](Request&, Response& rsp) -> uint32_t {
            std::lock_guard lock(mu());
            rsp.push<float>(v->main);
            return 0;
        });
        s.add_cmd(2, "GetLibraryAppletExpectedMasterVolume", [v](Request&, Response& rsp) -> uint32_t {
            std::lock_guard lock(mu());
            rsp.push<float>(v->library);
            return 0;
        });
        s.add_cmd(3, "ChangeMainAppletMasterVolume", [v](Request& req, Response&) -> uint32_t {
            std::lock_guard lock(mu());
            v->main = clamp_volume(req.arg<float>(0));
            return 0;
        });
        s.add_cmd(4, "SetTransparentVolumeRate", ignored("SetTransparentVolumeRate"));
    });
}

// [am] resolution = WxH overrides the default resolution of the mode (ICommonStateGetter::GetDefaultDisplayResolution).
bool parse_resolution(const std::string& text, uint32_t* w, uint32_t* h) {
    if (text.empty()) return false;
    char* end = nullptr;
    const unsigned long a = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != 'x') return false;
    const char* p = end + 1;
    const unsigned long b = std::strtoul(p, &end, 10);
    if (end == p || *end != '\0' || a == 0 || b == 0 || a > 0xFFFF || b > 0xFFFF) return false;
    *w = static_cast<uint32_t>(a);
    *h = static_cast<uint32_t>(b);
    return true;
}

void default_resolution(bool docked, uint32_t* w, uint32_t* h) {
    *w = docked ? 1920 : 1280;
    *h = docked ? 1080 : 720;
    uint32_t cw = 0, ch = 0;
    if (parse_resolution(nxrt::config_get_str("am", "resolution", ""), &cw, &ch)) {
        *w = cw;
        *h = ch;
    }
}

// The events that never fire (GetDefaultDisplayResolutionChangeEvent and the rest): each call hands out a new handle onto this one.
ServiceEvent& never_event() { return st().never_event; }

// ICommonStateGetter: the message queue, the operation and performance modes, VR mode, the default resolution and the boost calls.
ServicePtr make_common_state_getter() {
    return make_iface("ICommonStateGetter", [](Service& s) {
        s.add_cmd(0, "GetEventHandle", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.out_copy.push_back(st().msg_event.new_handle());
            return 0;
        });
        s.add_cmd(1, "ReceiveMessage", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            AmState& a = st();
            if (a.msgs.empty()) {
                a.msg_event.clear();
                return kNoMessages;
            }
            rsp.push<uint32_t>(a.msgs.front());
            a.msgs.pop_front();
            if (a.msgs.empty()) a.msg_event.clear();
            return 0;
        });
        s.add_cmd(2, "GetThisAppletKind", ignored("GetThisAppletKind", 8));
        s.add_cmd(5, "GetOperationMode", [](Request&, Response& rsp) -> uint32_t {   // nn::oe::OperationMode: Handheld 0, Console 1
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint8_t>(st().docked ? 1 : 0);
            return 0;
        });
        s.add_cmd(6, "GetPerformanceMode", [](Request&, Response& rsp) -> uint32_t {   // nn::oe::PerformanceMode: Handheld 0, Boost 1
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint32_t>(st().docked ? 1u : 0u);
            return 0;
        });
        s.add_cmd(8, "GetBootMode", ignored("GetBootMode", 1));
        s.add_cmd(9, "GetCurrentFocusState", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint8_t>(st().focus);
            return 0;
        });
        s.add_cmd(10, "RequestToAcquireSleepLock", [](Request&, Response&) -> uint32_t {
            st().sleep_lock_event.signal();
            return 0;
        });
        s.add_cmd(11, "ReleaseSleepLock", ignored("ReleaseSleepLock"));
        s.add_cmd(12, "ReleaseSleepLockTransiently", ignored("ReleaseSleepLockTransiently"));
        s.add_cmd(13, "GetAcquiredSleepLockEvent", [](Request&, Response& rsp) -> uint32_t {
            rsp.out_copy.push_back(st().sleep_lock_event.new_handle());
            return 0;
        });
        s.add_cmd(30, "GetHomeButtonReaderLockAccessor", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_lock_accessor());
            return 0;
        });
        s.add_cmd(31, "GetReaderLockAccessorEx", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_lock_accessor());
            return 0;
        });
        s.add_cmd(32, "GetWriterLockAccessorEx", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_lock_accessor());
            return 0;
        });
        s.add_cmd(50, "IsVrModeEnabled", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint8_t>(st().vr_mode ? 1 : 0);
            return 0;
        });
        s.add_cmd(51, "SetVrModeEnabled", [](Request& req, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().vr_mode = req.arg<uint8_t>(0) != 0;
            return 0;
        });
        s.add_cmd(52, "SetLcdBacklighOffEnabled", ignored("SetLcdBacklighOffEnabled"));
        s.add_cmd(53, "BeginVrModeEx", [](Request&, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().vr_mode = true;
            return 0;
        });
        s.add_cmd(54, "EndVrModeEx", [](Request&, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().vr_mode = false;
            return 0;
        });
        s.add_cmd(55, "IsInControllerFirmwareUpdateSection", ignored("IsInControllerFirmwareUpdateSection", 1));
        s.add_cmd(59, "SetVrPositionForDebug", ignored("SetVrPositionForDebug"));
        s.add_cmd(60, "GetDefaultDisplayResolution", [](Request&, Response& rsp) -> uint32_t {
            bool docked;
            {
                std::lock_guard<std::mutex> lk(mu());
                docked = st().docked;
            }
            uint32_t w = 0, h = 0;
            default_resolution(docked, &w, &h);
            rsp.push<int32_t>(static_cast<int32_t>(w));
            rsp.push<int32_t>(static_cast<int32_t>(h));
            return 0;
        });
        s.add_cmd(61, "GetDefaultDisplayResolutionChangeEvent", [](Request&, Response& rsp) -> uint32_t {
            rsp.out_copy.push_back(never_event().new_handle());
            return 0;
        });
        s.add_cmd(62, "GetHdcpAuthenticationState", ignored("GetHdcpAuthenticationState", 4));
        s.add_cmd(63, "GetHdcpAuthenticationStateChangeEvent", [](Request&, Response& rsp) -> uint32_t {
            rsp.out_copy.push_back(never_event().new_handle());
            return 0;
        });
        s.add_cmd(66, "SetCpuBoostMode", ignored("SetCpuBoostMode"));
        s.add_cmd(67, "CancelCpuBoostMode", ignored("CancelCpuBoostMode"));
        s.add_cmd(68, "GetBuiltInDisplayType", ignored("GetBuiltInDisplayType", 4));
        // 21.0.0+: the start-up logo is gone at once (there is none here), so the message is queued when it is asked for.
        s.add_cmd(130, "EnableStartupLogoDisappearedMessage", [](Request&, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            queue_message(kMsgStartupLogoDisappeared);
            return 0;
        });
        s.add_cmd(300, "GetSettingsPlatformRegion", [](Request&, Response& rsp) -> uint32_t {
            rsp.push<uint32_t>(1);   // Global, as ScarletNX answers
            return 0;
        });
        s.add_cmd(900, "SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled",
                  ignored("SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled"));
        s.add_cmd(1000, "BeginVrMode3d", [](Request&, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().vr_mode_3d = true;
            return 0;
        });
        s.add_cmd(1001, "EndVrMode3d", [](Request&, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().vr_mode_3d = false;
            return 0;
        });
        s.add_cmd(1002, "IsVrModeEnabled3d", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint8_t>(st().vr_mode_3d ? 1 : 0);
            return 0;
        });
    });
}

// ISelfController: exit, the notification flags (real state), the display layers, the idle and sleep settings.
ServicePtr make_self_controller() {
    return make_iface("ISelfController", [](Service& s) {
        // Switch 2 ([S2], switchbrew): 2000 GetGraphicsSupervisor hands out a TIPC session (a move handle, also from a domain);
        // 2100-2402 switch recording / sharing / copyright layers and audio sharing on or off, which a PC run has nothing to do for.
        s.add_cmd(2000, "GetGraphicsSupervisor", [](Request&, Response& rsp) -> uint32_t {
            const uint32_t h = ipc::create_session_handle(std::make_shared<ipc::StubService>("GraphicsSupervisor"));
            if (!h) return kernel::kStubService;
            rsp.out_move.push_back(h);
            return 0;
        });
        for (uint32_t id : {2100u, 2200u, 2300u, 2400u, 2401u, 2402u})
            s.add_cmd(id, "SetLayerOrAudioSharingEnabled (S2)", [](Request&, Response&) -> uint32_t { return 0; });
        // Exit: the application asks to end. The run is ended by the graphics shell (src/gfx), which this library does not link, so
        // here the request is logged and answered with success only.
        s.add_cmd(0, "Exit", [](Request&, Response&) -> uint32_t {
            NXLOG_W("am", "Exit requested by the application: nothing ends the run here (the shell is not linked into the services)");
            return 0;
        });
        s.add_cmd(1, "LockExit", ignored("LockExit"));
        s.add_cmd(2, "UnlockExit", ignored("UnlockExit"));
        s.add_cmd(3, "EnterFatalSection", ignored("EnterFatalSection"));
        s.add_cmd(4, "LeaveFatalSection", ignored("LeaveFatalSection"));
        s.add_cmd(9, "GetLibraryAppletLaunchableEvent", [](Request&, Response& rsp) -> uint32_t {
            rsp.out_copy.push_back(st().launchable_event.new_handle());
            return 0;
        });
        s.add_cmd(10, "SetScreenShotPermission", ignored("SetScreenShotPermission"));
        s.add_cmd(11, "SetOperationModeChangedNotification", [](Request& req, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().operation_notify = req.arg<uint8_t>(0) != 0;
            return 0;
        });
        s.add_cmd(12, "SetPerformanceModeChangedNotification", [](Request& req, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().performance_notify = req.arg<uint8_t>(0) != 0;
            return 0;
        });
        s.add_cmd(13, "SetFocusHandlingMode", ignored("SetFocusHandlingMode"));
        s.add_cmd(14, "SetRestartMessageEnabled", ignored("SetRestartMessageEnabled"));
        s.add_cmd(16, "SetOutOfFocusSuspendingEnabled", ignored("SetOutOfFocusSuspendingEnabled"));
        s.add_cmd(40, "CreateManagedDisplayLayer", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint64_t>(st().next_layer++);
            return 0;
        });
        s.add_cmd(41, "IsSystemBufferSharingEnabled", ignored("IsSystemBufferSharingEnabled"));
        s.add_cmd(44, "CreateManagedDisplaySeparableLayer", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint64_t>(st().next_layer++);
            rsp.push<uint64_t>(st().next_layer++);
            return 0;
        });
        s.add_cmd(62, "SetIdleTimeDetectionExtension", [](Request& req, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().idle_extension = req.arg<uint32_t>(0);
            return 0;
        });
        s.add_cmd(63, "GetIdleTimeDetectionExtension", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint32_t>(st().idle_extension);
            return 0;
        });
        s.add_cmd(68, "SetAutoSleepDisabled", [](Request& req, Response&) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            st().auto_sleep_disabled = req.arg<uint8_t>(0) != 0;
            return 0;
        });
        s.add_cmd(69, "IsAutoSleepDisabled", [](Request&, Response& rsp) -> uint32_t {
            std::lock_guard<std::mutex> lk(mu());
            rsp.push<uint8_t>(st().auto_sleep_disabled ? 1 : 0);
            return 0;
        });
        s.add_cmd(90, "GetAccumulatedSuspendedTickValue", ignored("GetAccumulatedSuspendedTickValue", 8));
        s.add_cmd(91, "GetAccumulatedSuspendedTickChangedEvent", [](Request&, Response& rsp) -> uint32_t {
            rsp.out_copy.push_back(st().tick_event.new_handle());
            return 0;
        });
        s.add_cmd(100, "SetAlbumImageTakenNotificationEnabled", ignored("SetAlbumImageTakenNotificationEnabled"));
        s.add_cmd(110, "SetApplicationAlbumUserData", ignored("SetApplicationAlbumUserData"));
        s.add_cmd(120, "SaveCurrentScreenshot", ignored("SaveCurrentScreenshot"));
    });
}

// IWindowController: the applet resource user id of the one application (the value Wonder answers).
ServicePtr make_window_controller() {
    return make_iface("IWindowController", [](Service& s) {
        s.add_cmd(1, "GetAppletResourceUserId", [](Request&, Response& rsp) -> uint32_t {
            rsp.push<uint64_t>(kAppletResourceUserId);
            return 0;
        });
        s.add_cmd(10, "AcquireForegroundRights", ignored("AcquireForegroundRights"));
        s.add_cmd(11, "ReleaseForegroundRights", ignored("ReleaseForegroundRights"));
        s.add_cmd(12, "RejectToChangeIntoBackground", ignored("RejectToChangeIntoBackground"));
    });
}


ServicePtr make_library_applet_creator() {
    return make_iface("ILibraryAppletCreator", [](Service& s) {
        s.add_cmd(0, "CreateLibraryApplet", [](Request&, Response&) -> uint32_t { return kLibraryAppletFailed; });
        s.add_cmd(10, "CreateStorage", [](Request& req, Response& rsp) -> uint32_t {
            const int64_t size = req.arg<int64_t>(0);
            if (!storage_size_ok(size)) return kOutOfMemory;
            rsp.push_object(new_storage(std::make_shared<Bytes>(static_cast<size_t>(size), 0)));
            return 0;
        });
    });
}
ServicePtr make_application_functions() {
    return make_iface("IApplicationFunctions", [](Service& s) {
        s.add_cmd(21, "GetDesiredLanguage", [](Request&, Response& rsp) -> uint32_t {
            const auto selected=config_get_str("system","language","AmericanEnglish");
            const char* code="en-US";
            for(const auto [name, value] : {std::pair{"Japanese","ja"},{"AmericanEnglish","en-US"},{"French","fr"},{"German","de"},
                    {"Italian","it"},{"Spanish","es"},{"Chinese","zh-CN"},{"Korean","ko"},{"Dutch","nl"},{"Portuguese","pt"},
                    {"Russian","ru"},{"Taiwanese","zh-TW"},{"BritishEnglish","en-GB"},{"CanadianFrench","fr-CA"},
                    {"LatinAmericanSpanish","es-419"},{"SimplifiedChinese","zh-Hans"},{"TraditionalChinese","zh-Hant"},{"BrazilianPortuguese","pt-BR"}})
                if(selected==name || selected==value) { code=value;break; }
            uint64_t language=0;std::memcpy(&language,code,std::strlen(code));rsp.push(language);return 0;
        });
        s.add_cmd(23, "GetDisplayVersion", [](Request&, Response& rsp) -> uint32_t {
            const std::string version = nxrt::config_get_str("title", "display_version", "131072");
            push_bytes(rsp, version.data(), version.size(), 16); return 0;
        });
        s.add_cmd(40, "NotifyRunning", [](Request&, Response& rsp) -> uint32_t { rsp.push<uint8_t>(1); return 0; });
        s.add_cmd(123, "GetPreviousProgramIndex", [](Request&, Response& rsp) -> uint32_t { rsp.push<int32_t>(-1); return 0; });
        for (const auto [id, name] : {std::pair{37u, "GetLimitedApplicationLicenseUpgradableEvent"},
                {130u, "GetGpuErrorDetectedSystemEvent"}, {140u, "GetFriendInvitationStorageChannelEvent"},
                {150u, "GetNotificationStorageChannelEvent"}, {160u, "GetHealthWarningDisappearedSystemEvent"},
                {210u, "GetUnknownEvent210"}}) {
            s.add_cmd(id, name, [](Request&, Response& rsp) -> uint32_t {
                rsp.out_copy.push_back(never_event().new_handle()); return 0;
            });
        }
        s.add_cmd(141, "TryPopFromFriendInvitationStorageChannel", [](Request&, Response&) -> uint32_t { return res(128, 2); });
        s.add_cmd(151, "TryPopFromNotificationStorageChannel", [](Request&, Response&) -> uint32_t { return res(128, 2); });
        s.add_cmd(22, "SetTerminateResult", ignored("SetTerminateResult"));
        s.add_cmd(320, "Cmd320", ignored("IApplicationFunctions Cmd320", 2));
        s.add_cmd(321, "Cmd321", ignored("IApplicationFunctions Cmd321", 1));

    });
}
ServicePtr make_application_proxy() {
    return make_iface("IApplicationProxy", [](Service& s) {
        s.add_cmd(0, "GetCommonStateGetter", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_common_state_getter());
            return 0;
        });
        s.add_cmd(1, "GetSelfController", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_self_controller());
            return 0;
        });
        s.add_cmd(2, "GetWindowController", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_window_controller());
            return 0;
        });
        s.add_cmd(3, "GetAudioController", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_audio_controller());
            return 0;
        });
        s.add_cmd(4, "GetDisplayController", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_iface("IDisplayController", [](Service&) {}));
            return 0;
        });
        // Switch 2 (switchbrew [S2]): 40 IPerformanceFunctions, 60 IGraphicsScalingParametersFunctions. Commands unknown so far:
        // stub objects that log what the game asks of them.
        s.add_cmd(40, "GetPerformanceFunctions", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(std::make_shared<ipc::StubService>("IPerformanceFunctions"));
            return 0;
        });
        s.add_cmd(60, "GetGraphicsScalingParametersFunctions", [](Request&, Response& rsp) -> uint32_t {
            // nn::oe::GetCurrentGraphicsScale (sdk) maps the first word of cmd 10 to a scale level: 10 -> 0, 20 -> 1, 30 -> 2 (anything else
            // aborts). Level 0 is the console's full-quality one: Pokopia's dynamic resolution (main 0x710022d338 / 0x710022cd20) tops out
            // at its level 30 (1920x1080) at scale 0 docked, 24 (1728x972) at scale 1 and 22 (1664x936) at scale 2. [am] graphics_scale
            // (10, 20 or 30) picks another answer.
            rsp.push_object(make_iface("IGraphicsScalingParametersFunctions", [](Service& g) {
                g.add_cmd(10, "GetGraphicsScalingParameters", [](Request&, Response& r) -> uint32_t {
                    static const uint32_t answer = [] {
                        const int64_t v = nxrt::config_get_int("am", "graphics_scale", 10);
                        return (v == 20 || v == 30) ? static_cast<uint32_t>(v) : 10u;
                    }();
                    r.push<uint32_t>(answer);
                    r.push<uint32_t>(0);
                    r.push<uint64_t>(0);
                    return 0;
                });
                g.add_cmd(1050, "SetGraphicsScalingNotification (S2)", [](Request&, Response&) -> uint32_t { return 0; });
            }));
            return 0;
        });
        s.add_cmd(10, "GetProcessWindingController", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_iface("IProcessWindingController", [](Service&) {}));
            return 0;
        });
        s.add_cmd(11, "GetLibraryAppletCreator", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_library_applet_creator());
            return 0;
        });
        s.add_cmd(20, "GetApplicationFunctions", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_application_functions());
            return 0;
        });
        s.add_cmd(1000, "GetDebugFunctions", [](Request&, Response& rsp) -> uint32_t {
            rsp.push_object(make_iface("IDebugFunctions", [](Service&) {}));
            return 0;
        });
    });
}

// appletOE: IApplicationProxyService, OpenApplicationProxy(pid, process copy). appletAE: IAllSystemAppletProxiesService, whose
// OpenSystemApplicationProxy (350) answers the same proxy.
ServicePtr make_applet_service(const char* name, bool is_system) {
    return make_iface(name, [is_system](Service& s) {
        const uint32_t open_cmd = is_system ? 350u : 0u;
        s.add_cmd(open_cmd, is_system ? "OpenSystemApplicationProxy" : "OpenApplicationProxy",
                  [](Request& req, Response& rsp) -> uint32_t {
                      close_copies(req);   // the process handle is not kept
                      rsp.push_object(make_application_proxy());
                      return 0;
                  });
        if (!is_system)   // Switch 2 SDKs open the proxy with OpenApplicationProxy2 (10); same answer
            s.add_cmd(10, "OpenApplicationProxy2", [](Request& req, Response& rsp) -> uint32_t {
                close_copies(req);
                rsp.push_object(make_application_proxy());
                return 0;
            });
    });
}

}  // namespace

void register_am() {
    add("appletOE", [] { return make_applet_service("appletOE", false); });
    add("appletAE", [] { return make_applet_service("appletAE", true); });
}

void am_reset() {
    std::lock_guard<std::mutex> lk(mu());
    st() = fresh_state();
}

namespace amctl {

void set_docked(bool docked) {
    std::lock_guard<std::mutex> lk(mu());
    st().docked = docked;
    if (st().operation_notify) queue_message(kMsgOperationModeChanged);
    if (st().performance_notify) queue_message(kMsgPerformanceModeChanged);
}

void set_focus(bool in_focus) {
    host_focus = in_focus;
    std::lock_guard<std::mutex> lk(mu());
    st().focus = in_focus ? kFocusInFocus : kFocusOutOfFocus;
    queue_message(kMsgFocusStateChanged);
}

bool docked() {
    std::lock_guard<std::mutex> lk(mu());
    return st().docked;
}

bool in_focus() {
    std::lock_guard<std::mutex> lk(mu());
    return st().focus == kFocusInFocus;
}

}  // namespace amctl

}  // namespace nxrt::services
