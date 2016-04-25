
#include "src/core/lib/iomgr/endpoint.h"

void network_status_register_endpoint(grpc_endpoint *ep);
void network_status_shutdown_all_endpoints();