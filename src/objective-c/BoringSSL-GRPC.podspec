

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
    :commit => "fd3e77073fe5bb38181cad3dfed1ffa65b295335",
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
      va0fG7A0VkpxNjSNNSY56/L41CnDmNA01gjknEAaK4E41+Y7rtp8503wOFhIYyWLwDbGp7GSJVj7YtNY
      yYJua1RCyzFG1RBPyneNl73otS+MF50ZYfJdyb+xfFcGZbx418/muw4LgHzXKcObdG0mznclizRtJsp3
      JUsUbSbMd50sgPJdQ44xgjc34nzX4b9AvuuUYUyaz4A//opjzx53TT8V9VFtoe74ApT3urNG6b2ivFfp
      DHy1u5GDD9IJNvUZ/a8WzdyvFo3y93lG/H2eWfMbODP/G7hO93u9Tvq93kV5v+Yi3q+5aO/XXKT7Ncd/
      121Zvdq17QD+5a+2+/G+uL/g2Hnz9+XpHwI+8f+/KSq3uMhMXb10bu3/ZF22uIDASxX+zE7n5U/tcuy8
      GTk2PD763/Lf0+2p3h3T3O6ReyytWJxFwLGjue5fG4n2PQE2+prjzjwkadkVbdaVdWXSbLcrmi4DHiqb
      c0SV3EMWr8sPNaUiW7Mt0qLatT8bLC5RwKn/2T+D5x57LXL/YSD2CA7dTdaaIj0UGXBuxCS1fvV7lBd+
      jxApASfOt21XH4sqLT6aB3tWltXixyYZVPLuTmVRdf4zxgMrFqikuvbwlZdiXNnY3S86XWHeJVW2p7Jr
      K359Xb2pQa7SpQf/mLZ7Mtt2r9pSgUaqVxpzLtpP+RxZlVS3tS1BV8aRktU1XZ3VkZL1XK1oRVeYdyf6
      9pmks95Pa58J0j6TT2yfCdQ+k9XtM1nQPpPPaZ/J0vaZfF77TJD2majbZzLTPhN1+0xm2meypn0mcft0
      oVLukznaRT4NZXve7ws30rXDDjc8WlzovmlSVfNOlZZ/p0o7vhelTzgDWgTHUrP9M3NPKIMjFgblvU1/
      my3t7OEz9ui9aSpEEr6Wj0dps3dNiRsrmX8VOuuvghrhB6MJRF1+mzUxNixM3StCcgSc9dsjvrZGqBjr
      GHvJpZl9CTnGqJl9YeGJ+5A9qC9ZWZi4XfbFCjuHE7/LlV3h5/CJ3/67KBoo8X/KBKZTsTyTfAAYR1ru
      YY1lOFPTtbjKQtR1bhDJuSH0HvjWva5O+bZAjqpbnfBlZYCXXwwAdZjU1G1XIDsyMMQEZEz3a4d0Wp1P
      J0zhEepZnjHer03opkbOB7t2SKOf6Q1hPXaEplBZitrOy19dc12d8KZATjFTdCHtX1GwP1c7TDNg1Hco
      99D2uPWpoYbajFud8Bc3dwoI/PrEgKR3Xlcf+Yv9UtTcIwg52fiiVb7ITuBUZtCJ9zHN3NiuXNxfjQS1
      nDrEcOoIvd3VlQF4vz4x7Jr6hBj8+tTQnlxuYw68LoRSkQ3oO0cisrT+DgMo6qHQlWMW+gnbr3w7LrL/
      BiQDQ0zFR5cez4CmB4jD9szmYAfb4AZNMeIr8wbQ2LUpXe1rBLerB/yh3JZdmlU/oc2YYMTnGujZZK/I
      mTwwxFRlby4YvDL20te9dAkQhij1mrTMntJTaZB+Y0IFth0wchsA4qh3pnF3rewZgnwGUyz2VbWf/UB9
      V4z4ml0JaOzalK5P5e6n8pOMYc79ljVNWb0qxDeSWA3YqEzUqgz8zWaib7a6afeKqfWQY42rJtXvediK
      66bT74rYmpqJdAFn/aumtO952IrIZHaAsT5kGjvAWB84gR2TE2uTFSbdbXe3e+SLpSEYObv2MRnuvPuZ
      FwPKGUNYBZxVJlDoUh0BYe/d9di1DNQuOJhz346Kyj2BR/eHMqj2Q8ypvS55LZAoZgJxLtd2fdNFI91n
      FFyd5qF5cKnvTYIXGNlZ8+MK8yNrfvTvBsvs4EJxwKc0Z++T8F3cLO4e2Xkz9AIlUXCnhnnLTvCLsu+b
      2KrLQ6QJxLm6Gvrqi8DICd8q+hDzo69LzA5810jITYxPX/7489H/wsrPCPU9jPG/IVxsn3HQSmlevrrL
      Rn/TKju91m3ZHd6QOryBr3K9sYT8mk3AA3/Turh3f0fPmBTLFRIFQQ1/y7f78L2QwewUZbyuqOuDug/Y
      O6LU62ajkjItG+RLKOAiY//tYcsdig9QOkUjr+983XRIUZkSmDIT8NhfV/v+uv3NvcWsgAuEfFTB7hX8
      +h0Gjbynuj6a9FQeizSvjN8GUM8YfvvH3xnC40FTuwQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
