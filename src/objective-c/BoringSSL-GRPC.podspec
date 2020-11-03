

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
  version = '0.0.14'
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
    :commit => "29c6e0e27268f5a43e039cd2ed4e849d6b736fc1",
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
      hwL4R48EAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
