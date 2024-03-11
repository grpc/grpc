

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
  version = '0.0.33'
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
    :commit => "de02f415b10fcd5545870a50892a43e9b047295a",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrj2xE6fd
      751jKx1NHNsjKT2duWFREmRzhyIVgvJH//oDkJSIj7VArgW/VbtmOpaeZ1EAiC+CwH/+58mDKESV1mJ9
      snw9/iNZllVWPEiZJ7tKbLKX5FGka1H9Uz6elMXJp+bT+fzmZFVut1n9/52sxbuzzYfT8+Xpu81qfX7+
      4fzit3fp+buL38/SD+/F78t3H347+/08/bd/+8//PLkqd69V9vBYn/zf1X+cnL07vfjHyR9l+ZCLk2mx
      +qf6iv7Wvai2mZSZileXJ3sp/qGi7V7/cbIt19lG/f+0WP9nWZ2sM1lX2XJfi5P6MZMnstzUz2klTjbq
      w7R41a7dvtqVUpw8Z7X6AVXz/8t9fbIR4kQhj6IS+tdXaaES4h8nu6p8ytYqSerHtFb/R5yky/JJaNPq
      eO1FWWcroa+ijbvrr/fw0W4n0uokK07SPNdkJuTh1y2+TE7md58X/3M5m5xM5yf3s7s/p9eT65P/czlX
      //4/J5e3182XLr8vvtzNTq6n86uby+m3+cnlzc2JomaXt4vpZK5d/zNdfDmZTf64nCnkTlHK17tvr26+
      X09v/2jA6bf7m6mK0gtO7j5rx7fJ7OqL+svlp+nNdPGjCf95uridzOf/VI6T27uTyZ+T28XJ/Iv2GFf2
      aXJyM738dDM5+az+dXn7Q+vm95Or6eXNP9R1zyZXi38oxeG/1Jeu7m7nk//+rnTqOyfXl98u/9AX0tCH
      fzY/7MvlYn6n4s7Uz5t/v1non/F5dvft5OZurq/85Pt8omJcLi41rdJQXfL8H4qbqAuc6eu+VP+7Wkzv
      brVPASr0Ynapr+N28sfN9I/J7dVEs3cNsLibqe9+n3fMP04uZ9O5Dnr3faHpO+1sivDd7e2k+U6b+jo9
      1LU0VzGZqYT4dtmIP9u58c+m/H+6mymnun2Sy+vr5H42+Tz962SXylrIk/q5PFFFr6izTSYqqQqPKvxl
      IVQm1LqIqUK9lfoPWpTV+m7VJa7cnGzTVVWeiJddWjSFUP0vq+VJWj3st8onT5ZCwaIJpO7ef/7bv6/V
      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
      AA==
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
