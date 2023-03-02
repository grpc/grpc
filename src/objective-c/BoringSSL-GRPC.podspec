

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
  version = '0.0.26'
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
    :commit => "85db207a482ae4f91f83a6a70d432b9121e48d2d",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydW3PbuJao3+dXuM68nKnaNRM7Sbf7
      vCm2kmji2N6S3NOZFxYlUjJ3KFIhKF/61x+ApERc1gK5Fly1a6Zj6fsWBYC4EQT+67/OtmmRVnGdJmer
      19M/olVZZcVWiDzaV+kme4ke0zhJq/8Uj2dlcfap+XSxuDlbl7tdVv+/s8uPyeri3e/xh8uLOP2w+eN8
      c/k+/i3+/V3y4f3F6o/zi/P0w2Vykfzbv/3Xf51dlfvXKts+1mf/d/0fZxfvzi//cfalLLd5ejYr1v8p
      v6K+dZ9Wu0yITMary7ODSP8ho+1f/3G2K5NsI/9/XCT/VVZnSSbqKlsd6vSsfszEmSg39XNcpWcb+WFc
      vCrX/lDtS5GePWe1/AFV8//LQ322SdMziTymVap+fRUXMiH+cbavyqcskUlSP8a1/D/pWbwqn1JlWp+u
      vSjrbJ2qq2jj7vvrPX6036dxdZYVZ3GeKzJLxfHXLb9OzxZ3n5f/M5lPz2aLs/v53Z+z6+n12f+ZLOS/
      /8/Z5Pa6+dLkYfn1bn52PVtc3Uxm3xdnk5ubM0nNJ7fL2XShXP8zW349m0+/TOYSuZOU9PXu26ubh+vZ
      7ZcGnH2/v5nJKL3g7O6zcnyfzq++yr9MPs1uZssfTfjPs+XtdLH4T+k4u707m/45vV2eLb4qj3Zln6Zn
      N7PJp5vp2Wf5r8ntD6Vb3E+vZpObf8jrnk+vlv+QiuN/yS9d3d0upv98kDr5nbPryffJF3UhDX38Z/PD
      vk6WizsZdy5/3uLhZql+xuf53fezm7uFuvKzh8VUxpgsJ4qWaSgvefEPyU3lBc7VdU/k/66Ws7tb5ZOA
      DL2cT9R13E6/3My+TG+vpoq9a4Dl3Vx+92HRMf84m8xnCxX07mGp6DvlbIrw3e3ttPlOm/oqPeS1NFcx
      ncuE+D5pxJ/N3PjPpvx/uptLp7x9osn1dXQ/n36e/XW2j0WdirP6uTyTRa+os02WVkIWHln4yyKVmVCr
      IiYL9U6oPyhRVqu7VZW4cnO2i9dVeZa+7OOiKYTyf1ktzuJqe9hJnzhbpRJOm0Dy7v3Pf/v3RN7ZRQpe
      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
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
