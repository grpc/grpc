/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX

#include <arpa/inet.h>
#include <asm/types.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <memory>
#include <random>
#include <sstream>
#include <thread>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/env.h"

namespace grpc_core {

namespace testing {

namespace {

const char* kDummyDeviceName = "dummy0";
const char* kDummyDeviceType = "dummy";
gpr_once g_blackhole_ipv6_discard_prefix = GPR_ONCE_INIT;
gpr_once g_ensure_ipv6_discard_prefix_is_blackholed = GPR_ONCE_INIT;

void SendNetlinkMessageAndWaitForAck(struct nlmsghdr* netlink_message_header,
                                     const std::string& reason) {
  // create a netlink socket
  int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  struct sockaddr_nl local_netlink_addr;
  memset(&local_netlink_addr, 0, sizeof(local_netlink_addr));
  local_netlink_addr.nl_family = AF_NETLINK;
  local_netlink_addr.nl_pid = getpid();
  {
    if (bind(fd, (struct sockaddr*)&local_netlink_addr,
             sizeof(local_netlink_addr)) == -1) {
      gpr_log(GPR_ERROR, "got error:|%s| binding netlink socket",
              strerror(errno));
      abort();
    }
  }
  struct iovec iov;
  iov.iov_base = reinterpret_cast<void*>(netlink_message_header);
  iov.iov_len = netlink_message_header->nlmsg_len;
  struct sockaddr_nl kernel_netlink_addr;
  memset(&kernel_netlink_addr, 0, sizeof(kernel_netlink_addr));
  kernel_netlink_addr.nl_family = AF_NETLINK;
  struct msghdr outer_message_header;
  memset(&outer_message_header, 0, sizeof(outer_message_header));
  outer_message_header.msg_name = &kernel_netlink_addr;
  outer_message_header.msg_namelen = sizeof(kernel_netlink_addr);
  outer_message_header.msg_iov = &iov;
  outer_message_header.msg_iovlen = 1;
  if (sendmsg(fd, &outer_message_header, 0) == -1) {
    gpr_log(GPR_ERROR,
            "got error:|%s| sending netlink message. message reason: %s",
            strerror(errno), reason.c_str());
    abort();
  }
  char recv_buf[2048];
  if (recv(fd, recv_buf, sizeof(recv_buf), 0) == -1) {
    gpr_log(GPR_ERROR,
            "got error:|%s| recving netlink message. message reason: %s",
            strerror(errno), reason.c_str());
    abort();
  }
  struct nlmsghdr* response = reinterpret_cast<struct nlmsghdr*>(recv_buf);
  if (response->nlmsg_type != NLMSG_ERROR) {
    gpr_log(
        GPR_ERROR,
        "expected response type of NLMSG_ERROR but got:%d. message reason: %s",
        response->nlmsg_type, reason.c_str());
    abort();
  }
  struct nlmsgerr* error_msg =
      static_cast<struct nlmsgerr*>(NLMSG_DATA(response));
  gpr_log(GPR_INFO,
          "received NLMSG_ERROR error:%d error str:|%s|. message reason: %s",
          -error_msg->error, strerror(-error_msg->error), reason.c_str());
  GPR_ASSERT(error_msg->error == 0);
  close(fd);
  gpr_free(netlink_message_header);
}

void DumpNetworkInterfacesState() {
  const std::vector<std::string> files = {
      "/proc/net/if_inet6",
      "/proc/net/ipv6_route",
  };
  for (const auto& file : files) {
    gpr_log(GPR_INFO, "Begin contents of %s", file.c_str());
    system(("cat " + file).c_str());
    gpr_log(GPR_INFO, "End contents of %s", file.c_str());
  }
}

void BlackHoleIPv6DiscardPrefix() {
  DumpNetworkInterfacesState();
  gpr_log(GPR_INFO,
          "attempting to create a new dummy network interface named dummy0");
  {
    const int dummy_device_name_attribute_size =
        RTA_LENGTH(strlen(kDummyDeviceName));
    const int dummy_device_type_attribute_size =
        RTA_LENGTH(strlen(kDummyDeviceType));
    // the dummy_device_type_attribute is nested within ifla_linkinfo
    const int linkinfo_attribute_size =
        RTA_SPACE(0) + dummy_device_type_attribute_size;
    // pack the message together
    const int attribute_buffer_size =
        RTA_ALIGN(dummy_device_name_attribute_size) +
        RTA_ALIGN(linkinfo_attribute_size);
    const int netlink_message_body_size = sizeof(struct ifinfomsg);
    const int total_message_size =
        NLMSG_SPACE(netlink_message_body_size) + attribute_buffer_size;
    struct nlmsghdr* const create_dummy_device_header =
        static_cast<struct nlmsghdr*>(gpr_zalloc(total_message_size));
    create_dummy_device_header->nlmsg_len = total_message_size;
    create_dummy_device_header->nlmsg_type = RTM_NEWLINK;
    create_dummy_device_header->nlmsg_flags =
        NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
    struct ifinfomsg* const create_dummy_device_body =
        static_cast<struct ifinfomsg*>(NLMSG_DATA(create_dummy_device_header));
    create_dummy_device_body->ifi_change = 0xFFFFFFFF;
    create_dummy_device_body->ifi_flags |= IFF_UP;
    create_dummy_device_body->ifi_flags |= IFF_NOARP;
    struct rtattr* const device_name_attribute =
        reinterpret_cast<struct rtattr*>(
            reinterpret_cast<char*>(create_dummy_device_header) +
            NLMSG_SPACE(netlink_message_body_size));
    int remaining_attributes_buffer = attribute_buffer_size;
    device_name_attribute->rta_type = IFLA_IFNAME;
    device_name_attribute->rta_len = dummy_device_name_attribute_size;
    memcpy(RTA_DATA(device_name_attribute), kDummyDeviceName,
           strlen(kDummyDeviceName));
    struct rtattr* const link_info_attribute =
        RTA_NEXT(device_name_attribute, remaining_attributes_buffer);
    link_info_attribute->rta_type = IFLA_LINKINFO;
    link_info_attribute->rta_len = linkinfo_attribute_size;
    struct rtattr* const device_type_attribute =
        static_cast<struct rtattr*>(RTA_DATA(link_info_attribute));
    device_type_attribute->rta_type = IFLA_INFO_KIND;
    device_type_attribute->rta_len = dummy_device_type_attribute_size;
    memcpy(RTA_DATA(device_type_attribute), kDummyDeviceType,
           strlen(kDummyDeviceType));
    RTA_NEXT(link_info_attribute, remaining_attributes_buffer);
    GPR_ASSERT(remaining_attributes_buffer == 0);
    SendNetlinkMessageAndWaitForAck(create_dummy_device_header,
                                    "create dummy0 interface of type dummy");
  }
  const uint32_t dummy_interface_index = if_nametoindex(kDummyDeviceName);
  GPR_ASSERT(dummy_interface_index > 0);
  gpr_log(GPR_INFO, "created dummy device named:%s. interface index:%d",
          kDummyDeviceName, dummy_interface_index);
  gpr_log(GPR_INFO,
          "attempting to route 100::/64 through the new dummy0 interface");
  {
    const int kDestinationAddressAttributeSize =
        RTA_LENGTH(16 /* number of bytes in an ipv6 address */);
    const int kOutputInterfaceIndexAttributeSize =
        RTA_LENGTH(sizeof(dummy_interface_index));
    const int attribute_buffer_size =
        RTA_ALIGN(kDestinationAddressAttributeSize) +
        RTA_ALIGN(kOutputInterfaceIndexAttributeSize);
    const int netlink_message_body_size = sizeof(struct rtmsg);
    // pack the message together
    const int total_message_size =
        NLMSG_SPACE(netlink_message_body_size) + attribute_buffer_size;
    struct nlmsghdr* create_route_header =
        static_cast<struct nlmsghdr*>(gpr_zalloc(total_message_size));
    create_route_header->nlmsg_type = RTM_NEWROUTE;
    create_route_header->nlmsg_flags =
        NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
    create_route_header->nlmsg_len = total_message_size;
    struct rtmsg* const create_route_body =
        static_cast<struct rtmsg*>(NLMSG_DATA(create_route_header));
    create_route_body->rtm_family = AF_INET6;
    create_route_body->rtm_scope = RT_SCOPE_NOWHERE;
    create_route_body->rtm_protocol = RTPROT_STATIC;
    create_route_body->rtm_type = RTN_UNICAST;
    create_route_body->rtm_table = RT_TABLE_MAIN;
    create_route_body->rtm_dst_len = 64;
    struct rtattr* const destination_address_attribute =
        reinterpret_cast<struct rtattr*>(
            reinterpret_cast<char*>(create_route_header) +
            NLMSG_SPACE(netlink_message_body_size));
    destination_address_attribute->rta_type = RTA_DST;
    destination_address_attribute->rta_len = kDestinationAddressAttributeSize;
    GPR_ASSERT(inet_pton(AF_INET6, "100::",
                         RTA_DATA(destination_address_attribute)) == 1);
    int remaining_attributes_buffer = attribute_buffer_size;
    struct rtattr* const output_interface_index_attribute =
        RTA_NEXT(destination_address_attribute, remaining_attributes_buffer);
    output_interface_index_attribute->rta_type = RTA_OIF;
    output_interface_index_attribute->rta_len =
        kOutputInterfaceIndexAttributeSize;
    memcpy(RTA_DATA(output_interface_index_attribute), &dummy_interface_index,
           sizeof(dummy_interface_index));
    RTA_NEXT(output_interface_index_attribute, remaining_attributes_buffer);
    GPR_ASSERT(remaining_attributes_buffer == 0);
    SendNetlinkMessageAndWaitForAck(create_route_header,
                                    "route 100::/64 through dummy0");
  }
  gpr_log(GPR_INFO, "routed 100::/64 through the dummy interface");
  DumpNetworkInterfacesState();
}

void EnsureIpv6DiscardPrefixIsBlackholed() {
  // test the blackhole, attempt to connect to a blackholed address and make
  // sure it hangs until the socket is shut down.
  {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    gpr_event socket_shutdown_ev;
    gpr_event_init(&socket_shutdown_ev);
    std::thread test_blackhole_thread([fd, &socket_shutdown_ev] {
      struct sockaddr_in6 addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin6_family = AF_INET6;
      addr.sin6_port = htons(443);
      GPR_ASSERT(inet_pton(AF_INET6, "100::1234", &addr.sin6_addr) == 1);
      if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) == 0) {
        gpr_log(GPR_ERROR,
                "connect succeeded to an address that was supposed to be "
                "blackholed");
        abort();
      }
      gpr_log(GPR_INFO, "connect to blackholed address failed with: %s",
              strerror(errno));
      // The connect call should only fail if the socket has been shutdown.
      // Note that this sanity check is racey because this could happen after
      // setting shutdown_ev but before calling shutdown, but this is unlikely,
      // and it's racey on the side of passing.
      GPR_ASSERT(gpr_event_get(&socket_shutdown_ev));
    });
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_seconds(1, GPR_TIMESPAN)));
    gpr_event_set(&socket_shutdown_ev, (void*)1);
    shutdown(fd, SHUT_RDWR);
    test_blackhole_thread.join();
    gpr_log(GPR_INFO, "[100::1234]:443 appears to be black holed, as intended");
    close(fd);
  }
}

}  // namespace

