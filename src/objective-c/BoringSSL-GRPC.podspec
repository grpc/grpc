

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
  version = '0.0.30'
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
    :commit => "529f1b459362fe89d581bb88d4b61d52b1d361d2",
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
      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
      ti8q39cvVV42yh4DiCcIf//b/wGrSQ1rl84EAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
