/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE_CONFIG

#include "src/core/lib/iomgr/network_monitor.h"

#include <SystemConfiguration/SystemConfiguration.h>
#include <CoreFoundation/CoreFoundation.h>
#include <grpc/support/sync.h>

struct grpc_connectivity_monitor {
  gpr_mu mu;
  dispatch_queue_t dispatch_queue;
  SCNetworkReachabilityRef reachability_ref;
  void (*loss_connection_handler)(void);
};

static struct grpc_connectivity_monitor g_monitor;
static gpr_once g_monitor_mu_once = GPR_ONCE_INIT;

static bool is_host_reachable(SCNetworkReachabilityFlags flags) {
  return !!(flags & kSCNetworkReachabilityFlagsReachable) &&
         !(flags & kSCNetworkReachabilityFlagsInterventionRequired) &&
         !(flags & kSCNetworkReachabilityFlagsConnectionOnDemand);
}

static void reachability_callback(SCNetworkReachabilityRef target,
                                  SCNetworkReachabilityFlags flags,
                                  void* info) {
  struct grpc_connectivity_monitor* monitor =
      (struct grpc_connectivity_monitor*)info;
  if (!is_host_reachable(flags)) {
    monitor->loss_connection_handler();
  }
}

static bool is_monitor_initialized(struct grpc_connectivity_monitor* monitor) {
  return monitor != NULL && monitor->reachability_ref != NULL &&
         monitor->dispatch_queue != NULL &&
         monitor->loss_connection_handler != NULL;
}

static bool start_monitor(struct grpc_connectivity_monitor* monitor) {
  SCNetworkReachabilityContext context = {
      .version = 0, .info = (void*)monitor,
  };
  return is_monitor_initialized(monitor) &&
         SCNetworkReachabilitySetCallback(monitor->reachability_ref,
                                          reachability_callback, &context) &&
         SCNetworkReachabilitySetDispatchQueue(monitor->reachability_ref,
                                               monitor->dispatch_queue);
}

static bool stop_monitor(struct grpc_connectivity_monitor* monitor) {
  return is_monitor_initialized(monitor) &&
         SCNetworkReachabilitySetCallback(monitor->reachability_ref, NULL,
                                          NULL) &&
         SCNetworkReachabilitySetDispatchQueue(monitor->reachability_ref, NULL);
}

static bool init_connectivity_monitor(struct grpc_connectivity_monitor* monitor,
                                      const char* addr, void (*handler)(void)) {
  // Check if monitor has already been initialized.
  if (monitor->reachability_ref != NULL) {
    return false;
  }
  if (!(monitor->dispatch_queue = dispatch_get_main_queue())) {
    return false;
  }
  if (!(monitor->reachability_ref =
            SCNetworkReachabilityCreateWithName(NULL, addr))) {
    monitor->dispatch_queue = NULL;
    return false;
  }
  monitor->loss_connection_handler = handler;
  return true;
}

static void clear_connectivity_monitor(
    struct grpc_connectivity_monitor* monitor) {
  if (monitor->reachability_ref != NULL) {
    CFRelease(monitor->reachability_ref);
  }
  monitor->reachability_ref = NULL;
  monitor->dispatch_queue = NULL;
  monitor->loss_connection_handler = NULL;
}

static void connectivity_monitor_mu_init(void) {
  gpr_mu_init(&g_monitor.mu);
  clear_connectivity_monitor(&g_monitor);
}

bool grpc_start_connectivity_monitor(const char* addr, void (*handler)(void)) {
  gpr_once_init(&g_monitor_mu_once, &connectivity_monitor_mu_init);
  gpr_mu_lock(&g_monitor.mu);
  init_connectivity_monitor(&g_monitor, addr, handler);
  // TODO(zyc): Should we check connectivity at the beginning
  // SCNetworkReachabilityFlags flags;
  // if (SCNetworkReachabilityGetFlags(g_monitor.reachability_ref, &flags)) {
  //   reachability_callback(g_monitor.reachability_ref, flags, &monitor);
  // }
  if (!start_monitor(&g_monitor)) {
    clear_connectivity_monitor(&g_monitor);
    gpr_mu_unlock(&g_monitor.mu);
    return false;
  }
  gpr_mu_unlock(&g_monitor.mu);
  return true;
}

bool grpc_stop_connectivity_monitor(void) {
  gpr_mu_lock(&g_monitor.mu);
  if (!stop_monitor(&g_monitor)) {
    gpr_mu_unlock(&g_monitor.mu);
    return false;
  }
  clear_connectivity_monitor(&g_monitor);
  gpr_mu_unlock(&g_monitor.mu);
  return true;
}

#endif /* GPR_APPLE_CONFIG */