const std::string GetBlackHoledIPv6Address() {
  const char* key = "GRPC_TEST_LINUX_ONLY_BLACKHOLE_ADDRESS";
  std::string val;
  auto env = getenv(key);
  val = env == nullptr ? "" : std::string(env);
  if (val == "can_create") {
    gpr_once_init(&g_blackhole_ipv6_discard_prefix, BlackHoleIPv6DiscardPrefix);
  } else if (val != "already_exists") {
    gpr_log(
        GPR_ERROR,
        "Need %s set to \"can_create\" or \"already_exists\" in order "
        "for GetBlackHoledIPv6Address to work. Have setting:|%s|.\n"
        "Setting this to \"can_create\" will allow this test to attempt "
        "to modify the local network stack, with effectively the following:\n"
        "$ sudo ip link add dummy0 type dummy\n"
        "$ sudo ip link set dummy0 up\n"
        "$ sudo ip route add 100::/64 dev dummy0\n"
        "... whether or not this is actually possible depends on this test's "
        "specific runtime environment; normally this is only expected to work "
        "if the test is running on linux bazel RBE, or if running as root on a "
        "development machine or within a docker container that has the "
        "NET_ADMIN "
        "capability.\n"
        "Setting this to \"already_exists\" is suitable if this test is "
        "running in an environment where the 100::/64 is known to already be "
        "black holed; this can be useful for example if this test is running "
        "as "
        "a non-root user on a development machine.",
        key, val.c_str());
    abort();
  }
  gpr_once_init(&g_ensure_ipv6_discard_prefix_is_blackholed,
                EnsureIpv6DiscardPrefixIsBlackholed);
  return "ipv6:[100::1234]:443";
}

}  // namespace testing
}  // namespace grpc_core

#endif  // GPR_LINUX
