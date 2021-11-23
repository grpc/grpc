

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
  version = '0.0.22'
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
    :commit => "4fb158925f7753d80fb858cb0239dff893ef9f15",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM77W73
      fVNsJdG0Y/tISk9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLsg/50sbg9W1e7Xd78v7Nftqvzy6vfLy63v/12+X5z
      9W67urq8Wq/eXbz/fbPdXv3+Ptv+vj2//Ld/+6//Oruu9q91/vjUnP3f9X+cXbw7v/rH2aeqeiyys1m5
      /k/5FfWth6ze5ULkMl5TnR1E9g8Zbf/6j7Ndtcm38v+n5ea/qvpsk4umzleHJjtrnnJxJqpt85LW2dlW
      fpiWr8q1P9T7SmRnL3kjf0Ct/391aM62WXYmkaesztSvr9NSJsQ/zvZ19ZxvZJI0T2kj/092lq6q50yZ
      1qdrL6smX2fqKtq4+/56jx/t91lan+XlWVoUiswzcfx1y8/Ts8X9x+X/TObTs9ni7GF+/+fsZnpz9n8m
      C/nv/3M2ubvRX5p8XX6+n5/dzBbXt5PZl8XZ5Pb2TFLzyd1yNl0o1//Mlp/P5tNPk7lE7iUlfb377vr2
      683s7pMGZ18ebmcySi84u/+oHF+m8+vP8i+TD7Pb2fKbDv9xtrybLhb/KR1nd/dn0z+nd8uzxWflMa7s
      w/Tsdjb5cDs9+yj/Nbn7pnSLh+n1bHL7D3nd8+n18h9Scfwv+aXr+7vF9J9fpU5+5+xm8mXySV2Ipo//
      1D/s82S5uJdx5/LnLb7eLtXP+Di//3J2e79QV372dTGVMSbLiaJlGspLXvxDclN5gXN13RP5v+vl7P5O
      +SQgQy/nE3Udd9NPt7NP07vrqWLvNbC8n8vvfl10zD/OJvPZQgW9/7pU9L1y6iJ8f3c31d9pU1+lh7wW
      fRXTuUyILxMt/mjnxn/q8v/hfi6d8vZJJjc3ycN8+nH219k+FU0mzpqX6kwWvbLJt3lWC1l4ZOGvykxm
      QqOKmCzUO6H+oER5o+5WVeKq7dkuXdfVWfZzn5a6EMr/5Y04S+vHw076xNkqk3CmA8m79z//7d838s4u
      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
      /8Apm2J//vE/eUEPizujBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
