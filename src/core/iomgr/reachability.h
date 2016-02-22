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

/// Watch addr for reachability.
/// If addr==NULL, watch for any nic being available.
/// When reachability changes from *reachable, call on_reachability_changed
/// with success=true, and *reachable set to the current reachability, and
/// *out_target_nic set to NULL or a specific nic on which the address is
/// reachable.
/// Ownership of *out_target_nic is transferred to the on_reachability_changed
/// callback.
void grpc_reachability_watch(
    const sockaddr_t *addr, size_t addr_len,
    const grpc_network_interface *optionally_only_on_nic, bool *reachable,
    const grpc_network_interface **out_target_nic,
    grpc_closure *on_reachability_changed);

/// Cancel a previously queued watch: the callback will be called with
/// success=false (if it has not already been scheduled)
void grpc_reachability_cancel_watch(grpc_closure *on_reachability_changed);

#endif  // GRPC_INTERNAL_CORE_IOMGR_REACHABILITY_H
