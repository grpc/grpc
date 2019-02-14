/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_H

#include <grpc/support/port_platform.h>
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/lib/avl/avl.h"

/** Channel arg indicating if a target corresponding to the address is grpclb
 * loadbalancer. The type of this arg is an integer and the value is treated as
 * a bool. */
#define GRPC_ARG_ADDRESS_IS_XDS_LOAD_BALANCER \
  "grpc.address_is_xds_load_balancer"
/** Channel arg indicating if a target corresponding to the address is a backend
 * received from a balancer. The type of this arg is an integer and the value is
 * treated as a bool. */
#define GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER \
  "grpc.address_is_backend_from_xds_load_balancer"

class LocalityMap {
 public:
  LocalityMap();
  ~LocalityMap();

  // Adds or updates a locality for the given locality name
  void CreateOrUpdateLocality(
      char* locality_name, grpc_channel_args* channel_args,
      grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> child_policy);
  // Retreives the child policy from the locality map. NOTE : Being an
  // Orphanable pointer the lb policy is dereffed from the LocalityEntry
  // and would either require calling SetChildPolicy to ref it back
  // or CreateOrUpdateLocality to store it in a new entry
  grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> RetrieveChildPolicy(
      char* locality_name);
  bool SetChildPolicy(
      char* locality_name,
      grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> child_policy);
  ::grpc_channel_args* GetGrpcChannelArgs(char* locality_name);

 private:
  grpc_avl map_;
  gpr_mu mu_;
  static void destroy_locality_name(void* p, void* unused);
  static void* copy_locality_name(void* key, void* unused);
  static void destroy_locality_entry(void* p, void* unused);
  static void* copy_locality_entry(void* entry, void* unused);
  static long compare_locality_name(void* key1, void* key2, void* unused);
  const grpc_avl_vtable locality_avl_vtable = {
      destroy_locality_name, copy_locality_name, compare_locality_name,
      destroy_locality_entry, copy_locality_entry};
  class LocalityEntry {
   public:
    LocalityEntry() {}
    grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> child_policy_;
    ::grpc_channel_args* channel_args_;
  };
};

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_H \
        */
