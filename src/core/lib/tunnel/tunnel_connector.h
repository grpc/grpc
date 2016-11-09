/*
 * tunnel_connector.h
 *
 *  Created on: Oct 29, 2016
 *      Author: gnirodi
 */

#ifndef SRC_CORE_LIB_TUNNEL_TUNNEL_CONNECTOR_H_
#define SRC_CORE_LIB_TUNNEL_TUNNEL_CONNECTOR_H_

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/tunnel/tunnel.h"

grpc_channel *tunnel_channel_create(const char *target,
                                    const grpc_channel_args *args,
                                    void *reserved,
                                    grpc_tunnel *tunnel);

#endif /* SRC_CORE_LIB_TUNNEL_TUNNEL_CONNECTOR_H_ */
