

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
  version = '0.0.17'
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
    :commit => "1a7359455220f7010def8c63f7c7e041ce6707c6",
  }

  s.ios.deployment_target = '9.0'
  s.osx.deployment_target = '10.10'
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
      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
      hiiXWwcaqPYo9vtv/wM/DBrFrZQEAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
