

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
  version = '0.0.38'
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
    :commit => "6b6d60bbef81887e7213078bf6df0594530697f8",
  }

  s.ios.deployment_target = '11.0'
  s.osx.deployment_target = '10.14'
  s.tvos.deployment_target = '13.0'
  s.watchos.deployment_target = '6.0'
  s.visionos.deployment_target = '1.0'

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
  s.compiler_flags = '-DOPENSSL_NO_ASM', '-w', '-DBORINGSSL_PREFIX=GRPC'
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
    ss.source_files = 'src/include/openssl/*.h',
                      'src/include/openssl/**/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'

    ss.resource_bundles = {
      s.module_name => 'src/PrivacyInfo.xcprivacy'
    }

    ss.source_files = 'src/ssl/*.{h,c,cc}',
                      'src/ssl/**/*.{h,c,cc}',
                      'src/crypto/*.{h,c,cc}',
                      'src/crypto/**/*.{h,c,cc,inc}',
                      # We have to include fiat because spake25519 depends on it
                      'src/third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'src/gen/crypto/err_data.c'

    ss.private_header_files = 'src/ssl/*.h',
                              'src/ssl/**/*.h',
                              'src/crypto/*.h',
                              'src/crypto/**/*.h',
                              'src/third_party/fiat/*.h'
    ss.exclude_files = 'src/**/*_test.*',
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
      5wDiCcL//et/aOeRPBVaBQA=
    EOF

    # PrivacyInfo.xcprivacy is not part of BoringSSL repo, inject it during pod installation
    base64 --decode $opts <<EOF | gunzip > src/PrivacyInfo.xcprivacy
      H4sICAAAAAAC/1ByaXZhY3lJbmZvLnhjcHJpdmFjeQCFUU1PwjAYPo9fUXtnr7uoMWMEN0iWEFykHDw2
      3Ss2dGvTNuD+vUWdk4hya54+n3nT6VujyB6tk7qd0CS+pgRboWvZbid0wxbjOzrNRulV8Ziz52pOjJLO
      k2rzsCxzQscAM2MUAhSsINWyXDMSPADmK0roq/fmHuBwOMT8yIqFbo5EB5XVBq3vlsFsHARx7WsaYj7d
      T+oEtJbCZ6Mo3WGXrdaVlXsuOma52IWWKRzh8PvClUP4xcu1Uig81gX3nHUG3beCW8s7+NO50A2X7UX6
      TAh0DutZVZ6xD4+oHxD9r+yFgea8DQXOMnPucattt5AKmWzQed6YFL4UF0OekDs9jIp+1Bxy85vkNk5O
      TWGYA/1BeqxHUvg4YDZ6B1ry6jZXAgAA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' -or -path '*.inc' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' -or -path '*.inc' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
