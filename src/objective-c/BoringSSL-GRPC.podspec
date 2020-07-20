

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
  version = '0.0.11'
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
      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
      pdwGVECzR7Gff/oPxLWS61eCBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
