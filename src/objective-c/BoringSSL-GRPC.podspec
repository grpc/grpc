

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
    :commit => "2a3f97f7970676b7f6855f5d7504e03976494d75",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKy9XXPbuJaofT+/wnXm5kzVrj2x0k6n
      3zvFVhJNHNtbkns654ZFSZTNHYpUCNIf/etfgKREfKwFci24atdMx9LzLArEN0Hgv//77CHJkzKuku3Z
      +vX0j2hdlGn+IEQWHcpkl75Ej0m8Tcp/isezIj/71Hy6XF6fbYr9Pq3+v7NJ/H73x++73//4/d2H3z+s
      f999+HhxsbvY/n7x7rfk3fs/fv/w2x+/yX/9x3/893+fXRaH1zJ9eKzO/u/mv84m784//uPsS1E8ZMnZ
      PN/8U35FfesuKfepEKmMVxVntUj+IaMdXv9xti+26U7+/zjf/ndRnm1TUZXpuq6Ss+oxFWei2FXPcZmc
      7eSHcf6qXIe6PBQiOXtOK/kDyub/F3V1tkuSM4k8JmWifn0Z5zIh/nF2KIundCuTpHqMK/l/krN4XTwl
      yrQ5XXteVOkmUVfRxj3013v86HBI4vIszc/iLFNkmojjr1t9nZ0tbz+v/ne6mJ3Nl2d3i9s/51ezq7P/
      M13Kf/+fs+nNVfOl6f3q6+3i7Gq+vLyezr8vz6bX12eSWkxvVvPZUrn+d776eraYfZkuJHIrKenr3TeX
      1/dX85svDTj/fnc9l1F6wdntZ+X4PltcfpV/mX6aX89XP5rwn+erm9ly+U/pOLu5PZv9ObtZnS2/Ko92
      ZZ9mZ9fz6afr2dln+a/pzQ+lW97NLufT63/I617MLlf/kIrjf8kvXd7eLGf/upc6+Z2zq+n36Rd1IQ19
      /Gfzw75OV8tbGXchf97y/nqlfsbnxe33s+vbpbrys/vlTMaYrqaKlmkoL3n5D8nN5AUu1HVP5f8uV/Pb
      G+WTgAy9WkzVddzMvlzPv8xuLmeKvW2A1e1Cfvd+2TH/OJsu5ksV9PZ+pehb5Wyy8O3Nzaz5Tpv6Kj3k
      tTRXMVvIhPg+bcSfzbvxzyb/f7pdSKcsPtH06iq6W8w+z/86O8SiSsRZ9VycyayXV+kuTUohM4/M/EWe
      yJtQqSwmM/VeqD8oUVqp0qpyXLE728ebsjhLXg5x3mRC+b+0Emdx+VDvpU+crRMJJ00gWXr/+R//uZUl
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
