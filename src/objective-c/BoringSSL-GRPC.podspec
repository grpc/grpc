

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
  version = '0.0.20'
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
    :commit => "fc44652a42b396e1645d5e72aba053349992136a",
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

  # The module map and umbrella header created automatically by Cocoapods don't work for C libraries
  # like this one. The following file, and a correct umbrella header, are created on the fly by the
  # `prepare_command` of this pod.
  s.module_map = 'src/include/openssl/BoringSSL.modulemap'

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

  s.prepare_command = <<-END_OF_COMMAND
    # Add a module map and an umbrella header
    cat > src/include/openssl/umbrella.h <<EOF
      #include "ssl.h"
      #include "crypto.h"
      #include "aes.h"
      /* The following macros are defined by base.h. The latter is the first file included by the    
         other headers. */    
      #if defined(OPENSSL_ARM) || defined(OPENSSL_AARCH64)    
      #  include "arm_arch.h"   
      #endif
      #include "asn1.h"
      #include "asn1_mac.h"
      #include "asn1t.h"
      #include "blowfish.h"
      #include "cast.h"
      #include "chacha.h"
      #include "cmac.h"
      #include "conf.h"
      #include "cpu.h"
      #include "curve25519.h"
      #include "des.h"
      #include "dtls1.h"
      #include "hkdf.h"
      #include "md4.h"
      #include "md5.h"
      #include "obj_mac.h"
      #include "objects.h"
      #include "opensslv.h"
      #include "ossl_typ.h"
      #include "pkcs12.h"
      #include "pkcs7.h"
      #include "pkcs8.h"
      #include "poly1305.h"
      #include "rand.h"
      #include "rc4.h"
      #include "ripemd.h"
      #include "safestack.h"
      #include "srtp.h"
      #include "x509.h"
      #include "x509v3.h"
    EOF
    cat > src/include/openssl/BoringSSL.modulemap <<EOF
      framework module openssl {
        umbrella header "umbrella.h"
        textual header "arm_arch.h"
        export *
        module * { export * }
      }
    EOF

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
    base64 -D <<EOF | gunzip > src/include/openssl/boringssl_prefix_symbols.h
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM77bT7
      vim20tG0Y/tISk9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLso/50sbg9W1e7Xd78v7Pt+pdfPlxepL9crN7/9iE7
      //DL5eYy+/UiXaXvLt+//+W33367OH//If23f/uv/zq7rvavdf741Jz93/V/nF28O7/6x9nvVfVYZGez
      cv2f8ivqWw9ZvcuFyGW8pjo7iOwfMtr+9R9nu2qTb+X/T8vNf1X12SYXTZ2vDk121jzl4kxU2+YlrbOz
      rfwwLV+Va3+o95XIzl7yRv6AWv//6tCcbbPsTCJPWZ2pX1+npUyIf5zt6+o538gkaZ7SRv6f7CxdVc+Z
      Mq1P115WTb7O1FW0cff99R4/2u+ztD7Ly7O0KBSZZ+L465afp2eL+0/L/5nMp2ezxdnD/P7P2c305uz/
      TBby3//nbHJ3o780+br8fD8/u5ktrm8nsy+Ls8nt7Zmk5pO75Wy6UK7/mS0/n82nv0/mErmXlPT17rvr
      2683s7vfNTj78nA7k1F6wdn9J+X4Mp1ff5Z/mXyc3c6W33T4T7Pl3XSx+E/pOLu7P5v+Ob1bni0+K49x
      ZR+nZ7ezycfb6dkn+a/J3TelWzxMr2eT23/I655Pr5f/kIrjf8kvXd/fLab//Cp18jtnN5Mvk9/VhWj6
      +E/9wz5Plot7GXcuf97i6+1S/YxP8/svZ7f3C3XlZ18XUxljspwoWqahvOTFPyQ3lRc4V9c9kf+7Xs7u
      75RPAjL0cj5R13E3/f129vv07nqq2HsNLO/n8rtfFx3zj7PJfLZQQe+/LhV9r5y6CN/f3U31d9rUV+kh
      r0VfxXQuE+LLRIs/2bnxn7r8f7yfS6e8fZLJzU3yMJ9+mv11tk9Fk4mz5qU6k0WvbPJtntVCFh5Z+Ksy
      k5nQqCImC/VOqD8oUd6ou1WVuGp7tkvXdXWW/dynpS6E8n95I87S+vGwkz5xtsoknOlA8u79z3/79428
      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
      urNSX1bVkRV6sbo7F+m+ZcWlfe6qrr6gz1zLNYMFwkjBRjsKs5FHYTZrRmE286MwmxWjMJvZUZiNchRm
      I47CaDM/OXbGjFcCm/l526jI/IxJxgpfizbCaNRGPRq1mRmN2mhHozbyaBSe+UkpakMyP+/7xwYs85NS
      ku1Vp3vlfGjmZ0xy1uUhnVOGMaGZnxHIOYHMTwJxrs03XLX5xpvgfrWQ+Uk2gW2Wz/wkW7D2ymZ+kg3d
      1qqEjmOMqi6jlCIab3vVa7n2h460MCmi5N9YiiiDMl78p4RNER02ACmiU4Y36dpMnCJKNmnaTJQiSrYo
      2kyYIjrZAKWIhhxjBCdL4hTR4b9AiuiUYUya74Cvf0Xds/WuuU5F16jWqC98Acp7/Vmj9N5Q3qt0Br7a
      TwzhnX6CTX1W/xSknXsKMtqYgg+rCQKmDPiZQis+U2jXPLdn55/b63TPGHbSM4Zn/fO757nnd8/Kuauz
      OHd11s5dnaW5q/e/67as3tze7mbm9Ufbff+5+FrHsfPmb6ZaI3f4xP9vYyq/2WS2rl47v/c/WZctLkDg
      pRL+yw6n5W8Bc+y8GakbHh/9B3M2h/49uaouFr8CR6nQ5v7U6AZs9B2LP9Ptoc7f08LVt3810SxOXuDY
      qfnLbWtmjyo7z48l1NeFKtHfjQAbfc17bp+StOxMm3VlXdk0y3PTdBnw6uKcIyrJvxb3tvxUo1Rka7Ym
      NVXefjRYjKOAU/9Luj1VBVYPdyY0NVlrTbo3GXA2xCS1/tUff2H640ekBJw4j9uufjdVai7NkzsPXctZ
      bI1RyZsfSlN1/TeKx30sUEnlutPHn5/G764rbmqQS+nSff9Su3+P3V3StUUFGqm80tqTaT+lNlmVVG7r
      zkddMZ6UrD4cQGf1pGQ9VSvO5RvMuxN9K0nSWe+ntZIEaSXJ6laSLGglyee0kmRpK0k+r5UkSCtJ1K0k
      mWklibqVJDOtJFnTShKmldSup/GR5lm+N9c+WAH0jXlasrfG6MQOFJzWdCql42RjesyaBjnZBT4qoe8o
      Kqph4HgjEMAZYJHPd8D71F/cOUV5r+KTDxxvPCLxehFInB/p5geyMsYEGT0+7M1f595dQ+tTiran3c74
      O0bXffXd7MXN9rFpUqpmzaCWXzOoHdf9uSYPAr8vHEvN7s/Mhx+AfWEG5b3Ndeo+7Vz1WVd7R00JkYQv
      qw9IarOfmiLurGT+ZXTWX4Ya4VQUAhHXr/Tpj+TP9C3r9qb90uczAVKG5uw+3UhnvpOctXLfYdKaQqkm
      OOd32xK/k9JPcM5v86zr9JVOcNb/o9Wqb+RotUmpGiMOOcaoGSNm4Yl7nz2ph5hYmLh9DNIKO4cTv0+P
      XuHn8Inf/duYBlrXY8oEpoNZvvLAADCOtOla2OMh6jo1iOTUEHoH9L9vu1Me6Ajddid8WVlgYZoBoA6b
      2rrtDPJBBoaYgK7ide+QTqvT4YApeoR6luf/X/cmdFMj54PbO6TR7/SOsB53r6ZQOYraTsuXlbrtTnjg
      3uq6d0j3dwO7U5VjmgGjvn25g47H708NNdRm/O6EP/v5E0DQ708MSCLwbfeR7/xX3N9jL1/dY8qMpvP9
      RxGfyWRQ6tXMZIacbHzVKl9lJ9DYGHTifU4z33MuF19RR4JaDh1iOHSE3uZ1ZQG+358Ycndrixj6/amh
      Pfi02gJYbIhSkQ24uo9EZGn7eVBQdIVCV4FZ6DfsOiWuv+X+DUgGhpjMpUvfT4DmChCH++2we2M78ICm
      GPGVRQNo3N6UrnY1grvdA35fbn0mYvUBHcYEIz7fQE82e0PO5IEhpio7+uUQKtu1mV+yDRCGKPXatMy+
      pIfSIteNCRXYcqBvOQDEUee28XPL7gxBvoMpFvuquh9bQn03jPiavAQ0bm9K34Z7Vd9kDHPu2wCyQnwn
      idWCjcpGrcrCv2w2+mWrm3anmIwLOda4ahrukYctUTMBJ+Csf9VU2CMPWyIyCRZgrA+Z/gow1gdOfMXk
      xNpkxqb5Nr8/VbJYGoKRs2ufk+FZlX50xYJyxhCWAo6fEyh0qWpA+PT+7u1WDNQuOJhz32tF5Z7Ao/ui
      jCS/iInkty1vBonIJxDn8m23b7roohIzCq6c5ql58utONAlewMjOmp9XmJ9Z83O/yp+fflVU+JTm7Ne1
      OHxmN+4e2XkztISbKHhQhj1mB3jJ+8cmttTl6+oQiHN1NfTTF4GRE54Uu4grBdy22Bxc7SjkJkb/BkJR
      vvkbq36WMDu81W3Z7Rff/8oGvpSzacvdB/RUpoAH/qb1i2P0M4rWplhWmigIyuinnLtLf22wmJ2ijNcX
      6q8M3QX2jij1+vGapEzLBvlpCLjIeL2mu+L25gJKp2jkvT7PYi6dqWwJDCoJeOR3ZcKLZjFo5D3U9bt1
      N7bvJi3cXa6/dwb1jCEq5XpLDlxIKfb7b/8D3Dbq90mgBAA=
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
