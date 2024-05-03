

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
  version = '0.0.34'
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
    :commit => "5a2bca2124800f2861263959b72bc35cdf18949b",
  }

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
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
                      'src/crypto/**/*.{h,c,cc}',
                      # We have to include fiat because spake25519 depends on it
                      'src/third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'src/gen/crypto/err_data.c'

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnY6WSn
      3zvFVjqaOLZHUvp0zg2LkiCLOxSpEJRj968/AEmJ+FgL5FrwW7VrpmPpeRYFgPgiCPzXf108ikJUaS02
      F6uX8z+SVVllxaOUeXKoxDZ7TnYi3YjqP+XuoiwuPjafLha3F+tyv8/q/+/iXXq1WqdXl1e/fXjzZnv1
      4f3l1fu3v7/7ffVP9fe379ab7eWH33/7ffVv//Zf/3VxXR5equxxV1/87/V/XFy9ufzwj4s/yvIxFxez
      Yv2f6iv6Ww+i2mdSZipeXV4cpfiHinZ4+cfFvtxkW/X/02LzX2V1sclkXWWrYy0u6l0mL2S5rX+llbjY
      qg/T4kW7DsfqUEpx8Sur1Q+omv9fHuuLrRAXCtmJSuhfX6WFSoh/XByq8inbqCSpd2mt/o+4SFflk9Cm
      9fnai7LO1kJfRRv30F/v6aPDQaTVRVZcpHmuyUzI069bfp5eLO4/Lf/PZD69mC0uHub3f85upjcX/2uy
      UP/+XxeTu5vmS5Nvy8/384ub2eL6djL7uriY3N5eKGo+uVvOpgvt+j+z5eeL+fSPyVwh94pSvt59d337
      7WZ290cDzr4+3M5UlF5wcf9JO75O59ef1V8mH2e3s+X3Jvyn2fJuulj8p3Jc3N1fTP+c3i0vFp+1x7iy
      j9OL29nk4+304pP61+Tuu9YtHqbXs8ntP9R1z6fXy38oxem/1Jeu7+8W0//5pnTqOxc3k6+TP/SFNPTp
      n80P+zxZLu5V3Ln6eYtvt0v9Mz7N779e3N4v9JVffFtMVYzJcqJplYbqkhf/UNxUXeBcX/dE/e96Obu/
      0z4FqNDL+URfx930j9vZH9O766lm7xtgeT9X3/226Jh/XEzms4UOev9tqel77WyK8P3d3bT5Tpv6Oj3U
      tTRXMZ2rhPg6acSf7Nz4z6b8f7yfK6e6fZLJzU3yMJ9+mv11cUhlLeRF/au8UEWvqLNtJiqpCo8q/GUh
      VCbUuoipQr2X+g9alNX6btUlrtxe7NN1VV6I50NaNIVQ/S+r5UVaPR73yicvVkLBogmk7t7//Ld/36g7
      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
      CcLf//Z/2aoCAcHMBAA=
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
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
