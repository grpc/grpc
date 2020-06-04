

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
  version = '0.0.9'
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
    :commit => "3ab047a8e377083a9b38dc908fe1612d5743a021",
  }

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.7'
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nW73
      fVNsJdG0Y/tIck9nXliUSNk8oUiFoOy4f/0FSIrEx94g94arTs10TK21SRDEF0Hgv/7r7DEt0iqu0+Rs
      89r/I9qUVVY8CpFHhyrdZT+jpzRO0uo/xdNZWZx9bI6uVjdn23K/z+r/d/Y+3rz75bf4Mn3/22/vLt/H
      v2/eXybb399d7tLzX88vkg+//fI+fndx/m//9l//dXZVHl6r7PGpPvu/2/84u3h3fvmPs89l+ZinZ4ti
      +5/yJ+pX92m1z4TIZLy6PDuK9B8y2uH1H2f7Msl28v/HRfJfZXWWZKKuss2xTs/qp0yciXJXv8RVeraT
      B+PiVbkOx+pQivTsJavlBVTN/y+P9dkuTc8k8pRWqbr6Ki5kQvzj7FCVz1kik6R+imv5f9KzeFM+p8q0
      7c+9KOtsm6qzaOMehvM9HToc0rg6y4qzOM8VmaXidHXrL/Oz1d2n9f/MlvOzxersfnn35+J6fn32f2Yr
      +e//cza7vW5+NHtYf7lbnl0vVlc3s8XX1dns5uZMUsvZ7XoxXynX/yzWX86W88+zpUTuJCV9g/v26ubh
      enH7uQEXX+9vFjLKIDi7+6QcX+fLqy/yL7OPi5vF+lsT/tNifTtfrf5TOs5u787mf85v12erL8qjndnH
      +dnNYvbxZn72Sf5rdvtN6Vb386vF7OYf8ryX86v1P6Ti9F/yR1d3t6v5Px+kTv7m7Hr2dfZZnUhDn/7Z
      XNiX2Xp1J+Mu5eWtHm7W6jI+Le++nt3crdSZnz2s5jLGbD1TtExDecqrf0huLk9wqc57Jv93tV7c3Sqf
      BGTo9XKmzuN2/vlm8Xl+ezVX7F0DrO+W8rcPq475x9lsuVipoHcPa0XfKWeThe9ub+fNb9rUV+khz6U5
      i/lSJsTXWSP+ZN6N/2zy/8e7pXTKxyeaXV9H98v5p8VfZ4dY1Kk4q1/KM5n1ijrbZWklZOaRmb8sUnkT
      apXFZKbeC/UHJcpq9bSqHFfuzvbxtirP0p+HuGgyofxfVouzuHo87qVPnG1SCadNIPn0/ue//Xsin+wi
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
