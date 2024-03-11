

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
    :commit => "ea96e1113da6ebfb917508d6d020960110e8ca65",
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
      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
      Wtk0gHiC8Pe//R/s4R73h9UEAA==
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
