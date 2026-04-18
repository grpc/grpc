#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/grpc_check.h"
#include <iostream>
#include <limits>

using namespace grpc_core;
using namespace grpc_core::chttp2;

// Mock enough of the transport to test the parser logic if needed, 
// but for a PoC we can just show the flow_control objects.

int main() {
    ExecCtx exec_ctx;
    MemoryOwner memory_owner = MemoryOwner(
        ResourceQuota::Default()->memory_quota()->CreateMemoryOwner());
    TransportFlowControl tfc("test", true, &memory_owner);
    
    std::cout << "Initial remote window: " << tfc.remote_window() << std::endl;
    
    // According to RFC 7540 6.9.1, max window is 2^31 - 1
    int64_t max_window = 2147483647;
    
    // Simulate current state: window is already large
    uint32_t big_update = 2147000000;
    {
        TransportFlowControl::OutgoingUpdateContext upd(&tfc);
        upd.RecvUpdate(big_update);
    }
    std::cout << "Remote window after large update: " << tfc.remote_window() << std::endl;

    // Now send another update that pushes it over the limit
    uint32_t overflow_update = 1000000;
    
    // CURRENT BEHAVIOR: No check in RecvUpdate
    {
        TransportFlowControl::OutgoingUpdateContext upd(&tfc);
        upd.RecvUpdate(overflow_update);
    }
    
    int64_t final_window = tfc.remote_window();
    std::cout << "Final window: " << final_window << std::endl;
    
    if (final_window > max_window) {
        std::cout << "POC SUCCESS: Window overflowed to " << final_window 
                  << " (Limit: " << max_window << ")" << std::endl;
        std::cout << "This violates RFC 7540 Section 6.9.1." << std::endl;
    } else {
        std::cout << "POC FAILED: Window was clamped or rejected." << std::endl;
    }
    
    return 0;
}
