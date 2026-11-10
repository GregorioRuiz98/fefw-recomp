// SPDX-License-Identifier: GPL-3.0-or-later
// ABI 3 call/import portion of nxrt/core/abi_glue.cpp. Kernel/EH ABI functions
// deliberately remain undefined until their real subsystems have been ported.
#include "nxrt/dispatch.h"
#include "nxrt/nxmodule.h"
#include <cstdio>

extern "C" {
void call_indirect(Ctx* context, uint64_t address) { nxrt::dispatch_call(context, address); }
void guest_trap(const char* what, uint64_t address) { nxrt::trap(what, address); }
void host_import(Ctx* context, uint32_t id) {
    const uint64_t target = nxrt::import_resolve(id);
    if (!target) {
        char reason[96];
        std::snprintf(reason, sizeof reason, "Unknown import id: module %u, symbol %u",
                      NX_IMPORT_MODULE(id), NX_IMPORT_SYM(id));
        nxrt::trap(reason, id);
    }
    nxrt::detail::count_import();
    struct RestoreImport {
        Ctx* context;
        const char* name;
        uint64_t from;
        ~RestoreImport() { context->in_import = name; context->import_from = from; }
    } restore{context, context->in_import, context->import_from};
    context->in_import = nxrt::import_name(id);
    context->import_from = context->x[30] - 4;
    nxrt::dispatch_call(context, target);
}
}  // extern "C"
