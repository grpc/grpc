

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
  version = '0.0.25'
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
    :commit => "0e2e48f9baa351c58fb68540332dc1382773699a",
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
    set -e
    # Add a module map and an umbrella header
    mkdir -p src/include/openssl
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
    case "$(uname)" in
      Darwin) opts="" ;;
           *) opts="--ignore-garbage" ;;
    esac
    base64 --decode $opts <<EOF | gunzip > src/include/openssl/boringssl_prefix_symbols.h
      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
      8Qzh73/7PyYbopZpvAQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
