

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
  version = '0.0.8'
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
    :commit => "1c2769383f027befac5b75b6cedd25daf3bf4dcf",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKy9XXOjyJaofT+/wnHm5pyIHTNlV7vb
      +71T2aoqTbtsjyT3dM0NgQSy2YVATSK73L/+zQQE+bFWwlrpiB0zXUbPsyBJ8osk8z//8+wpLdIqrtPk
      bPPW/yPalFVWPAmRR4cq3WU/o+c0TtLqP8TzWVmcfWqOrla3Z9tyv8/q/+/sfHvx26///Hj1cffh4rdN
      uou3l5vfLje/btMkubhM4t3Hze6XZLv7t3/7z/88uy4Pb1X29Fyf/d/t/zu7+HB+9Y+zL2X5lKdni2L7
      H/In6lcPabXPhMhkvLo8O4r0HzLa4e0fZ/syyXby/8dF8p9ldZZkoq6yzbFOz+rnTJyJcle/xlV6tpMH
      4+JNuQ7H6lCK9Ow1q+UFVM3/L4/12S5NzyTynFapuvoqLmRC/OPsUJUvWSKTpH6Oa/l/0rN4U76kyrTt
      z70o62ybqrNo4x6G8z0dOhzSuDrLirM4zxWZpeJ0deuv87PV/ef1/8yW87PF6uxhef/H4mZ+c/Z/Ziv5
      7/9zNru7aX40e1x/vV+e3SxW17ezxbfV2ez29kxSy9ndejFfKdf/LNZfz5bzL7OlRO4lJX2D++769vFm
      cfelARffHm4XMsogOLv/rBzf5svrr/Ivs0+L28X6exP+82J9N1+t/kM6zu7uz+Z/zO/WZ6uvyqOd2af5
      2e1i9ul2fvZZ/mt2913pVg/z68Xs9h/yvJfz6/U/pOL0X/JH1/d3q/l/P0qd/M3Zzezb7Is6kYY+/bO5
      sK+z9epexl3Ky1s93q7VZXxe3n87u71fqTM/e1zNZYzZeqZomYbylFf/kNxcnuBSnfdM/u96vbi/Uz4J
      yNDr5Uydx938y+3iy/zueq7Y+wZY3y/lbx9XHfOPs9lysVJB7x/Xir5XziYL39/dzZvftKmv0kOeS3MW
      86VMiG+zRvzZvBv/0eT/T/dL6ZSPTzS7uYkelvPPiz/PDrGoU3FWv5ZnMusVdbbL0krIzCMzf1mk8ibU
      KovJTL0X6g9KlNXqaVU5rtyd7eNtVZ6lPw9x0WRC+b+sFmdx9XTcS584k49lKR8LFUg+vf/xb/+eyCe7
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
