

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
  version = '0.0.32'
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
    :commit => "2ff4b968a7e0cfee66d9f151cb95635b43dc1d5b",
  }

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
  s.watchos.deployment_target = '6.0'

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
    ss.private_header_files = 'src/include/openssl/time.h'
    ss.source_files = 'src/include/openssl/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'

    ss.resource_bundles = {
      s.module_name => 'src/PrivacyInfo.xcprivacy'
    }

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnYaafd
      751iK4kmju0tKT2duWFRImVxhyIVgvJH//oDkJSIj7VArgW/VbtmOpaeZ1EAiC+CwH/919ljWqRVXKfJ
      2er19I9oVVZZ8ShEHu2rdJO9RNs0TtLqP8X2rCzOPjafLha3Z+tyt8vq/+/s96vk9/e/n199uLy6fPf7
      ZrVKzt+9f//u6sP6jz/en1/+/u7DOrl49+7q3/7tv/7r7Lrcv1bZ47Y++7/r/zi7eHd+9Y+zz2X5mKdn
      s2L9n/Ir6lsPabXLhMhkvLo8O4j0HzLa/vUfZ7syyTby/8dF8l9ldZZkoq6y1aFOz+ptJs5Euamf4yo9
      28gP4+JVufaHal+K9Ow5q+UPqJr/Xx7qs02anklkm1ap+vVVXMiE+MfZviqfskQmSb2Na/l/0rN4VT6l
      yrQ+XXtR1tk6VVfRxt3313v8aL9P4+osK87iPFdklorjr1t+mZ4t7j8t/2cyn57NFmcP8/s/ZzfTm7P/
      M1nIf/+fs8ndTfOlyffll/v52c1scX07mX1bnE1ub88kNZ/cLWfThXL9z2z55Ww+/TyZS+ReUtLXu++u
      b7/fzO4+N+Ds28PtTEbpBWf3n5Tj23R+/UX+ZfJxdjtb/mjCf5ot76aLxX9Kx9nd/dn0z+nd8mzxRXm0
      K/s4PbudTT7eTs8+yX9N7n4o3eJhej2b3P5DXvd8er38h1Qc/0t+6fr+bjH953epk985u5l8m3xWF9LQ
      x382P+zLZLm4l3Hn8uctvt8u1c/4NL//dnZ7v1BXfvZ9MZUxJsuJomUaykte/ENyU3mBc3XdE/m/6+Xs
      /k75JCBDL+cTdR1308+3s8/Tu+upYu8bYHk/l9/9vuiYf5xN5rOFCnr/fanoe+VsivD93d20+U6b+io9
      5LU0VzGdy4T4NmnEn8zc+M+m/H+8n0unvH2iyc1N9DCffpr9dbaPRZ2Ks/q5PJNFr6izTZZWQhYeWfjL
      IpWZUKsiJgv1Tqg/KFFWq7tVlbhyc7aL11V5lr7s46IphPJ/WS3O4urxsJM+cbZKJZw2geTd+5//9u+J
      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
      tbLHAOIJwj//8X/eF6yV09UEAA==
    EOF

    # PrivacyInfo.xcprivacy is not part of BoringSSL repo, inject it during pod installation
    base64 --decode $opts <<EOF | gunzip > src/PrivacyInfo.xcprivacy
      H4sICAAAAAAC/1ByaXZhY3lJbmZvLnhjcHJpdmFjeQC1kl9PwjAUxZ/Hp6h9Z1di/JsxAhskJAQXGQ8+
      Nt0VG7a1aRuw395OHUhE8UHflrNzzj2/pNHgpSrJBrURsu7TXnhOCdZcFqJe9ekyn3Rv6CDuRGfpfZI/
      ZmOiSmEsyZaj2TQhtAswVKpEgDRPSTabLnLiOwDGc0ros7XqDmC73YascYVcVo3RQKalQm3dzJd1fSAs
      bEH9mff2gzleLQS3cSeI1uji+SLTYsO4yzXja78ygkb2f59YaRC++BJZlsgtFimzLHcKzS7BtGYOvm1O
      ZcVEfdI+5ByNwWKYTY/U+4+gBQh+TrZBbzNW+wFHnQmzuJLaTUSJuajQWFapCD4SJ488IDNyDxV8mrm/
      m1z1rsPeYSnscaDl+RewhTMWq5GUtsH7Y7KLy8ntL8h2WqtE8PY0484rAb5xoDEDAAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
