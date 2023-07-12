

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
  version = '0.0.29'
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
    :commit => "14de8bccb14ebff8fb793bd6459ad55841af6866",
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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
