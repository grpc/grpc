

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
  version = '0.0.19'
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
    :commit => "bcc01b6c66b1c6fa2816b108e50a544b757fbd7b",
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
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLso/50sbg9W1e7Xd78v7PVev3ufPVh/eHD6nz9YZte
      XJ3L/3p3lV2+Sy9/+WX16+Wv29Xm19W//dt//dfZdbV/rfPHp+bs/67/4+zi3fnVP85+r6rHIjublev/
      lF9R33rI6l0uRC7jNdXZQWT/kNH2r/8421WbfCv/f1pu/quqzza5aOp8dWiys+YpF2ei2jYvaZ2dbeWH
      afmqXPtDva9EdvaSN/IH1Pr/V4fmbJtlZxJ5yupM/fo6LWVC/ONsX1fP+UYmSfOUNvL/ZGfpqnrOlGl9
      uvayavJ1pq6ijbvvr/f40X6fpfVZXp6lRaHIPBPHX7f8PD1b3H9a/s9kPj2bLc4e5vd/zm6mN2f/Z7KQ
      //4/Z5O7G/2lydfl5/v52c1scX07mX1ZnE1ub88kNZ/cLWfThXL9z2z5+Ww+/X0yl8i9pKSvd99d3369
      md39rsHZl4fbmYzSC87uPynHl+n8+rP8y+Tj7Ha2/KbDf5ot76aLxX9Kx9nd/dn0z+nd8mzxWXmMK/s4
      PbudTT7eTs8+yX9N7r4p3eJhej2b3P5DXvd8er38h1Qc/0t+6fr+bjH951epk985u5l8mfyuLkTTx3/q
      H/Z5slzcy7hz+fMWX2+X6md8mt9/Obu9X6grP/u6mMoYk+VE0TIN5SUv/iG5qbzAubruifzf9XJ2f6d8
      EpChl/OJuo676e+3s9+nd9dTxd5rYHk/l9/9uuiYf5xN5rOFCnr/danoe+XURfj+7m6qv9OmvkoPeS36
      KqZzmRBfJlr8yc6N/9Tl/+P9XDrl7ZNMbm6Sh/n00+yvs30qmkycNS/VmSx6ZZNv86wWsvDIwl+VmcyE
      RhUxWah3Qv1BifJG3a2qxFXbs126rquz7Oc+LXUhlP/LG3GW1o+HnfSJs1Um4UwHknfvf/7bv2/knV1m
      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
      lWuvnUE9Y/Bq6S/JgY6UYr//9j8R+yVO/ZwEAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
