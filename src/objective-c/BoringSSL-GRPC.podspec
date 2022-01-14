

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
  version = '0.0.24'
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nW73
      fVNsJdG0Y/tIck9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLso/50sbg5W1e7Xd78v7PV7xfvL7a/Zxe/ZR9+/fVy
      tX73y/kvl7/9vllvss3qIv3we5b+9mF78W//9l//dXZV7V/r/PGpOfu/6/84u3h3fvmPs89V9VhkZ7Ny
      /Z/yK+pb91m9y4XIZbymOjuI7B8y2v71H2e7apNv5f9Py81/VfXZJhdNna8OTXbWPOXiTFTb5iWts7Ot
      /DAtX5Vrf6j3lcjOXvJG/oBa///q0Jxts+xMIk9ZnalfX6elTIh/nO3r6jmXFy6FaSP/T3aWrqrnTJnW
      p2svqyZfZ+oq2rj7/nqPH+33WVqf5eVZWhSKzDNx/HXLL9Ozxd2n5f9M5tOz2eLsfn735+x6en32fyYL
      +e//cza5vdZfmjwsv9zNz65ni6ubyezr4mxyc3MmqfnkdjmbLpTrf2bLL2fz6efJXCJ3kpK+3n17dfNw
      Pbv9rMHZ1/ubmYzSC87uPinH1+n86ov8y+Tj7Ga2/KbDf5otb6eLxX9Kx9nt3dn0z+nt8mzxRXmMK/s4
      PbuZTT7eTM8+yX9Nbr8p3eJ+ejWb3PxDXvd8erX8h1Qc/0t+6erudjH954PUye+cXU++Tj6rC9H08Z/6
      h32ZLBd3Mu5c/rzFw81S/YxP87uvZzd3C3XlZw+LqYwxWU4ULdNQXvLiH5Kbygucq+ueyP9dLWd3t8on
      ARl6OZ+o67idfr6ZfZ7eXk0Ve6eB5d1cfvdh0TH/OJvMZwsV9O5hqeg75dRF+O72dqq/06a+Sg95Lfoq
      pnOZEF8nWvzJzo3/1OX/491cOuXtk0yur5P7+fTT7K+zfSqaTJw1L9WZLHplk2/zrBay8MjCX5WZzIRG
      FTFZqHdC/UGJ8kbdrarEVduzXbquq7Ps5z4tdSGU/8sbcZbWj4ed9ImzVSbhTAeSd+9//tu/b+SdXWbg
      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
      ynDzD1yyKfaf3/4P5m1j6i2mBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
