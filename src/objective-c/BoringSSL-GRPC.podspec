

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
    :commit => "311e6f6d8e77da1f64c3256b30bd1992a555ce6c",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9XXPbuJaofT+/wnXm5kzVrpnYaafd
      751jK4kmju0tKT2duWFREmRzhyIVgvJH//oDkJSIj7VArgW/VbtmOpaeZ1EAiC+CwH/918mDKESV1mJ9
      snw9/iNZllVWPEiZJ7tKbLKX5FGka1H9p3w8KYuTj82n8/nNyarcbrP6/zt5f3oqPmw+rC/E77+v09PN
      h99W78/OPyzfv1uuT//44yw9Pz9fiQ+rf/u3//qvk6ty91plD4/1yf9d/cfJ2bvTi3+cfC7Lh1ycTIvV
      f6qv6G/di2qbSZmpeHV5spfiHyra7vUfJ9tynW3U/0+L9X+V1ck6k3WVLfe1OKkfM3kiy039nFbiZKM+
      TItX7drtq10pxclzVqsfUDX/v9zXJxshThTyKCqhf32VFioh/nGyq8qnbK2SpH5Ma/V/xEm6LJ+ENq2O
      116UdbYS+irauLv+eg8f7XYirU6y4iTNc01mQh5+3eLL5GR+92nxP5ezycl0fnI/u/tzej25Pvk/l3P1
      7/9zcnl73Xzp8vviy93s5Ho6v7q5nH6bn1ze3JwoanZ5u5hO5tr1P9PFl5PZ5PPlTCF3ilK+3n17dfP9
      enr7uQGn3+5vpipKLzi5+6Qd3yazqy/qL5cfpzfTxY8m/Kfp4nYyn/+ncpzc3p1M/pzcLk7mX7THuLKP
      k5Ob6eXHm8nJJ/Wvy9sfWje/n1xNL2/+oa57Nrla/EMpDv+lvnR1dzuf/PO70qnvnFxffrv8rC+koQ//
      bH7Yl8vF/E7FnamfN/9+s9A/49Ps7tvJzd1cX/nJ9/lExbhcXGpapaG65Pk/FDdRFzjT132p/ne1mN7d
      ap8CVOjF7FJfx+3k88308+T2aqLZuwZY3M3Ud7/PO+YfJ5ez6VwHvfu+0PSddjZF+O72dtJ8p019nR7q
      WpqrmMxUQny7bMSf7Nz4z6b8f7ybKae6fZLL6+vkfjb5NP3rZJfKWsiT+rk8UUWvqLNNJiqpCo8q/GUh
      VCbUuoipQr2V+g9alNX6btUlrtycbNNVVZ6Il11aNIVQ/S+r5UlaPey3yidPlkLBogmk7t7//Ld/X6s7
      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
      yLmGfFHYBAA=
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
