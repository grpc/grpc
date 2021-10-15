

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
    :commit => "95b3ed1b01f2ef1d72fed290ed79fe1b0e7dafc0",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nW73
      fVNsJdG0Y/tIck9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLso/50sbg5W1e7Xd78v7PfP6zeZ5vz1bvz7UW2Pd/8
      drHNNhe/v8s2v/2+zeTfs9826Xb97t/+7b/+6+yq2r/W+eNTc/Z/1/9xdvHu/PIfZ5+r6rHIzmbl+j/l
      V9S37rN6lwuRy3hNdXYQ2T9ktP3rP8521Sbfyv+flpv/quqzTS6aOl8dmuysecrFmai2zUtaZ2db+WFa
      virX/lDvK5GdveSN/AG1/v/VoTnbZtmZRJ6yOlO/vk5LmRD/ONvX1XO+kUnSPKWN/D/ZWbqqnjNlWp+u
      vayafJ2pq2jj7vvrPX6032dpfZaXZ2lRKDLPxPHXLb9MzxZ3n5b/M5lPz2aLs/v53Z+z6+n12f+ZLOS/
      /8/Z5PZaf2nysPxyNz+7ni2ubiazr4uzyc3NmaTmk9vlbLpQrv+ZLb+czaefJ3OJ3ElK+nr37dXNw/Xs
      9rMGZ1/vb2YySi84u/ukHF+n86sv8i+Tj7Ob2fKbDv9ptrydLhb/KR1nt3dn0z+nt8uzxRflMa7s4/Ts
      Zjb5eDM9+yT/Nbn9pnSL++nVbHLzD3nd8+nV8h9Scfwv+aWru9vF9J8PUie/c3Y9+Tr5rC5E08d/6h/2
      ZbJc3Mm4c/nzFg83S/UzPs3vvp7d3C3UlZ89LKYyxmQ5UbRMQ3nJi39IbiovcK6ueyL/d7Wc3d0qnwRk
      6OV8oq7jdvr5ZvZ5ens1VeydBpZ3c/ndh0XH/ONsMp8tVNC7h6Wi75RTF+G729up/k6b+io95LXoq5jO
      ZUJ8nWjxJzs3/lOX/493c+mUt08yub5O7ufTT7O/zvapaDJx1rxUZ7LolU2+zbNayMIjC39VZjITGlXE
      ZKHeCfUHJcobdbeqEldtz3bpuq7Osp/7tNSFUP4vb8RZWj8edtInzlaZhDMdSN69//lv/76Rd3aZgZfz
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
