

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
  version = '0.0.28'
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
    :commit => "d473ae3587b9fa15f19f54da6243de3c53f67dfe",
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
      eVNspaOJY3tLSk9nXliUBNncoUiFoHzpX38AkhJxWQvkWnDVrpmOpe9bFADiRhD4r/86exCFqNJabM5W
      r6d/JKuyyooHKfNkX4lt9pI8inQjqv+Uj2dlcfax+XSxuDlbl7tdVv+/s80vv71PxfsPl7+tft+m5x+2
      579vP/yySX+9+OX9Rrxff3i//fW3zVb827/913+dXZX71yp7eKzP/u/6P84u3p1f/uPsj7J8yMXZrFj/
      p/qK/ta9qHaZlJmKV5dnByn+oaLtX/9xtis32Vb9/7TY/FdZnW0yWVfZ6lCLs/oxk2ey3NbPaSXOturD
      tHjVrv2h2pdSnD1ntfoBVfP/y0N9thXiTCGPohL611dpoRLiH2f7qnzKNipJ6se0Vv9HnKWr8klo0/p0
      7UVZZ2uhr6KNu++v9/jRfi/S6iwrztI812Qm5PHXLT9PzxZ3n5b/M5lPz2aLs/v53Z+z6+n12f+ZLNS/
      /8/Z5Pa6+dLk2/Lz3fzsera4upnMvi7OJjc3Z4qaT26Xs+lCu/5ntvx8Np/+MZkr5E5Ryte7b69uvl3P
      bv9owNnX+5uZitILzu4+acfX6fzqs/rL5OPsZrb83oT/NFveTheL/1SOs9u7s+mf09vl2eKz9hhX9nF6
      djObfLyZnn1S/5rcfte6xf30aja5+Ye67vn0avkPpTj+l/rS1d3tYvrPb0qnvnN2Pfk6+UNfSEMf/9n8
      sM+T5eJOxZ2rn7f4drPUP+PT/O7r2c3dQl/52bfFVMWYLCeaVmmoLnnxD8VN1QXO9XVP1P+ulrO7W+1T
      gAq9nE/0ddxO/7iZ/TG9vZpq9q4Blndz9d1vi475x9lkPlvooHfflpq+086mCN/d3k6b77Spr9NDXUtz
      FdO5Soivk0b8yc6N/2zK/8e7uXKq2yeZXF8n9/Ppp9lfZ/tU1kKe1c/lmSp6RZ1tM1FJVXhU4S8LoTKh
      1kVMFeqd1H/QoqzWd6suceX2bJeuq/JMvOzToimE6n9ZLc/S6uGwUz55thIKFk0gdff+57/9+0bd2YUA
      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
      fTG2a/9S5aXt57tjAPEM4e9/+z9q7ion6b4EAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
