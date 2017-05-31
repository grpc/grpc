/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_INTERNAL_CORE_IOMGR_REACHABILITY_H
#define GRPC_INTERNAL_CORE_IOMGR_REACHABILITY_H

// Reachability (as used here) is defined as being the existance of a network
// route to a specified address. It does not indicate whether a server is
// serving traffic at that address.

/// Network type: a broad classification of different kinds of network
/// Useful for systems that want to send traffic only on a restricted class
typedef enum grpc_network_type {
  /// Network type is unknown
  GRPC_NETWORK_UNKNOWN_TYPE,
  /// Network is a wired connection
  GRPC_NETWORK_WIRED,
  /// Network is a wifi connection
  GRPC_NETWORK_WIFI,
  /// Network is a cellular connection
  GRPC_NETWORK_CELLULAR
} grpc_network_type;

/// A description of a nic
typedef struct grpc_network_interface {
  /// Type of the network this nic refers to
  grpc_network_type network_type;
  /// Address to pass to bind() to select this nic
  const sockaddr_t *bind_addr;
  size_t bind_addr_len;
  /// A name a sysadmin could use to identify this nic
  const char *name;
} grpc_network_interface;

/// Copy a network interface description
grpc_network_interface *grpc_network_interface_copy(
    grpc_network_interface *src);
/// Destroy a network interface description
void grpc_network_interface_destroy(grpc_network_interface *nic);
/// Enumerate all network interfaces on this system
void grpc_enumerate_network_interfaces(grpc_network_interface **nics,
                                       size_t *nic_count);

/// One time global initialization
void grpc_reachability_init(void);
/// Global shutdown - cancels all outstanding watches
void grpc_reachability_shutdown(void);

/// Watch \a addr for reachability.
/// If \a addr == NULL, watch for any network being available.
/// When \a addr becomes reachable, call \a on_reachable with success=true
/// \a optionally_only_on_nic allows reachability to be awaited only via a
/// specific network interface
void grpc_reachability_await_reachable(
    const sockaddr_t *addr, size_t addr_len,
    const grpc_network_interface *optionally_only_on_nic,
    grpc_closure *on_reachable);
/// Watch \a addr for reachability.
/// If \a addr == NULL, watch for all networks being unavailable.
/// When \a addr becomes unreachable, call \a on_unreachable with success=true
/// \a optionally_only_on_nic allows reachability to be awaited only via a
/// specific network interface
void grpc_reachability_await_unreachable(
    const sockaddr_t *addr, size_t addr_len,
    const grpc_network_interface *optionally_only_on_nic,
    grpc_closure *on_unreachable);

/// Query (instantaneous) reachability of \a addr
/// (optionally) on \a optionally_only_on_nic (if it is non-NULL)
/// Return true if \a is (probably) reachable, false if it is not
/// If optional_bind_nic is non-NULL, AND this function returns true,
/// set *optional_bind_nic to the network interface that must be passed to
/// bind() to route to this address - or NULL if no such call is required.
bool grpc_reachability_query(
    const sockaddr_t *addr, size_t addr_len,
    const grpc_network_interface *optionally_only_on_nic,
    const grpc_network_interface **optional_bind_nic);

/// Cancel a previously queued watch: the callback will be called with
/// success=false (if it has not already been scheduled)
void grpc_reachability_cancel_await_reachable(grpc_closure *on_reachable);
void grpc_reachability_cancel_await_unreachable(grpc_closure *on_reachable);

#endif  // GRPC_INTERNAL_CORE_IOMGR_REACHABILITY_H
