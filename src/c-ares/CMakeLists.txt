# c-ares cmake file for gRPC
#
# This is currently very experimental, and unsupported.
#
# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

string(TOLOWER ${CMAKE_SYSTEM_NAME} cares_system_name)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cares)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cares/cares)

if(${cares_system_name} MATCHES windows)
  add_definitions(-DCARES_STATICLIB=1)
  add_definitions(-DWIN32_LEAN_AND_MEAN=1)
else()
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cares/config_${cares_system_name})
  add_definitions(-DHAVE_CONFIG_H=1)
  add_definitions(-D_GNU_SOURCE=1)
endif()

add_library(cares 
  ../../third_party/cares/cares/ares__close_sockets.c
  ../../third_party/cares/cares/ares__get_hostent.c
  ../../third_party/cares/cares/ares__read_line.c
  ../../third_party/cares/cares/ares__timeval.c
  ../../third_party/cares/cares/ares_cancel.c
  ../../third_party/cares/cares/ares_data.c
  ../../third_party/cares/cares/ares_destroy.c
  ../../third_party/cares/cares/ares_expand_name.c
  ../../third_party/cares/cares/ares_expand_string.c
  ../../third_party/cares/cares/ares_fds.c
  ../../third_party/cares/cares/ares_free_hostent.c
  ../../third_party/cares/cares/ares_free_string.c
  ../../third_party/cares/cares/ares_getenv.c
  ../../third_party/cares/cares/ares_gethostbyaddr.c
  ../../third_party/cares/cares/ares_gethostbyname.c
  ../../third_party/cares/cares/ares_getnameinfo.c
  ../../third_party/cares/cares/ares_getsock.c
  ../../third_party/cares/cares/ares_init.c
  ../../third_party/cares/cares/ares_library_init.c
  ../../third_party/cares/cares/ares_llist.c
  ../../third_party/cares/cares/ares_mkquery.c
  ../../third_party/cares/cares/ares_create_query.c
  ../../third_party/cares/cares/ares_nowarn.c
  ../../third_party/cares/cares/ares_options.c
  ../../third_party/cares/cares/ares_parse_a_reply.c
  ../../third_party/cares/cares/ares_parse_aaaa_reply.c
  ../../third_party/cares/cares/ares_parse_mx_reply.c
  ../../third_party/cares/cares/ares_parse_naptr_reply.c
  ../../third_party/cares/cares/ares_parse_ns_reply.c
  ../../third_party/cares/cares/ares_parse_ptr_reply.c
  ../../third_party/cares/cares/ares_parse_soa_reply.c
  ../../third_party/cares/cares/ares_parse_srv_reply.c
  ../../third_party/cares/cares/ares_parse_txt_reply.c
  ../../third_party/cares/cares/ares_platform.c
  ../../third_party/cares/cares/ares_process.c
  ../../third_party/cares/cares/ares_query.c
  ../../third_party/cares/cares/ares_search.c
  ../../third_party/cares/cares/ares_send.c
  ../../third_party/cares/cares/ares_strcasecmp.c
  ../../third_party/cares/cares/ares_strdup.c
  ../../third_party/cares/cares/ares_strerror.c
  ../../third_party/cares/cares/ares_timeout.c
  ../../third_party/cares/cares/ares_version.c
  ../../third_party/cares/cares/ares_writev.c
  ../../third_party/cares/cares/bitncmp.c
  ../../third_party/cares/cares/inet_net_pton.c
  ../../third_party/cares/cares/inet_ntop.c
  ../../third_party/cares/cares/windows_port.c
)
