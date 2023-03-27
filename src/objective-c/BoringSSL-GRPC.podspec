

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
  version = '0.0.27'
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
    :commit => "8872d958b7b07173bf29b8f3b8bf36a1ca8c94a3",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydW3PbuJao3+dXuM68nKnaNRM7nW73
      eVNsJdG0Y3tLSk9nXliUBNncoUiFoHzpX38AkhJxWQvkWnDVrpmOpe9bFADiRhD4r/86exCFqNJabM5W
      r6d/JKuyyooHKfNkX4lt9pI8inQjqv+Uj2dlcfax+XSxuDlbl7tdVv+/s8vL3y42v3+4XP22evfb+W/v
      V9uL31eX2/ery9X2/a/p+Tq9XP/+S/r+3/7tv/7r7Krcv1bZw2N99n/X/3F28e788h9nn8vyIRdns2L9
      n+or+lv3otplUmYqXl2eHaT4h4q2f/3H2a7cZFv1/9Ni819ldbbJZF1lq0MtzurHTJ7Jcls/p5U426oP
      0+JVu/aHal9Kcfac1eoHVM3/Lw/12VaIM4U8ikroX1+lhUqIf5ztq/Ip26gkqR/TWv0fcZauyiehTevT
      tRdlna2Fvoo27r6/3uNH+71Iq7OsOEvzXJOZkMdft/wyPVvcfVr+z2Q+PZstzu7nd3/OrqfXZ/9nslD/
      /j9nk9vr5kuTb8svd/Oz69ni6mYy+7o4m9zcnClqPrldzqYL7fqf2fLL2Xz6eTJXyJ2ilK93317dfLue
      3X5uwNnX+5uZitILzu4+acfX6fzqi/rL5OPsZrb83oT/NFveTheL/1SOs9u7s+mf09vl2eKL9hhX9nF6
      djObfLyZnn1S/5rcfte6xf30aja5+Ye67vn0avkPpTj+l/rS1d3tYvrPb0qnvnN2Pfk6+awvpKGP/2x+
      2JfJcnGn4s7Vz1t8u1nqn/Fpfvf17OZuoa/87NtiqmJMlhNNqzRUl7z4h+Km6gLn+ron6n9Xy9ndrfYp
      QIVezif6Om6nn29mn6e3V1PN3jXA8m6uvvtt0TH/OJvMZwsd9O7bUtN32tkU4bvb22nznTb1dXqoa2mu
      YjpXCfF10og/2bnxn035/3g3V051+yST6+vkfj79NPvrbJ/KWsiz+rk8U0WvqLNtJiqpCo8q/GUhVCbU
      uoipQr2T+g9alNX6btUlrtye7dJ1VZ6Jl31aNIVQ/S+r5VlaPRx2yifPVkLBogmk7t7//Ld/36g7uxDg
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
