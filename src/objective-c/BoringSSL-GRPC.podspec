

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
  version = '0.0.12'
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
    :commit => "412844d75b14b9090b58423fd5f5ed8c2fd80212",
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
      exT79Zf/AOIKDiPhhgQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
