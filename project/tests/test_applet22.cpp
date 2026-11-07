#include "svc_test_util.h"
#include "nxrt/services_api.h"

NXTEST_GROUP(applet22)
NXTEST(auxiliary_event_has_own_identity_and_can_be_closed) {
    nxsvc::Env env;
    auto oe = nxsvc::connect("appletOE");
    REQUIRE(oe.valid());
    nxrt::services::ServiceEvent process;
    auto proxy = oe.sub(oe.call(0, nxsvc::Args().with_pid().hcopy(process.new_handle())));
    REQUIRE(proxy.valid());
    auto creator = proxy.sub(proxy.call(11));
    REQUIRE(creator.valid());
    auto applet = creator.sub(creator.call(3, nxsvc::Args().u32(0x100000c).u32(0)));
    REQUIRE(applet.valid());
    auto normal = applet.call(0), extra = applet.call(170);
    REQUIRE(normal.result == 0 && !normal.copy.empty());
    REQUIRE(extra.result == 0 && !extra.copy.empty());
    auto n = nxrt::kernel::handle_as<nxrt::kernel::KEvent>(normal.copy[0]);
    auto e = nxrt::kernel::handle_as<nxrt::kernel::KEvent>(extra.copy[0]);
    REQUIRE(n && e);
    CHECK(n->state != e->state);
    CHECK(!e->state->signaled);
    nxrt::kernel::handle_close(extra.copy[0]);
    CHECK(nxrt::kernel::handle_as<nxrt::kernel::KEvent>(extra.copy[0]) == nullptr);
}
