

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
  version = '0.0.18'
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
    :commit => "688fc5cf5428868679d2ae1072cad81055752068",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM77bT7
      vim20tG0Y/tISk9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLso/50sbg9W1e7Xd78v7MPV1fb9eV6e/nLxdXVh6sP
      v/62uUiz83e/XqzTzdX5u8vLXy8v3n24+rd/+6//Oruu9q91/vjUnP3f9X+cXbw7v/rH2e9V9VhkZ7Ny
      /Z/yK+pbD1m9y4XIZbymOjuI7B8y2v71H2e7apNv5f9Py81/VfXZJhdNna8OTXbWPOXiTFTb5iWts7Ot
      /DAtX5Vrf6j3lcjOXvJG/oBa///q0Jxts+xMIk9ZnalfX6elTIh/nO3r6jnfyCRpntJG/p/sLF1Vz5ky
      rU/XXlZNvs7UVbRx9/31Hj/a77O0PsvLs7QoFJln4vjrlp+nZ4v7T8v/mcynZ7PF2cP8/s/ZzfTm7P9M
      FvLf/+dscnejvzT5uvx8Pz+7mS2ubyezL4uzye3tmaTmk7vlbLpQrv+ZLT+fzae/T+YSuZeU9PXuu+vb
      rzezu981OPvycDuTUXrB2f0n5fgynV9/ln+ZfJzdzpbfdPhPs+XddLH4T+k4u7s/m/45vVueLT4rj3Fl
      H6dnt7PJx9vp2Sf5r8ndN6VbPEyvZ5Pbf8jrnk+vl/+QiuN/yS9d398tpv/8KnXyO2c3ky+T39WFaPr4
      T/3DPk+Wi3sZdy5/3uLr7VL9jE/z+y9nt/cLdeVnXxdTGWOynChapqG85MU/JDeVFzhX1z2R/7tezu7v
      lE8CMvRyPlHXcTf9/Xb2+/TueqrYew0s7+fyu18XHfOPs8l8tlBB778uFX2vnLoI39/dTfV32tRX6SGv
      RV/FdC4T4stEiz/ZufGfuvx/vJ9Lp7x9ksnNTfIwn36a/XW2T0WTibPmpTqTRa9s8m2e1UIWHln4qzKT
      mdCoIiYL9U6oPyhR3qi7VZW4anu2S9d1dZb93KelLoTyf3kjztL68bCTPnG2yiSc6UDy7v3Pf/v3jbyz
      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
      pwvqGUNQy+0GGrjsUez33/4Hk9kXTTeXBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
