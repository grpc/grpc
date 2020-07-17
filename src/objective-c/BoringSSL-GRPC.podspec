

# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/src/objective-c/BoringSSL-GRPC.podspec.template` instead. This
# file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# BoringSSL CocoaPods podspec

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

Pod::Spec.new do |s|
  s.name     = 'BoringSSL-GRPC'
  version = '0.0.10'
  s.version  = version
  s.summary  = 'BoringSSL is a fork of OpenSSL that is designed to meet Google\'s needs.'
  # Adapted from the homepage:
  s.description = <<-DESC
    BoringSSL is a fork of OpenSSL that is designed to meet Google's needs.

    Although BoringSSL is an open source project, it is not intended for general use, as OpenSSL is.
    We don't recommend that third parties depend upon it. Doing so is likely to be frustrating
    because there are no guarantees of API stability. Only the latest version of this pod is
    supported, and every new version is a new major version.

    We update Google libraries and programs that use BoringSSL as needed when deciding to make API
    changes. This allows us to mostly avoid compromises in the name of compatibility. It works for
    us, but it may not work for you.

    As a Cocoapods pod, it has the advantage over OpenSSL's pods that the library doesn't need to
    be precompiled. This eliminates the 10 - 20 minutes of wait the first time a user does "pod
    install", lets it be used as a dynamic framework (pending solution of Cocoapods' issue #4605),
    and works with bitcode automatically. It's also thought to be smaller than OpenSSL (which takes
    1MB - 2MB per ARM architecture), but we don't have specific numbers yet.

    BoringSSL arose because Google used OpenSSL for many years in various ways and, over time, built
    up a large number of patches that were maintained while tracking upstream OpenSSL. As Google's
    product portfolio became more complex, more copies of OpenSSL sprung up and the effort involved
    in maintaining all these patches in multiple places was growing steadily.

    Currently BoringSSL is the SSL library in Chrome/Chromium, Android (but it's not part of the
    NDK) and a number of other apps/programs.
  DESC
  s.homepage = 'https://github.com/google/boringssl'
  s.license  = { :type => 'Mixed', :file => 'LICENSE' }
  # "The name and email addresses of the library maintainers, not the Podspec maintainer."
  s.authors  = 'Adam Langley', 'David Benjamin', 'Matt Braithwaite'

  s.source = {
    :git => 'https://github.com/google/boringssl.git',
    :commit => "e8a935e323510419e0b37638716f6df4dcbbe6f6",
  }

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.7'
  s.tvos.deployment_target = '10.0'
  s.watchos.deployment_target = '4.0'

  name = 'openssl_grpc'

  # When creating a dynamic framework, name it openssl.framework instead of BoringSSL.framework.
  # This lets users write their includes like `#include <openssl/ssl.h>` as opposed to `#include
  # <BoringSSL/ssl.h>`.
  s.module_name = name

  # When creating a dynamic framework, copy the headers under `include/openssl/` into the root of
  # the `Headers/` directory of the framework (i.e., not under `Headers/include/openssl`).
  #
  # TODO(jcanizales): Debug why this doesn't work on macOS.
  s.header_mappings_dir = 'src/include/openssl'

  # The above has an undesired effect when creating a static library: It forces users to write
  # includes like `#include <BoringSSL/ssl.h>`. `s.header_dir` adds a path prefix to that, and
  # because Cocoapods lets omit the pod name when including headers of static libraries, the
  # following lets users write `#include <openssl/ssl.h>`.
  s.header_dir = name

  # The module map and umbrella header created automatically by Cocoapods don't work for C libraries
  # like this one. The following file, and a correct umbrella header, are created on the fly by the
  # `prepare_command` of this pod.
  s.module_map = 'src/include/openssl/BoringSSL.modulemap'

  # We don't need to inhibit all warnings; only -Wno-shorten-64-to-32. But Cocoapods' linter doesn't
  # want that for some reason.
  s.compiler_flags = '-DOPENSSL_NO_ASM', '-GCC_WARN_INHIBIT_ALL_WARNINGS', '-w', '-DBORINGSSL_PREFIX=GRPC'
  s.requires_arc = false

  # Like many other C libraries, BoringSSL has its public headers under `include/<libname>/` and its
  # sources and private headers in other directories outside `include/`. Cocoapods' linter doesn't
  # allow any header to be listed outside the `header_mappings_dir` (even though doing so works in
  # practice). Because we need our `header_mappings_dir` to be `include/openssl/` for the reason
  # mentioned above, we work around the linter limitation by dividing the pod into two subspecs, one
  # for public headers and the other for implementation. Each gets its own `header_mappings_dir`,
  # making the linter happy.
  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'src/include/openssl'
    ss.source_files = 'src/include/openssl/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'
    ss.source_files = 'src/ssl/*.{h,c,cc}',
                      'src/ssl/**/*.{h,c,cc}',
                      'src/crypto/*.{h,c,cc}',
                      'src/crypto/**/*.{h,c,cc}',
                      # We have to include fiat because spake25519 depends on it
                      'src/third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'err_data.c'

    ss.private_header_files = 'src/ssl/*.h',
                              'src/ssl/**/*.h',
                              'src/crypto/*.h',
                              'src/crypto/**/*.h',
                              'src/third_party/fiat/*.h'
    # bcm.c includes other source files, creating duplicated symbols. Since it is not used, we
    # explicitly exclude it from the pod.
    # TODO (mxyan): Work with BoringSSL team to remove this hack.
    ss.exclude_files = 'src/crypto/fipsmodule/bcm.c',
                       'src/**/*_test.*',
                       'src/**/test_*.*',
                       'src/**/test/*.*'

    ss.dependency "#{s.name}/Interface", version
  end

  s.prepare_command = <<-END_OF_COMMAND
    # Add a module map and an umbrella header
    cat > src/include/openssl/umbrella.h <<EOF
      #include "ssl.h"
      #include "crypto.h"
      #include "aes.h"
      /* The following macros are defined by base.h. The latter is the first file included by the    
         other headers. */    
      #if defined(OPENSSL_ARM) || defined(OPENSSL_AARCH64)    
      #  include "arm_arch.h"   
      #endif
      #include "asn1.h"
      #include "asn1_mac.h"
      #include "asn1t.h"
      #include "blowfish.h"
      #include "cast.h"
      #include "chacha.h"
      #include "cmac.h"
      #include "conf.h"
      #include "cpu.h"
      #include "curve25519.h"
      #include "des.h"
      #include "dtls1.h"
      #include "hkdf.h"
      #include "md4.h"
      #include "md5.h"
      #include "obj_mac.h"
      #include "objects.h"
      #include "opensslv.h"
      #include "ossl_typ.h"
      #include "pkcs12.h"
      #include "pkcs7.h"
      #include "pkcs8.h"
      #include "poly1305.h"
      #include "rand.h"
      #include "rc4.h"
      #include "ripemd.h"
      #include "safestack.h"
      #include "srtp.h"
      #include "x509.h"
      #include "x509v3.h"
    EOF
    cat > src/include/openssl/BoringSSL.modulemap <<EOF
      framework module openssl {
        umbrella header "umbrella.h"
        textual header "arm_arch.h"
        export *
        module * { export * }
      }
    EOF

    # To avoid symbol conflict with OpenSSL, gRPC needs to rename all the BoringSSL symbols with a
    # prefix. This is done with BoringSSL's BORINGSSL_PREFIX mechanism
    # (https://github.com/google/boringssl/blob/75148d7abf12bdd1797fec3c5da9a21963703516/BUILDING.md#building-with-prefixed-symbols).
    # The required prefix header file boringssl_prefix_symbols.h is not part of BoringSSL repo at
    # this moment. It has to be generated by BoringSSL's users and be injected to BoringSSL build.
    # gRPC generates this file in script /tools/distrib/upgrade_boringssl_objc.sh. This script
    # outputs a gzip+base64 encoded version of boringssl_prefix_symbols.h because of Cocoapods'
    # limit on the 'prepare_command' field length. The encoded header is generated from
    # /src/boringssl/boringssl_prefix_symbols.h. Here we decode the content and inject the header to
    # the correct location in BoringSSL.
    base64 -D <<EOF | gunzip > src/include/openssl/boringssl_prefix_symbols.h
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nW73
      fVNsJdG0Y/tIck9nXliUSNk8oUiFoOy4f/0FSIrEx94g94arTs10LK61SRDEF0Hgv/7r7DEt0iqu0+Rs
      89r/I9qUVVY8CpFHhyrdZT+jpzRO0uo/xdNZWZx9bH5drW7OtuV+n9X/7+zD779tLs/fvf/t9/T84tc4
      ffcheX+xPU9+/2VzHv/+y6+/vL/8EG+Td//2b//1X2dX5eG1yh6f6rP/u/2Ps4t355f/OPtclo95erYo
      tv8pD1FH3afVPhMik/Hq8uwo0n/IaIfXf5ztyyTbyf8fF8l/ldVZkom6yjbHOj2rnzJxJspd/RJX6dlO
      /hgXr8p1OFaHUqRnL1ktL6Bq/n95rM92aXomkae0StXVV3EhE+IfZ4eqfM4SmST1U1zL/5OexZvyOVWm
      bX/uRVln21SdRRv3MJzv6afDIY2rs6w4i/NckVkqTle3/jI/W919Wv/PbDk/W6zO7pd3fy6u59dn/2e2
      kv/+P2ez2+vmoNnD+svd8ux6sbq6mS2+rs5mNzdnklrObteL+Uq5/mex/nK2nH+eLSVyJynpG9y3VzcP
      14vbzw24+Hp/s5BRBsHZ3Sfl+DpfXn2Rf5l9XNws1t+a8J8W69v5avWf0nF2e3c2/3N+uz5bfVEe7cw+
      zs9uFrOPN/OzT/Jfs9tvSre6n18tZjf/kOe9nF+t/yEVp/+SB13d3a7m/3yQOnnM2fXs6+yzOpGGPv2z
      ubAvs/XqTsZdystbPdys1WV8Wt59Pbu5W6kzP3tYzWWM2XqmaJmG8pRX/5DcXJ7gUp33TP7var24u1U+
      CcjQ6+VMncft/PPN4vP89mqu2LsGWN8t5bEPq475x9lsuVipoHcPa0XfKWeThe9ub+fNMW3qq/SQ59Kc
      xXwpE+LrrBF/Mu/Gfzb5/+PdUjrl4xPNrq+j++X80+Kvs0Ms6lSc1S/lmcx6RZ3tsrQSMvPIzF8WqbwJ
      tcpiMlPvhfqDEmW1elpVjit3Z/t4W5Vn6c9DXDSZUP4vq8VZXD0e99InzjaphNMmkHx6//Pf/j2RT3aR
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
