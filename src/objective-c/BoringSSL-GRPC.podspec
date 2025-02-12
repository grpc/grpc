

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
    :commit => "9aab2f2a43f05a3c8dee685a94d1a5c816aac019",
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
                      'src/gen/crypto/err_data.cc'

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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9W3PbyJamfd+/wtF9Mx2xv70tueSt
      mrmSZbpKXbakluTqqrlBgESSxBYI0EhAh/r1XyYAEnlYK4G1UjOxo6cs4n1eIM/n/Mc/3m1EKeq0Edm7
      5evxH8myqvNyI2WR7Guxzl+SrUgzUf9dbt9V5btP3a/391/frardLm/+97uf03R5uj5Nf/qwfn+Wflid
      Z0J8PD9Lf/4pO0nPVucnH9N09f7k53/7t3/8491ltX+t8822eXf6/uT83cNWGMSLttlWtVTP6Ue/5itR
      SvVybans3zXq0Yt9ulL/3/DL3979LmqZq5c6/fv7d/9LP/Dvw0///p//RyNeq/bdLn19V1bNu1YKxcjl
      u3VeiHfiZSX2zbu81J+xL/K0XIl3z3mz7XwGyt8148+BUS2bVD2eKsFe/WttPvgubYaX1v9v2zR7+b//
      8Y/n5+e/p90b/72qN/8o+mflP75eXS6u7xf/n3rrQfW9LISU72rxo83rPjrSvXqrVbpU71qkz++q+l26
      qYX6ran0Wz/XeaNC7W/vZLVuntNaaEyWy6bOl21jBdrhHdWnmw+oYEvLd/9+cf/u6v7f3326uL+6/5uG
      /M/Vw6833x/e/c/F3d3F9cPV4v7dzd27y5vrz1cPVzfX6l9f3l1c//nut6vrz397J1SQKR/xotKK+gL1
      mrkOTpF1YXcvhPUK66p/JbkXq3ydr9SnlZs23Yh3m+pJ1KX6ond7Ue9yqaNVqhfMNKbIVUJLm+5P3nf9
      vUtXn27urq5/UYkoufj8Obm9W3y5+uPdPpWNkO+aZxVkmSgbZakSjAo+FYZVKf7+7qrRduqtdlL/QYPy
      RucCnahUFO/SVV3pj0vLLp2p/+WNeq160+4UT75bCiUWnZF697//239kKseUAnyd/5X+7d3yP8Gfkqvr
      68Vd/0CQYT6okuJ//Me7RP+f5b+NqqubZJ2o3Au/w/jH/g9/GwX/aTGkaKiUQTJyLj/dJ1napHMhh+dt
      Ql7mDYWgn7cJhSgpAPX4qP/88PU+WakUXTbJTqjyKZuL8pUOlYEDOVLUKvNwcJbSoerCMFm267XKMhw2
      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
      TfacxstdUe8DiCcI//7X/wAeHQ+Ar20FAA==
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
