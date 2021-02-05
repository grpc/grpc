

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
    :commit => "de220fb27fa5d0cf51da8d3d06f33bcb1b1b1146",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKy9XXPbuJaofT+/wnXm5pyqXTOx0+72
      fu8UW+lo2rE9ktzTmRsWJVI2dyhSISg77l//AiRF4mMtkGvBdXbN6Vh6nkWB+CYI/Od/nj2lRVrFdZqc
      bd76f0SbssqKJyHy6FClu+xn9JzGSVr9h3g+K4uzT82nq9Xt2bbc77P6/ztL0ouLD7vNxW+7+DL5sN1d
      nifxVfIx+fDr7uPHzXZzrv7f+S+//tu//ed/nl2Xh7cqe3quz/7v9v+dXXw4v/rH2e9l+ZSnZ4ti+x/y
      K+pbD2m1z4TIZLy6PDuK9B8y2uHtH2f7Msl28v+Pi+Q/y+osyURdZZtjnZ7Vz5k4E+Wufo2r9GwnP4yL
      N+U6HKtDKdKz16yWP6Bq/v/yWJ/t0vRMIs9plapfX8WFTIh/nB2q8iVLZJLUz3Et/096Fm/Kl1SZtv21
      F2WdbVN1FW3cw3C9p48OhzSuzrLiLM5zRWapOP269Zf52er+8/p/Zsv52WJ19rC8/3NxM785+z+zlfz3
      /zmb3d00X5o9rr/cL89uFqvr29ni6+psdnt7Jqnl7G69mK+U638W6y9ny/nvs6VE7iUlfYP77vr28WZx
      93sDLr4+3C5klEFwdv9ZOb7Ol9df5F9mnxa3i/W3Jvznxfpuvlr9h3Sc3d2fzf+c363PVl+UR7uyT/Oz
      28Xs0+387LP81+zum9KtHubXi9ntP+R1L+fX639Ixem/5Jeu7+9W8/9+lDr5nbOb2dfZ7+pCGvr0z+aH
      fZmtV/cy7lL+vNXj7Vr9jM/L+69nt/crdeVnj6u5jDFbzxQt01Be8uofkpvLC1yq657J/12vF/d3yicB
      GXq9nKnruJv/frv4fX53PVfsfQOs75fyu4+rjvnH2Wy5WKmg949rRd8rZ5OF7+/u5s132tRX6SGvpbmK
      +VImxNdZI/5s3o3/aPL/p/uldMriE81ubqKH5fzz4q+zQyzqVJzVr+WZzHpFne2ytBIy88jMXxapvAm1
      ymIyU++F+oMSZbUqrSrHlbuzfbytyrP05yEumkwo/5fV4iyuno576RNnm1TCaRNIlt7/+Ld/T2TJLlLw
      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
      BfWMIcrl1oEGqj2K/f7b/+pPpdoVlQQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
