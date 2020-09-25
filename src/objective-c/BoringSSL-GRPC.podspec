

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
  version = '0.0.13'
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
    :commit => "88aeb757f1a415c71fb4cbf5af936ecae4bc8179",
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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
