

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
  version = '0.0.26'
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
    :commit => "c8b740adfd0261b7c8c53d8bfd4b78080676d902",
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

  s.pod_target_xcconfig = {
    # Do not let src/include/openssl/time.h override system API
    'USE_HEADERMAP' => 'NO',
  }

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydW3PbuJao3+dXuM68nKnaNRM7nbT7
      vCm2kmji2N6S3NOZFxYlUjJ3KFIhKF/61x+ApERc1gK5Fly1a6Zj6fsWBYC4EQT+67/OtmmRVnGdJmer
      19M/olVZZcVWiDzaV+kme4ke0zhJq/8Uj2dlcfap+XSxuDlbl7tdVv+/s/Xl6vff3sXJJnl38fF89fv6
      cv3hfXK52iS/rX6/fHf57uPvH5M/3l3827/913+dXZX71yrbPtZn/3f9H2cX784v/3H2pSy3eXo2K9b/
      Kb+ivnWfVrtMiEzGq8uzg0j/IaPtX/9xtiuTbCP/f1wk/1VWZ0km6ipbHer0rH7MxJkoN/VzXKVnG/lh
      XLwq1/5Q7UuRnj1ntfwBVfP/y0N9tknTM4k8plWqfn0VFzIh/nG2r8qnLJFJUj/Gtfw/6Vm8Kp9SZVqf
      rr0o62ydqqto4+776z1+tN+ncXWWFWdxnisyS8Xx1y2/Ts8Wd5+X/zOZT89mi7P7+d2fs+vp9dn/mSzk
      v//P2eT2uvnS5GH59W5+dj1bXN1MZt8XZ5ObmzNJzSe3y9l0oVz/M1t+PZtPv0zmErmTlPT17turm4fr
      2e2XBpx9v7+ZySi94Ozus3J8n86vvsq/TD7NbmbLH034z7Pl7XSx+E/pOLu9O5v+Ob1dni2+Ko92ZZ+m
      Zzezyaeb6dln+a/J7Q+lW9xPr2aTm3/I655Pr5b/kIrjf8kvXd3dLqb/fJA6+Z2z68n3yRd1IQ19/Gfz
      w75Olos7GXcuf97i4Wapfsbn+d33s5u7hbrys4fFVMaYLCeKlmkoL3nxD8lN5QXO1XVP5P+ulrO7W+WT
      gAy9nE/UddxOv9zMvkxvr6aKvWuA5d1cfvdh0TH/OJvMZwsV9O5hqeg75WyK8N3t7bT5Tpv6Kj3ktTRX
      MZ3LhPg+acSfzdz4z6b8f7qbS6e8faLJ9XV0P59+nv11to9FnYqz+rk8k0WvqLNNllZCFh5Z+MsilZlQ
      qyImC/VOqD8oUVaru1WVuHJztovXVXmWvuzjoimE8n9ZLc7ianvYSZ84W6USTptA8u79z3/790Te2UUK
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
