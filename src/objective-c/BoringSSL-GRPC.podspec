

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
    :commit => "ae72a4514c7afd150596b0a80947f3ca9b8363b5",
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
