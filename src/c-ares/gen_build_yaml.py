#!/usr/bin/env python2.7

# Copyright 2015, Google Inc.
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

import re
import os
import sys
import yaml

os.chdir(os.path.dirname(sys.argv[0])+'/../..')

out = {}

try:
  def gen_ares_build(x):
    subprocess.call("third_party/cares/cares/buildconf", shell=True)
    subprocess.call("third_party/cares/cares/configure", shell=True)

  def config_platform(x):
    if 'linux' in sys.platform:
      return 'src/cares/cares/config_linux/ares_config.h'
    if 'darwin' in sys.platform:
      return 'src/cares/cares/config_darwin/ares_config.h'
    if not os.path.isfile('third_party/cares/cares/ares_config.h'):
      gen_ares_build(x)
    return 'third_party/cares/cares/ares_config.h'

  def ares_build(x):
    if os.path.isfile('src/cares/cares/ares_build.h'):
      return 'src/cares/cares/ares_build.h'
    if not os.path.isfile('third_party/cares/cares/ares_build.h'):
      gen_ares_build(x)
    return 'third_party/cares/cares/ares_build.h'

  out['libs'] = [{
      'name': 'ares',
      'defaults': 'ares',
      'build': 'private',
      'language': 'c',
      'secure': 'no',
      'src': [
        "third_party/cares/cares/ares__close_sockets.c",
        "third_party/cares/cares/ares__get_hostent.c",
        "third_party/cares/cares/ares__read_line.c",
        "third_party/cares/cares/ares__timeval.c",
        "third_party/cares/cares/ares_cancel.c",
        "third_party/cares/cares/ares_create_query.c",
        "third_party/cares/cares/ares_data.c",
        "third_party/cares/cares/ares_destroy.c",
        "third_party/cares/cares/ares_expand_name.c",
        "third_party/cares/cares/ares_expand_string.c",
        "third_party/cares/cares/ares_fds.c",
        "third_party/cares/cares/ares_free_hostent.c",
        "third_party/cares/cares/ares_free_string.c",
        "third_party/cares/cares/ares_getenv.c",
        "third_party/cares/cares/ares_gethostbyaddr.c",
        "third_party/cares/cares/ares_gethostbyname.c",
        "third_party/cares/cares/ares_getnameinfo.c",
        "third_party/cares/cares/ares_getopt.c",
        "third_party/cares/cares/ares_getsock.c",
        "third_party/cares/cares/ares_init.c",
        "third_party/cares/cares/ares_library_init.c",
        "third_party/cares/cares/ares_llist.c",
        "third_party/cares/cares/ares_mkquery.c",
        "third_party/cares/cares/ares_nowarn.c",
        "third_party/cares/cares/ares_options.c",
        "third_party/cares/cares/ares_parse_a_reply.c",
        "third_party/cares/cares/ares_parse_aaaa_reply.c",
        "third_party/cares/cares/ares_parse_mx_reply.c",
        "third_party/cares/cares/ares_parse_naptr_reply.c",
        "third_party/cares/cares/ares_parse_ns_reply.c",
        "third_party/cares/cares/ares_parse_ptr_reply.c",
        "third_party/cares/cares/ares_parse_soa_reply.c",
        "third_party/cares/cares/ares_parse_srv_reply.c",
        "third_party/cares/cares/ares_parse_txt_reply.c",
        "third_party/cares/cares/ares_platform.c",
        "third_party/cares/cares/ares_process.c",
        "third_party/cares/cares/ares_query.c",
        "third_party/cares/cares/ares_search.c",
        "third_party/cares/cares/ares_send.c",
        "third_party/cares/cares/ares_strcasecmp.c",
        "third_party/cares/cares/ares_strdup.c",
        "third_party/cares/cares/ares_strerror.c",
        "third_party/cares/cares/ares_timeout.c",
        "third_party/cares/cares/ares_version.c",
        "third_party/cares/cares/ares_writev.c",
        "third_party/cares/cares/bitncmp.c",
        "third_party/cares/cares/inet_net_pton.c",
        "third_party/cares/cares/inet_ntop.c",
        "third_party/cares/cares/windows_port.c",
      ],
      'headers': [
        "third_party/cares/cares/ares.h",
        "third_party/cares/cares/ares_data.h",
        "third_party/cares/cares/ares_dns.h",
        "third_party/cares/cares/ares_getenv.h",
        "third_party/cares/cares/ares_getopt.h",
        "third_party/cares/cares/ares_inet_net_pton.h",
        "third_party/cares/cares/ares_iphlpapi.h",
        "third_party/cares/cares/ares_ipv6.h",
        "third_party/cares/cares/ares_library_init.h",
        "third_party/cares/cares/ares_llist.h",
        "third_party/cares/cares/ares_nowarn.h",
        "third_party/cares/cares/ares_platform.h",
        "third_party/cares/cares/ares_private.h",
        "third_party/cares/cares/ares_rules.h",
        "third_party/cares/cares/ares_setup.h",
        "third_party/cares/cares/ares_strcasecmp.h",
        "third_party/cares/cares/ares_strdup.h",
        "third_party/cares/cares/ares_version.h",
        "third_party/cares/cares/bitncmp.h",
        "third_party/cares/cares/config-win32.h",
        "third_party/cares/cares/setup_once.h",
        "third_party/cares/ares_build.h",
        "third_party/cares/config_linux/ares_config.h",
        "third_party/cares/config_darwin/ares_config.h"
    ],
  }]
except:
  pass

print yaml.dump(out)
