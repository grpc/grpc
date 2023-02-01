

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
  version = '0.0.25'
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
    :commit => "487d3f153b0f5cf36ad4e2948c06f0bce76c100d",
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
    set -e
    # Add a module map and an umbrella header
    mkdir -p src/include/openssl
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
    case "$(uname)" in
      Darwin) opts="" ;;
           *) opts="--ignore-garbage" ;;
    esac
    base64 --decode $opts <<EOF | gunzip > src/include/openssl/boringssl_prefix_symbols.h
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydW3PbuJao3+dXuM68nKnaNRM7nbT7
      vCm2kmji2N6S3NOZFxYlUjJ3KFIhKF/61x+ApERc1gK5Fly1a6Zj6fsWBYC4EQT+67/OtmmRVnGdJmer
      19M/olVZZcVWiDzaV+kme4ke0zhJq/8Uj2dlcfap+XSxuDlbl7tdVv+/s98uf0/eb84/vF+923xYb95/
      jJPf0os/frtcv/u4ebdap79/XJ+/e5f827/913+dXZX71yrbPtZn/3f9H2cX784v/3H2pSy3eXo2K9b/
      Kb+ivnWfVrtMiEzGq8uzg0j/IaPtX/9xtiuTbCP/f1wk/1VWZ0km6ipbHer0rH7MxJkoN/VzXKVnG/lh
      XLwq1/5Q7UuRnj1ntfwBVfP/y0N9tknTM4k8plWqfn0VFzIh/nG2r8qnLJFJUj/Gtfw/6Vm8Kp9SZVqf
      rr0o62ydqqto4+776z1+tN+ncXWWFWdxnisyS8Xx1y2/Ts8Wd5+X/zOZT89mi7P7+d2fs+vp9dn/mSzk
      v//P2eT2uvnS5GH59W5+dj1bXN1MZt8XZ5ObmzNJzSe3y9l0oVz/M1t+PZtPv0zmErmTlPT17turm4fr
      2e2XBpx9v7+ZySi94Ozus3J8n86vvsq/TD7NbmbLH034z7Pl7XSx+E/pOLu9O5v+Ob1dni2+Ko92ZZ+m
      Zzezyaeb6dln+a/J7Q+lW9xPr2aTm3/I655Pr5b/kIrjf8kvXd3dLqb/fJA6+Z2z68n3yRd1IQ19/Gfz
      w75Olos7GXcuf97i4Wapfsbn+d33s5u7hbrys4fFVMaYLCeKlmkoL3nxD8lN5QXO1XVP5P+ulrO7W+WT
      gAy9nE/UddxOv9zMvkxvr6aKvWuA5d1cfvdh0TH/OJvMZwsV9O5hqeg75WyK8N3t7bT5Tpv6Kj3ktTRX
      MZ3LhPg+acSfzdz4z6b8f7qbS6e8faLJ9XV0P59+nv11to9FnYqz+rk8k0WvqLNNllZCFh5Z+MsilZlQ
      qyImC/VOqD8oUVaru1WVuHJztovXVXmWvuzjoimE8n9ZLc7ianvYSZ84W6USTptA8u79z3/790Te2UUK
      Xs7/jf9xtvoP8KNoJn/6vP2C16F/8Sw++/d/P4vU/1n9W0/N7qJNJGsZ+Br6P7Z/+EcP/IfhEGlNtXRI
      77le3iyidZ7JpIp2qawekrE6l7SsDB3oEWn1lFYcnUFaVlUXRqvDZiOLG8cN8GaEp/Pogp+yLg3YmVrU
      x05pl3bsISnhT4etLNN1tktVy0bzaqRjfZQtXJ4yxSbsuFmJgPz6kDzz55iqK7Iiq7M4P/6SKDl0NS81
      EK7q407n8+jLdBndzD6N9WuI65lPJwvZUhFVLWXa8jJOIvVl1eeSHUSK02Z789399FZ9oFKGUpHbXG+8
      n36PqrSLt5CdmNn43w+xgHmVlUF2izcjPFeybefqHRhyB1w+KOhjqD9eze5lfypKUrGusj3lRoFp0K5q
      rfggW58iSxh6HUf9K9WH4rkVinrX2V6OOgKuvBegMZJsm4o6IEYvQGOw3R7nz5eoiHcpU9zRXjv7qlsY
      de/il0g2JIJX3i0DHiUrQqP0BjRKQBZ4039fbQIyoKM99rIu12UeBUQ4GdAo1WYdkj5HHPU/xfmBK29Y
      3BxUbnxlJhNRLNs1hrkjMesqL9c/u/qOZ9cNYBRRy35qXCXcTDV4K8Ld9/soTpJoXe72VdpMEBE7qQMa
      IN6mSlPgm4IcERMBMWX5eEdPP4OErW/yQxAPEjFLWAGyBPFxkwVKleVfqhy8i9aPsazF12lVk8wuDvrP
      w/znQ/7mEyNH4nzLCAR6kIjt4PlqwgpzhGF3+lJXcViSOQ44kmh/JidAh7re9WMq68d9lT2puf+f6SvV
      7giAGG1/Vf62bVUe9uQIJg748zSutNQT5Ai2AIth5xMzkqPB4u3KJOWFUCRmLZtxFfPaO9h1p0W8ytOo
      XIu9ahT3uRzoU0NADjSSyLZF2tUCakJFAru9YIaEZWjsOhcq/4oiJXc3MYkba5MfxOPx1iX/MJMG7LJ9
      Jzsl45qaRlylXLbJ1rIWoFptHoug7heeW5E+K+9mtnkkwj6u4h3L3ZCYta1xGTW2hYP+9kYQtXpqRNdr
      NGJvqnTBUrco4j021VGeiZqlNwxwFPmn+JDL4WIsxLOsM1acQI5kZKzoINIqiev4TYKebHD09CXihupQ
      1Fukz7JJT9IXpvzEYxECW2pQAsfKik0ZreM8X8Xrn5w4hgCOIW/UvNwGRbEUcBw1CdXcvdwbyBDgMZqp
      FtaUBCZBYsmsC49lS5BYjN7akYONxWEneyPrnymv/Go47Gf2BDUU9v46ZOoh++OhTspnVpKbBjhK8ywl
      fqTOPDk0bO96TvJ+kUMcdt66Fjga8RkrgCLeXMharCsFqgpgZbZrgaPJ2yPbvAbVUpbCGydJ9/VjQJCG
      90bgZruGu/7maWj3jbxcx6x7EJS4sYpUjmrq3T6aL8iTHzoLmZ/pwmfXU6W78inlTm6YtGtXH0Txei1z
      mqrWUK832pZlEiBveH+EKi3SbVlnjMEVokHitdXU5pDnrDg9jvlX0WNGb8x0FjOXchy95mVyx/rN/GzW
      BQMxQjMa8CARm8FOk10i+5sXzFR44jRfXLFjtLjHr8YCAf4W9/i7SiYgxMmARGHfFJ47Qi1JTnnWFkW8
      sle5Ij6OM1HEK8JLpBhTIkVYiRRDJVKElUgxVCJFcIkUI0pk16vklZ8jDLnrd92S0WhfloxmxuSRCKy5
      QuGZK2w/O04OCZ76hCP+Y9+XPfcGW8Bo5+w0OvekkfzsUD1xap0T6vWypiVsHomQrh9ZAyQDRtzNkyue
      uUW9Xn6qaDwSgTV73ZOIVWTbON/yEqRj/WZ+kugCJEbY0x9AgcR5i/rgfGR9EMkBd/kcHYqfRfmsHqXv
      uzkvTibhMix2YLQxfpHmqmvMaTNtAxylXY/A0neox8vN/8F8bz4PnLjBPEjEZkI9LhLOegNHgMRoFw0w
      awEdR/xBT5rEiCdN2ndCCpZhQKKUu32excU6lV2qPFvz8sSWILEOVaUuSPUQuT/JVGBxZJHfdeWRF0UT
      wDGCnwOKcc8BxZs+BxTE54D697vbex/XjyIkru5BIpaiqdFlfdtMn/PS1pbAsdK4yl+bp5XdygxOkw5Y
      kGi8Z6rC90xVfbiJc5GqVTNV1/ymSdS97Ny0XpyAQ074SrZVGkssIC1NAxwl6KmrGH7qKsKfuooxT11F
      6FNXMfzUVbzFU1cx7qnr8Wsile3zpoq36hVkbixDgsQKfcIrxj3hFcwnvAJ9wtt8IsKKl84PR4jiahsa
      RTngSIV6RtimYlBfG/IMRRRRnDypJWQiTYLDWjIkNv/ZvBh6Nq++0CyCrFKxLwvBKnSGAInBe/4vfM//
      1YdqQ4xDnaoFNGkhuCFcCxKtX3zMeb0CtSDRxM9TrzrgxgU0eLzuJeXQeJYGiddtmMKJ0aKw99chWwdk
      j4aj/oA1J2LEmhMRtOZEDKw5aT9fl1XSv4cW0KIhKixurUbUZSF7sOIxvvjwMSo3+thR8C5hyIpdTTc+
      kH12WX8ddikvum2Box2bmH79MbP9AEVYzNC1RWLk2iL9e5l6hayoZXUaEq23+KOpCid5TLkrmzwqJC60
      gp/docZtePSs2KpXkMpKjpB2ze5ZghsaUCFxq3qvbvJNlqe8aLoAiVFX2Tp4Ss21wNG6RWbqtdCA5sK1
      YNHYpdNbGs35/ZCxMGxCo6pObNvOqxcIuR1+UDQ2Zkg3Bbf5o9dxfRChv/YkGROL10jYDm+kfr1lWDTD
      MzKieJN4whvtoCaXZP0TEOqoQOLIOjt5ZOkb0mcNK+amAo+TrvnXr1jcXImYK5ao1xucNLoDiVQdeM1Q
      A8JO/sMC31OCrhf6Bh0D2OSNylohLQZXSB/UxMKG6m0pwCbv4ft29P2N/kDQpIfs0WRxex4WolEMxlH9
      qcA4SgHHmS8mYQlmCEbEYCebaxkTjZt4rgWOFvCyqoUP+tkpZzuGI7WPxblpB5uGo75FPDySGvq1m6LW
      r9FjRn+SAErMWNOrr9G36Y+F2imBotc5xEh9ydoAEedjLKLksM+7rCqLTbYlLkMaciGRd3ElHuNcTexU
      r923BSsuaEKiEl800TnESG++LNT0dtvgRWqD6NPj0f5xMCXOgAqOqz15Xsd7NTzkhHQtcDRqkdY5zFju
      otVrTZvAcGnY3r6lT95CCsA9ft7UGqLwxGE/FMItnmj7NCDNFDzg1tsAERTIMA1Fbeeiw+K1Dk+kt5mO
      HKn0XEc7FmfHbHHUz1nNAuBeP2unAMyBR6K1oCaJW3dqb/eKutARNuBRQh4Y+Tx4xG6KJ882abMOj9o1
      G3L5Iu9SfqRd6jcT54IBHPcHZo43T1RHLrBysxR4HH6V0tOwPRPtozpuH0bn4QjEzqSGwb5mhT2v6uhQ
      rzekV2Ep0DghdbgYqsPFG9VOYnTt1D/94cbxlVARUAMJbw0kwmogMVQDCTmWyJNopd6NLLZ5qkbGrECA
      B45Yl/xe/ZH1m6NNWQVkNqCB49EHjCZpWunbEUC7EATsBOrdBTRgB1Dv7p9qG8p43041qIf6ssDWlHME
      fA43ktqivn3z5bD6V7quhcps2WGmPZPwm9yorH1GPXuMqo/U3Ngb/RSPyoqbqy+pTfi7ExtIkWx4wB3l
      ZWCAxgBFaeYGukcZqmOQ1/Q4rgOKVL/uU3ZaafCAm5lWtsGM0q4fesxIiXOCbJdabZU3y/eZu8oiCiuO
      Wj7WbklKcveY5QvZB3dgD1z6VQLXF7LH7cD+try9ZrF9Ztl7zHr2l2Vs7gLu6bI+1PVjVR62j+37aint
      +Q+Am/5EFtutOlExWldp88AhzlX/iDQ+QCVWrLI5YkkO1n6SfoTOWUbZWWG80Khhpq+dUT69N7CuX/ql
      3GpESwky5IIiN3PZbdeJlgMAjvrVm0qqJ0Ku+jGHFWn9yPsJGmcZA/dpHt6j+c32ZybszRy8L/OIPZnT
      qpLjBObBRg5suV/2ZdUsmVJt9E7e/pW87UkBQIMZhfrsxn1mczomVi0maw7XoPhc2rbX7/RX7Wll3qUB
      u/7YWXWLBDmCY4Ci8Bpq/47Szafqxm7WRZayT1pltDYbNiBR2E95YQMQRXvR67RdGT3HQQsQjf3sbOiZ
      GW+Xb2yH7/4ZU+ho2W/ConKfyY15Ftd/p+vkdKd2tOvZmOFAFRbXXkPHjOlogHjd21ZV+usgmyzZgBH3
      jUIlYKyQVzwQBRTnTZ5qkp5mbptNeei7g+qcY4y65UFE4RFzfcwVZRYKeNvXJVav9IPBABz1M3IQf5OD
      ucM/urt/2M7+Q7v6a59XclxU7pjyFgbc3XYl9CUoLu2x98cgsUP0CjxOf8g4M8pJAMZ4Sonddp3DjNQj
      uEzStR53MWE8rQFw1++MDKkRHAEQQw1HyF4FAS7680N07Yf2QfTXh3d/RIvl3XzarOTMkhdmCMAERmWt
      NPGvMOmOkdiJSBz2aoBGV2uw696Q75YNcJ/If2TiMaW7Os41sndnGTgPo/n4idyuSMT1nAahUZ6S7zED
      dt3sHV0GztAIPj9jxNkZwedmjDgzg3NeBnxWBvMcC/QMi2Yd1HEYQ98kFcA9fmaX0eaRCNzb2oAx9yHP
      Q5PIciCRmp0fatm9Es0EVzNkFqx4oAmJqoYncX2o0n6Qx4oJeKCIRaJm7Xh9RJMG7KyjwkwSsGovVZC9
      Gus3kxcWggI3Bn+3kKHTaZrt3ldZSXUqBjCx9hvxnW9z+kyoOYVinbLERxhw07skFdQnEela3TX9SQbN
      5BWvE+VzQZHb2WNjbwR6SEACxWrnd1gjTwNG3eqFWsa9b9KYnTO26kmftZlb56sbHPKzxsjoPJJ4jCs1
      i8Wb7jBp1M7YLdulITuv9sPrPaCxi5Jsm9K7wLhpXFTVPWcVII9rXGTWHYF4gIjc/V62/r1etHX48TaN
      xE/aOmkAB/zsh7MuDdsPRfaLPknak6BV26/j9BCIEQLSDMXjlGDX4EYJ2O578Iy2kPPZ/GezBZzL5j2T
      TfuQvkjQgUE3p81Bx83PjN7lM9i7fKb31Z6hvtqzrLJSdofSpE27emMk9Dko5nAjdSMpqrzDTF9WMN8B
      NkDHqW3JTJRqpGOVY32qTiGWR0SJrH1InhZxPErOmr6wWcfc9hCJyhZyXUCzrbau2QtqInhMZlTVFzns
      E+KcUU+ZtjxbVXH1Ss5+nbOM6ljK/nEbdeQE4IC/XUvVLpcTZL1Bm/ZdvM3Wp/mU0/aDNam8oBI7VrsF
      gloo0y6RoQWxaduuNs+WX1CLfKjTBw5surlniuLniRLfynPexlObKRuDe1KpcGnTvk9TUhdJfd82kNsV
      sE2Rffe1Ol+tmcjcl6LmLQH2aOB4soo+f9884joWZ/pLV0MuJ/JTlqTtJVJbUAc23e1WwrKMn351tMmz
      7WNNfQ7kFQExm5mzPH1Kc3KUHgW8bQeKJ9ZY01wRK43KqSeYh5miZ5dqH3DuKAC3/c0iKy031dyxoMUA
      FXYcYT+k/xfx7QZEYcbpNiTu10dSIjiw7VYHM8jIefuKEU1tsrZZrVvO/k7bbWiyPKsz2lQHbMCiBOQ2
      KrFjtfVclVJfBTFJ28o55xI74zLgfEvv2ZbNh9THIScIcAWdiTfmfMzmO8+cK36GrviclUfnSB5xztdE
      z9YMOVfTf6Zm8yn0HhM5BCQBYvXdYN4vsXggAv0ET/T0zpCTO/2ndjafPpYMpYIAF3lVO3byJ/fUT/zE
      z6DTPgdO+gw85XPwhM/w0z3HnOwpeOucBbbOuTkHs3mnrJldpl6vwQJm3hmg3vM/1Yf0mjyC6nHOIYzo
      yZ5Bp2AOnIAZcPql9+TLsFMvh068DD6HcsQZlO1XmteCeQXYgAE398zJgfMmw88oHHM+YfOd9iVI1Rq2
      R/CRg9gCKMamrGQOqenNZl5SxFtGHEACxKKvTEZ3NBLk1bYCWG2r/hY04qiHxhp105Zv8nhLNx9B18le
      zztw0qL6+F/Jz/Pz6LmsfsayY1OQ09jm3Qjs1bgDZysGn6s44kzF4PMUR5ylGHyO4ogzFDnnJ8JnJ4ac
      m+g/MzH0vMThsxKbb9QHsrQ+uB72C60DpwMyTwZETwUMPxFwzGmA4ScBjjkF8A1OABx1+t8bnPw36tQ/
      5ol/6Gl/p6P69O2k6W+kejRIPF52o6cKnj4MWXiOSpBYaq96Nd2xVi/NJ+m+zApeqkEiMCZzFeDQaYn8
      kxJ9pyS2n/WT+JzWxOahCG95FiLnHERBX0UtoFXUgrfeVWDrXcPPEhxzjmDzncc00fq59MfjqASKxSv/
      eMl/m5fkKacQvtEJhKNPHww6eXDg1MH2rEDG6BwZlYedXjjm5MK3Oe9v7Fl/2uFnarxGXm8M8WiEkHWv
      Yuy6VxG87lWMWPcaeO7c4JlzvPPmsLPmAs+ZGzxjjnu+HH62HPNcOfRMudDz5IbPkmOdI4ecIcc7Pw47
      O+5tzo0be2ZcyHlx/rPiBH2NsYDWGLPaaLh9JrcsQKui/sTY8U/ncCN5i1cHNt11WTcHLXFXx0G8GYF/
      fp/v7L7Ac/sGz+wLPK9v8Ky+oHP6Bs7oCz+fb8zZfOHn8o05ky/gPD7vWXyh5/ANn8EXehLe8Cl4wSfg
      jTj9Tq0sih7TPC+7/fq6NWzEMKDDjMSYVwZnkp9jWiKo79sG0T82irLiKc5pT/hBgRVDLawkORVgOJ4u
      3h+nCcjTWw7rmFlKxNXNMbKUBtublzcL3o93QNNJl0EW1g92QNOpzvuLVofNRhZ6hhnADf/TeXTOTlEX
      dt08KWbjprAL2+6LkFS48KfCBVOK2QJS4cKfCgFp4E0BjhA2Bfx25JcnF1mknc4y1mlhqI+y1ghAe292
      kXCu08JQH+U6AbT3yp7F1fzH/fIu+vTw+fN03gy028NLN4diPTbGgGYontql+g3inTSeeEma7psLY4c6
      GTxR1MsxxSHP2UGOAl+Mw46vP+w85v1BPLLVCva4xfh3jiDWYyZtrwrThn0xX97L798tp1dLdd/I//w8
      u5ly8nZINS4uKb89llHRiGXApzHjqXWps/uvpzpit6fe+ZgCi6NWoNcpL0DLoubDnqk97DGn/FPCkyoS
      s3IKrUujdlrRNEDMSS2AJolZqZWEjRreZlPS28n3KbsoIwZvFEbbjCl8cThtMqZA4nDaYoBG7MQbyQQR
      J+E1Z5vDjdQb04UxN+m2NDjEuC/3pCNIQBhx03oGBocbw25KXYDFIGxm54CIk1pJWaRrDbuhh+5lbhHG
      Sy+j4IJllltc8ZIqHrMNOb8byHWxstnK4cnVlRzWRdfTxdV8dt90vSg/GMG9/vEbjYCw102oX2Fas08X
      0dX3ydVoX/d907BeraO0WFev4497tTDLt1mdX1yylAZpWeuKazVI05qkZF2HmJ50veJcmoZZPoYL8pTs
      vCg9eSGaAwKaDyjvhQGo6+0CcrwaanoPxXMV76nKnsJs0T5OkvELqEDYdHOuE77KgGvEr3Bxex5Nbn9Q
      6scesTyfZstosVTfb48mJRltGHeTmgqAxc3b5iXMmivvcNzPV/uslObHRT3ew452kDoqwGMQus8A6vWG
      5KSAc/L7PbsIGijqpV6xBqJOcvHQSdt6d3czndySr/OEWb7p7cP36XyynF7Tk9RicfOWWMZM1OuNsqL+
      +FuAvRX4YxyCgxwGomTsBPLlKLXgmSjuFfz8FL78FKH5KYbzUwTnpxiRn3UZfbrlBmhgy/2ZeeN/Ru/8
      L9NbGe9m9r/T6+Xs+zSKk3+RzAA/EIHeJQENA1HI1RgkGIhBzAQXH/BTb1yAH4iwrwgLynDDQBRqRQHw
      wxGIC3IHNHA8bq/Dxb1+XrnCeiDmx8wyhfZEZpMP3FQxUdRLTA0dRJ3UVDBI23q7nH5RTxN3e5qz5xAj
      4QGhzSFGeh5pIOKkdus0DjcyOgAO7bEfwvQHnz/jJUeGpQa5rPYcYhTMHBNojomgHBMDOSbCckwM5Ri9
      m2aQlvX24eaGfqOdKMhGLFIdA5mohekIWa67T/89vVqqPfkIS/ZdEraS007jYCMx/U4UbKOmYY/Zvqvl
      tJ9sIzYfNuxzUxsSG/a56bll0z47NedM1mcm56IF+9zUCtaGLfe9/Pty8ulmyk1ySDAQg5jwLj7gpyY/
      wGMRAtLHmzLsNPGkBj8dgBRYTP/5ML29mnIeJFgsZuZaAeOSd5lL5ArbYtEmTZwkNKsF+9zrPI0LYn0K
      CeAY1FYArf+PHxDWR9kcbKRsqGdziJGXmgmWhuTbH68V+wdK79g//ASj7tOR8LtY/GSGMBxwpDwttuPf
      7nZJ2EqtwND6u/uAPiWlgx5nNP5cd4j1m6PNPkQucdhP7UmgfYj+g3dM4TvUGK1eo9vZNdPb0bg99O4Q
      o+4O+1tRLNZvEU154Ihy8Piw/HzJCdKhiJewe4rN4UbujX5kLfPy4zm3ujZR1EvsWegg6qSmgUHaVuaz
      nCX6LIf1AAd5asN8VIM+n2k+SLLNhq5TFGSjFxzkuQ7nYQ78BIf12AZ5VsN8QIM+lWE9ikGev4Q8dPE/
      aWk+ldXbNi3SqjlcJVE7n9EjuA4k0r4U2QvL35CIVQWMapa2RW3vj/speXRwhCAX/e45UpCN+hDgCEEu
      8v3TQZBLcK5LwNelzlxgyc4t28Pt7M/pfMF/nggJBmIQqzcXH/BTMw3g7QjLK1aDpnGIkd6sGSRm3e05
      d72LI356KdFAxJnxrjXDrpFcCnoOMdIbQINErNRqQeNwI6cxdHHH//mSXU2YLG4mFwONxK30wqCjlvfP
      2WIWMAPu4l4/MUFs2OumJotDW/Yk2xK2a9IQy9P2luo0enpPkmmcY6yjckU529DCLF9Wp7souchItiOE
      uCh7YTgg5iROBmkcaKRnsMaBxgPnAg/g1anDUjhZ0nKIkXx/6yDizC4SllJyiJF6J2scZOT9aOwXs34u
      8lvVJjCs+6QDMSfnPmk5yMjKDiQv9jGxh3iiIJvaVJtuUxRmi9b1C8+oSMh6KHi/ueUgI20/XJuzjLtV
      t8Mp+YmWQWLWgq8tAG/bfMn0/pt2R2ucZZS92V1WZ08pvZowUdt7qKO0pM10dwxgYrT2PWb56nh7QX11
      qGMAk8wsskkytind7fNmr05qJhikZn1YfpXA8kc0u/18F3WvJZPsqGEoCiFtEX4oAqVGxgRQjG/TH7Nr
      Zir1LG7mpMyRxK2s1DihvffTZDG7iq7ubuWQYDK7XdLKC0z77ONTA2J9ZkKKgLDmnt1F8X7fHHGW5Snl
      UAQANb2n07zWdZVTrAZoOfM0riLSKX0WBvnazXeZVg223GrDn+Yg+OYrJLOJWl5qcrqpKP/SDBebI4OI
      GxejAiRGsz9vtD3EVVzUacoKYzmASKocEiaRbM40JuXxzFKKr6dMW1puKBr5dZNXOyORHk4bkOXKCRt8
      nQDLUdFy0aonu79EcZ5TLYoxTc0KHsICI51xTeOPXOgJwLInW/auJSuymupRjGvaqUkIRhodOdi4H98x
      tDDXp/YkkuV1/EIjB3SdzDrdQjGvOqR3/JbsEOuaqad12JxjpP5w69c+pi/JYUcqzB1ielQGFaSy3BK2
      pSa3fEfGNKli2ByhVtBSSOdsY/1IrhZPEOCidPA0BjA1m6mRXjcBUMxLzA4DRJyJ7EhU5StL27GImXpD
      GCDilINwnlOBiLMiHP3ogIiTdKiCS7rWkt4j0TDTRyzsTjlXjcAqK6N9nFVE0YlzjYwOoIa5PlrfoiUA
      C+GsFJ0BTHuyZ+9aVJ24Omyoqg5zfaJc/0zJid5Stu2F6HmxDYfdKq3I96OGgT51R8k2hKHsSNPKGPiA
      Y559SSoQ8usWr5YNkApCS1iWuiI3K0fGMhEHOntnnEOt3N06nVp03DLTnukrinOqpoEAF2eWxwBtp6Dd
      rg1gOZ55V/WMXJPg1N0CrrkFsd4WTq0tyHW2AGpsdTLNjiaRgO2g164CrFtFmv4kWeT3bYPsBeaE09MN
      CHDJzGvOZaWWIgdG3GoosSfsOgzCiJvthZ3Usb4A50MEeT5EAPMhzd+oY/ATBLj2ZNHetVDnVgQ4tyK6
      KQ1i/0fDYF9abtRMwaEqONqedu0FYTGCzrim00wGuYT0pMdKnFsR3rmV/lOxT9dZnPPUHYy5yUMsC3W9
      nPkggc4HnQZz3VlnpIfsqMCK8Vge8iSSYypOStsw6CYXuR5DfMRHMzoHGukFQeNsY5uT8jOa8IRZvoLe
      Sz8ypqlOabP36vu2QTCahp4ybQd1QDrpd7WEaXmizuE9ufN3T5xEfoJT+ZkxuHsGR3fkQgmUxvbmJz62
      OUGQi9PtN0nNejP5Nr34dPHh42jbiYAs0eesIFRgFgcaZ5Ruh4mBvod9QpnXtUHNeRt9upndXrc7GBRP
      KaE/6qKwl3RrWRxs7I6PpSQBSKN2ZjJknlSgzHWamOG7Wv4VpeMP2ukJx0LMliPieAgvsvWEY6ElT0c4
      FlHHFfVqGsYwfZneXn1q1qIQVD0EuIhp3UOASz34i6stWddxgJGW9icGMAlSWTgxhun73e2yyRjKAlOb
      g43EbDA42EhLOh1DfaoyFTXlFV5UgMfYlFW0K5NDfhDcKJoCjkMrDDqG+qJczUklTG1HG/Z4JaJMRM9l
      RbFqlGlLSJbEockX0iGmR6wvVgXF0gCGY5UVNEcLmA75l4zkaADAQTw4xOYA4z6m2/axY1qvVqxr6znb
      mKRrmkoCtuORsJ7mCNiOPGX9sBNm+3b7jGaSgOFo1lwSFM33XQPlcA2dAUzE5qSHTBdhoc2tuTdB+29q
      nXFETA+tsXXa2HV5KFQF+xz9nValSjBB0jm0YZdlnFYbtYDpyJ4oguzJpqnpfERMz4GS28YbhPLfafEY
      F+s0iXZZnqtHzXFTyVXZTo5o6tdmkoSgH6Mz4/86xDmrg2KRpvWFkiby2wZNvAud+29TlTvZkSnqbblL
      q1eSyiAN63ZNKSry2yZ9fENY5UUakapzh7XMdVRt1u8/XHzsvnD+4f1Hkh4SDMS4ePfbZVAMJRiI8f7d
      7xdBMZRgIMZv7/4ISyslGIjx8fy334JiKMFAjMvzP8LSSgmcGIeP1As/fHSvlFjLHhHDI/sztPaiBQwH
      6VHhrf2U8FaND2Q7RhwF9ZDtKtJtrF5JpMmOlG0rSQOVFnAcBfFiJGA79uXzBU2iCMdCryU1CrZtYtlS
      qWcOPK2G235iAYfGmfJvqqNEsyjCsOQp7SZpvm8aSOfzngDAcU6WnBuWXVyJR9nDIK2YMjHLJ35Se7En
      xjSVCXFeoCMgS/TrkI1/59zmHCOt59URkOWi6QfRXS0HGZlCv4/VdYUFeAzi/e2wjrl5rCCol9xRmC1a
      5epli4RnPdKovUy45hIo+eR6pocQ1zlLdo7ZWPelwSLmADHi3R1yok4SkIU3aHJhx03sFBwRxyN+VUSN
      JCBLTde45U4cVlTNYQVZWEXixDlGRnXl1lL7jNaVaAHTQSuXdpmURYr6SzrE8NAe6NjPcYpCJg+FV993
      DdQ7oIdMlzrFmNaFOSKgh5rABucaKQc064xhog1C7BHIPlYtjur8RYdC7fVDag8B2rRz5+U8M3Ck3R2P
      33cNlOW0PWJ6RHpIyqiKSasRNAqzqf+zTXnOljXMxAt0rox1SZ5raf9MG1YanGmk9owqt1dUkXtEFdAb
      Eun6UKXECrSHLFdNfE7jnHve/Y0xbaJjjo82xyWAOS5Bn+MS0BwXrXdj92yIvRqnR0Przdg9GdUboaZB
      hxieuoysQ6AJRhcG3d3JhQxxR9pWVrfZ4AzjgTa5cLBnFg60B5AH+wnkgVYUDnZZeIrzQ0psx0+MYSJO
      iVnzYaevbA7Fus7KInok1EAgDdlFmm9o/QEX1bwPn6Pv0+/ddkyjlQbl2kiP1DTGNW2r8plqUgxsak8C
      4/ha0rVSWqsecT3q5cbqiZxoHWb6dumO8pT4RJgWUVdES0s4lnwd10SNQgAPYYVBjziegv6zCuh3FXla
      UD25/g721adPzdQqZcpZZ2BTtCrLnKNrQMRJOgrYJX3W6DmrH9Xmj3z9SYHEKdc1ea94VIDFyJL2eX5N
      2D0ANyBRDvyMOPhy4vAGWXEYygvSgN2AXJfYx+uU6mog13U4/0g1SQT0dOf2yQGv/Ohl/GSARwHGyVOG
      OYd++wW5NEkE9AT/dlcBxHl/Qfa+vwA9jDRUEOCi35EH6E6Uf2Rck4IA1yVZdAlZgjP1cjhPVT+aXC80
      kOkinhOrIaaH8hb88fuWISO+zGlAtkus4yqJ1o9ZntB8Gmg65X9k4/c46QnIQtn23qQsG2V/yRMAONrW
      SE15jN89E4RNN2U5z/H7riEi30U9ZdoIvc/u6yZPHHFoiOmhDJqP39cNi67zmVZqjiJJq/EyB4W8Wd3t
      Gv8YC8qcIG4Aoqi+mzpHjtT3c1nTrHYMjLNCdGuaXynVCUTb9v0rtUumU6aNVmcunDpz0b5eVrwSR0Mm
      hxujNE93hL0kMR6OoEpgaBTbAUTipAycKvRxogUiTu7vH/zdUbbb59k6ow/jcAcWiTbEsknEeuBrD4iX
      fPOeINeVx6ImdRoNzPWVezWHSVxPB8IDblYxdg1DUXhTCEOmoai8QgM53EikUe8JAT38QQKqAOPkKcOc
      p4Drgpyo1qj39Mfg3+4f9XZfoox6TwjoYaShPepdUBfrawjoUW9bqQULDN8RBb2M32qPprs/kytGqE4M
      GU1jBiBKUWe5HDBUgtwMa6jppY19Fs7YZ6GWjx+XuJzaynRL6+xjDidSsz2H1XknBoIUvji8n+MKzBik
      Md7CHuMt2l3d1ItzFMsJMl3tYiXtgO+IsgwaN0BRDvWaaT+SljVNf7bJTJrctkDTKX5me4pKfd8y1OOf
      bR6/bxsoz+h6QrNM58vZ59nVZDm9v7uZXc2mtLONMN4fgVCbgLTfTngmi+Ca//vkiryZiAEBLlIC6xDg
      ovxYjbFMpB2resKyUHapOgGWY07ZFrgnLAttfysN0Tx3t5+jPyc3D6Qztk3KsjW7naSClv82iDjzsttp
      mSU+0Za9rVTzjNCXMDHNN7+JrmeLZXR/Rz5BDWJxM6EQOiRupRQCF9W9P+6Xd9Gnh8+fp3P5jbsbYlKA
      uNdPunSIxuxxno8/yBJAMS9pNtEhMSs/mX0p3MzPy6aVZz7SmJ3SA7RBzMkuDp6S0GzopBYvsFNCNwxG
      EXVcZ+smt9WYIN6kgUFdIXYNtP1CIdYxf39YTv8iP74EWMRMGr7ZIOJUW2GRttSFaZ+d9gQVxhH/oQi7
      fo33R+D/Bl3gxJCd1R+yl0F9kAvBqJtRanQU9R6ajla0Uj9PMAMYDifSYjlZzq4CCyosGRGLk+WIxR+N
      X4gxzah4wb/PW7KXX+fTyfXsOlofqoryKAnGcX9zFEJ3XCw3iO7wRyoOu7TK1iGBOoU/zr5UE0lVSJxO
      4cRZr9bnF5dqwrV63VPzxYQxd1oEuDvYdW9W6uNzrt3CMf9lmH/w+oPsqPsxlv+LLt5RtUfONbY9EdW/
      j9IXTk8eMLhR6iogTQx4wK3+SXj6giucOJuy+ilviDpd11G2LcoqjXZx8hQ9Z/u0LJpP1Rap6n0Nyvw1
      R+5eG32oBI6RmqN3ecVARx3vdr1TCRyTW74exJy8+s2EB9ysMgUpsDi8+8KEB9whv8F/X3RfYnVtDRYz
      N2Pun+krz32kMbtsQsdvFAmgmJfy5MIGXac6uOm17Ye1B7Vy+0Iekzdqd+LqW4S1Vd647YWGBzU8YERe
      taeRmJV85jWCg/6maei2gMzKghHCMoBRmtSjnN8BsahZrdUMyGJbAcapH5uzDeV3CQ9OYNz1P8ZqhTR9
      /N2DjlOtXY3FjijsKNfWdgDJ/cYT5xibalW8CspuCwDqepvjGTeZOhY8i/NodaAso/c4nEh5tqri6pWT
      bzrqeHecWfYdPL/e/plziRrpWtMd4R1wA3Jcqnbi1Zwa6VoPu4gz33TiHGMZMior/aOyslhTK0aFOJ59
      mb+ev3/3gdeXsmjczihNBoubD7THuCDt2uVYSMiqYlW+sC7dwh1/lTDqsBZCXGqnqTrb5+kl5cRIj8KN
      k3IqmY4CbJt2Q3Y5WIlU8GYjU9KLIkMiPGZWrLlRJOp4uw1m+BWnKxgRI2sXSAWH6jxYxIPgxlAkYK2b
      d/NC+tigA4z0NuMXQRi/iLcbvwjK+EW80fhFjB6/CPb4RXjGL81huEnI1Ws0aA/s/YsxvX8R1vsXQ71/
      XicY6/92f29m+0SaMrUnHPVnmyh+irM8XuUpM4aucOLUuTiXbS+19Ttimm85j67nn77QzoMxKcBGmjHV
      IcB1PIGB7DuCgJPUcukQ4KIsINEYwKTeKCWUSRPTfI/xlRpVEiclDaq3XU8Xx2nW92NdOmOa0vXqPXWY
      YHOOkSlEfEl6oR6isaQW65jfB5jfe8wFPX+OjGkqmNdXoNemanjC9LKGgJ7oUKwfU8qxdSDsukvZzdrH
      VVaTL7UnNetX0l6x3dcNvrlSgqD5vmuI9ocVKQMszjSWu/1BdgqJvp7CbGpu7ZGQpxCMumknr4Gw4aa0
      bt3XDf50phAtGXUM9slSGO/SOq0EYUNUVGDFqN9FW5JTAa6D+ptbxPXsqZY94PhF/kUSATxV9sT5YUcO
      MJJvWh1zfb+opl+2Qx1Z9Psf53+QTp8CUMN7PDCkL3cEswsbbkK/rP22SRN3+9YQw9Mu/mf9Phs1vIJ+
      LwnoXhL0+0BA90EzWGzexqSZOsh0ZX9T6lf1dYOnLUo+AbqjSXVBOV9QZzTTbD69Wt7NfyyWc+rp7RCL
      m8cPaFwSt1JuIhfVvYv7m8mP5fSvJTENTA42Un67TsE20m82MMPXvfAS3U6+T6m/2WFxM+m3WyRupaWB
      jYJeZhKgv571w5HfzPu52C9tZhb3lAf6IKy5F5NoMSPWHhrjmlQbTzUpxjV1rTBV1mGuj5IVPeJ6mtaT
      amog1yUYqSWc1CJ1J7rvm4Z2YKZe+o/rQ0X6dRZqepMyRO3Sjl19QlQqxPE8pVW2eSWaWshyySb/+itJ
      1BCmhXo/uvciayhocYiRNxhEDXYU0nDwRAAW8i93erHHv+7Jnj1k+UX/XWZv+PRX6rDQBiEncWBocYDx
      F9n1y7FQH49ZGOgjL+yDWNMcMNwEacQuc49xSwM44j+s8mzN1p9o005sd502lz3QBVjQzEtVBwbdrBS1
      WdMsGHWbAOs2waiVBFgrCd6dKrA7ldqsu206aajffd80EAf7J8K00DsWQK+CMWmgQ71resWba7c53Bht
      sr3gahvYcDPGJyYF20riuXYQC5kpox+TwmxRxfNFFWoUTCP4i4mjNAeEnS+UHRkcEHISWiEDglykEaCF
      QT7BKjUCKTV1yS3bR9K2EsdZBgS4aFWihdk++oVBV6X+1h6pUaglvs0iyDyNf+rtO+c1QZ7dvbq/U2rE
      v52Sxkl2N82jL5+7M7Blj+px/CmqLulYi0zU+4uL33hmi0bsHz6G2E80aP87yP43Zp/fPdxHhIX/OgOY
      CJ0InQFMtEZZgwBXO4hv5wfKimw1ccxfVoR97wEU9rYbF27yeMtR9zRiX5ebeM1MkxOMuQ/VU6pKIE9+
      pL12ymw1giP+JN1ySmCPIl52MUFLSXtbEw7KcEnAquYiVq8hyewYkCj8cmLQgL1JMdIENoACXhF0X4qB
      +1J9zq+sDBqxN7uDqNfhZAss1DGVsnuwY0UCTUbUb9Mf3Tw7bexmgYiTNMo0OccoMzyTRandSixdV+O3
      sEQFbgxS+9gRjoXYNh4Rx8OZxgdQr5eT7Q4PRFBNclWSk7MHYSdjvg7BET95zg6mIXtzH1LvZYcFzWmx
      bqorwTCfWNhMm9hzScxKnohHcMefiajcx78O1FvwxDlGmZ8XhJcCTcqxHafMWU03LEBj8G8X73OD7juk
      aZUjAVnYPRmQByOQh2Ym6DjLdX1BT9WOAm0qpRk6hTm+9iECO0ltHPHTH8sgOOZnl17P85njN+RnjJv6
      iME+mR8cn8QcH7cP67CgmdsSCW9LJAJaIuFtiQS7JRKelqjpizM6KScONPJLrUXDdm4HxYQH3FG8UR/K
      vJYDrayISTPK43zOFdAeuRmQ4fo+XX69u263ycnSPInq1z2lAgR5I0K7pC5OKM3JiQFMzfuO1FGDjUJe
      0rzhiYFMhJMZDAhwJaucrJIMZDrQf589XqOvIjUgwNXM64XcPj7N6HjECZshFRA3U5MKNTlGi0E+EcVq
      fwi1FUpNL20mDvvLou3UcORHFjDvDvQSLRnAROtRA+uFT39tuoZq9ofsO5GAtfk7sdtkkah1vVoxrZJE
      rbQumUUCVvE2d7cYe3eLt7u7BeXubnt6u32VCpEmbxIb1yHx65JfHVi8EaEb2GTJRUE4dcUBQaeo5WcJ
      w9mChrM5S/SQ5XXW1T2UcubCmvv64sOH8z9Uz2wfZ+MnsU0M9R2nWMe/HYsK3BikZ/4a45qIz8QNSrfN
      7ifz5Q/yCzkOiDjHv5FiYYiP0sZYnGa8/TK7Jf7eHnE8qrC2iw6I8zQwDvrnIfY57m7OdTreaWmxlR8J
      YgRI4cSh5NuJcCxVupVVjToHO8+bGjlPa2oWgg4nkgjLUzGUpyIkTwWWp/N5tJj8OW1OUyCWbxc1vWrL
      rbSqyoo2j+GQPuuGr92Y3nZk2XxMcWoY5BOvsuDsuFqdNu3tz6AdQ2pzuDEquM6oMK3NjuvtR4Li1DnL
      eCjW7J/vwKa7edZCzaoThLiiXP2JI2xIn5V8YwG46y/Sl/5bzSay1BCuwYwi/8jOQpu1zKpl+TS745Q5
      mwXM6j+4Zo0FzPPJ7TVbrcOAu9lPqGTbTdz0N4fZkm+ZnsJs5JvGQr1e8m0D8UCEPBY1MzF61OvlJYvF
      D0fgJRAksWKVezVI3cXVT5K9xyxfpZb7NCFJxVrncGO0XnGlEvV4N3u2d7O3vAdOiTuAZa1KY1EW7IoZ
      wG3/rnxKm2MRU5q450Bjt/UlV6zjtl/U6pgbhlkDTaeIOWnQU5bt1KBTb1mTdK3Um/TIaKY/76PJdHLd
      nA8dE06Uc0DESTzdEmIRM2kcZIOIU3WMCCseXBTxUnbhdECPs32JI8mqdE05tWPIg0SkjPYtDjGW+5R3
      0Qr0OKNtXD8S1kwjPBJBpIT3y2zQ44zEOq5r5mXrAiRGHW9Jr7EBLGKm7PHugIBTPZ6n7bEFoIBXvY8n
      m5PqkVPT6TDi5qawxgLm9iUtZnrosOn+pF6tW5bfCMs2DMq0Xc3uv07nTaY2x7PSXhLDBGiMdbYn3uAO
      jLvpbZZL43bKugUXxb11lXO9EkW93V63lJ4mJkBj0FZnASxuJvYSLBT1NssS9ntalw5XoHGoPQcLxb1P
      jAoF4tEIvDocFKAxdmXCzV2Fol5iT8ckcWuWcK1ZglrVpujcItKwqFmEl3ExpoyrL4XUACfeGyG4PJoS
      byy1lTK/wtQMYJSg9nWgbeXmA57+ITWNv5YJytGBnGTWLGitwrv33fue3u2B+jrN3z5nBW0co2Goj7AD
      m0tC1hm1ATxRmI11iR0IOR9Ip5XZnGm8TteyBH2KRfrxN4pR50CjuusZQoVBPnLZ0TDIR83lnoJs9BzR
      OciY3JDrGQN0nKpHzEnEE4cbieXbQkEvI3uOGOrjXSZ4H3afsbK9By1ntk0F7Uc3BGShZ3SPob6/7j4z
      lZJErdRcMUjISi46JwqzsS4RLjfNRwvK6j2DwmzM/D6hmJeXlkcSszJuG4uFzFwrbvyTtjbS4nAjM7c0
      GHfzcqxncTM3fXXatE9vr+6up6xZEwtFvcRxtUla1oLVr9EwyEcuCxoG+aj531OQjZ7nOgcZGf0aA3Sc
      rH6NzuFGYr1voaCXkT1wv0b7gHeZYPvUfcbKdqxf8/X+27R9MkB93GuSmDVjOjPIyHkqbYCIkzHDb7OI
      OX3Zl1XNErco4qXWyAaIOH8mG5ZScpgx3fGM6Q4xcp/YgQIkBrFV0jnESH2ubYCIk/rU2QBRZ928rbzO
      9lla1Ey94fBGEmmR0KavQMGIGO2KBvW6DmubTJoWuR7qU3EDBJzfrj9Hj/Lmi3b0W0FjEXPGk4L19rfp
      92bnhJxxG2gsYuZcaYMhPn3XU+4VWw4sUr/7ADuQoQDj/GC3bxqLmYlPrw0QcbLaNmCHMv0j6lnKIIy4
      qc9kDRBxclrOjkOMnFbN3Q9J/4SziwjCYxHoO4nAOOJn1chH0HR+vw5Y6+LAoLu5EwVH3JG4lVY3fPes
      xzx+RqwXNAz1EUdSJglbq5RYJxgg6ExkH6AqOT++I0ErtU78jq1t/c5bgfodW3/afUDrgpwg2FU+cX6r
      wkAfseb7jqxS7f5OXl+hc6CRtd7BZmEzrx5CayDSNkUm5vjYNaWnluSkIpx66qXbdn8lhtKEHTfx2X9L
      OBZGyoFpxshTNz/vP00j0cwxUVQ9Zdm+XS0uL2Rb+4NkO1G2bfrjovmQZjtSrq2dTkqS83YIlRWbkqoG
      FEgc6jpOA0ScCa291znESG2fDBBxtvvVEjt/Lu2zVyKOyjjdR3m8SnN+HNODR2y+uNtuzokNJuYYiNRc
      UmCkzjEQibHCDXMMRRIiEnFeEwfMPo8n4ulkz5Bk1CVIrHYuhrjIzKURO7EHpHO4kTjvYqGIV7zRXSlG
      35Xym10lzK1pDMNgFFXmAsMoBR4nSpp7qYp327SgHV0waBob9dcbxv01FDldt19W04TskLpkRCx1Yaet
      toKDGjZPdMZsL8R7IqhbRpbi4JJjecZF3B9W6cv+LWK2poGoIe2wGNUOizdoh8Wodli8QTssRrXDQms/
      u9QO/GWGiRD1DbLP1Y2PH9IJwXUj4r9V4OGIwb0fMdz7iYUgLrjTMNQXXS8mTKdCcW+7qTNX3dK4fc6/
      6jl41atYpJyOWsdBRk6zgLQBlN2fNQY2cfb6h3HIr2aRQwKYPBAhSenzJxqHG8lzvQ4MutVBRQyrwlAf
      91JPLG5uXqJKaYsNIB6I0L3QSjZ3HG7kJYcOA27WTA0yS0M6TliHEFd0/ZWlkxxqZNSoRxBzMtsAjcXM
      c+7VzrGrPWem6TmapufcND3H0/Q8IE3PvWl6zk3Tc1+a1rlQ95la+ErbwdxrgaNFVfzMfdaOOXyRWM/c
      EQUQh9EZAfsh9DO0HBKwtp1xsrLFUB+vItdYwLzLZL+v2IZ0SlwFEIczdwjPG6qJv9CyDDh8kfhl2VUA
      cY6TN2T7EfQ4eWXGoCF7szNd8y16edFh3N3mDFfe0ri9yQ6uvIEBt+C2agJv1URAqya8rZrgtmoCb9XE
      m7RqYmSr1px8QHzubICQkzOLgMwhNANq1v13IkHr34xf7Dyzb/7MSj0k5YinWpkY4Hsiv5inYaiPlx8a
      i5urdK1eCeDKO3zQH/QLdIcZifWGKfJuKeetUvh90uNfiYv2NMz10V98wt5JZb7pib7jyXu7E3uvs/87
      MfUMEHLSUxB/P1Rtzd/unBbFeRaTuhM265oT8vv2PWXZ1E6xcSqi84vLaL1aR+IxblopkhyTjIwVZbu9
      7Htk1P1ERwl917DeRav8kNZlSXutE7eMjRZdvk286HIg4o68Syai8MWpq+hxFzfpf/HhIz+Y6fFE3K53
      7CiS9Zvl0KZImq0gQ2L0loFoIqDQd/xABHlHnF8ExWgMI6K8D47yHovyxwU/11sWMcuSFl7z2ZKRsYJr
      Pp/Qdw1vcMcCHk9Ebt51rN8ceMc6loFoIiCz/Hfs8Rv8O9YwjIjyPjgKdMeuH2P5v4t30b7MX8/fv/tA
      juIYgCiJvJI0Sd+H3b6gZWy0oBt40AhcRXHIc/5vNWjA/hKecS+DOXfqr9HcJwzx1RXLV1ewLyWclmFi
      sI9cAaK9lfaDcsO6PokBPtlAcvKjxRAfIz9aDPZx8qPFYB8nP+B+RPsBJz9azPV1rTrV12GIj54fHQb7
      GPnRYbCPkR9I36D9gJEfHWb6Vnn8M71YEXtJPWXaGC+Ugm+SqqaDWEI6xPUQc7JDAA9tgX6HgJ73DNF7
      2MRJpiOHGDkJ1nGgkXmJ7hWqrSBUE0+RHRnTpJ5Wt3NQq9ci3pEy1mY9Ztrzbgt1ve0MF++KddZjpl+x
      huLecvUvrleipvcxFk119hhXyXNckVLCZi3z/mfK7dDYLGJmNAU2C5iDurWwAYjSvn9CHlHbLGB+ac+u
      DgngKsw4u7iSf867YhXF+bassvqRlBOYA47EXOoA4IiftcDBpS17QtpsWn7d5j/Q+A8O34zgiJKGMU17
      +UvToPyGDVAUZl47MOhm5bPNmuZqfRH99o7aMPeUa2OoAM9vNIdV9qjlxi0zzdzBptkmstvda12p1xgO
      m032QlWjIifmxcVvRLkkXAut2oRqSfm395fUa5GEY/lAm99rCcgS0X9VR5k2NfWk5qGaxfi7mFRYbRY2
      d/WEelhfJRy9IYBjtJ8dvykOe7VNZMqKhqiwuM3Rm4w3zGCDFuWv5fT2enrdbK30sJh8IZ5qD+NeP+FB
      PQR73ZQVkyDd2z/P7hek18JPAOCICBvXGJDrOuRpRBmB2Jxl/HVIq9e+dW1OTT0IkhxWWHGaQ2PX5aEg
      PC92QMsp0uopW6vXT5JsHddlFcUb+a1oHY8fpA6KBmOu0o06vPYNgmomK+pTWgnCqaI605u+TG+n88lN
      dDv5Pl2QbnOXxKzjb26bw4yEW9oBYSfl3TebQ4yEXV1sDjFys8eTO+3rKqU6TvWWUIF4FL44T3F+CIjR
      4IifV8jQMsYtYp4S1ix6ZjkbErGKU+IX3PwzFb44/PwTnvxbPHxazqe84q2zuJleOHoStzKKiIb23q/f
      rkefFaO+a5JqY/K4SCiCDnE8dRWva6KoYTTT98nVaIP8rkly9tW0Ocw4vja2OchI2E/TgBAXYWGpzQFG
      yo1kQIBLzfuO323AwgAfZdG1AQEuwg2oM4CJtIukSVk20iLmnrAsM2oqzdwUIi5Y1hnLRFumrCGWh/LG
      xQnQHPPFQr0IH4+/k0+EZUkLqqUhLMtxI2rKRKADWk7+VDKCW37uBCYI2+4yf30vb1Y5yqhpXg0EnbtD
      zhBKqrfNFosH+dXoerZYRvd3s9slqZ5EcK9//D0Mwl43oe6D6d7+/Xr09KL8qsHRqrsTYDoold3x+6Zh
      WcmWX46TdxTNCTJdtMquJ3TLh/H4B4OjpucHNz0/ENPzg5OeHzjp+QFOzw/k9Pzgpud0+fXumvJSXE84
      lkNB9zRMb2oGNFd3t4vlfCJvpkW0fkzHH3MG0x47pZYCYY97fEEBUI+XUDtBrGaWn3ymJcGJsC3N3p/p
      uiZMmjkg6Kwrwgy8zdnGvBx/lFJPQJZolZV0k6JsGyU7j4DmmC4XV5P7abS4/yY7daTMdFHUSyjLNog6
      KT/cIWHrLFp9/E11SgmPETDeF6F955sfoeWxCNxMnHnycNbcFbJ3SeiWYjwWgVdIZmgZmXGLyMxXQkRg
      OojBdKC8nu+SmJX2qjnEaua75exqKr9KK2sGBdkIJUBjIBMl53Wod919+u9ovRIXhDV+GmJ5aJNcGmJ5
      djTHzuZJh7j0hGlJaL8ksX+F/I9EFdUsUQ8hBcVloah39Rqi7mjT3jzlkJ3fmCI9QaYrJx0d2xOWpaAW
      zpYwLfIPF+vViqLpENeTF1RNXrgWwupXDXE9gnw1wroaqaUmcYe4nvqlpnokYnoEOccFkONSS9V0iOsh
      5lWHaJ776a36ktqRIM7zflWCiNZlMXowOKBx460OWa72fWz3EhfUOBbu+okPSiwM8RHqXRODfRWp9XZJ
      wCrTOtuSjQ0F2PYHWRk3Z4iSlT3qejm/Gv69212d7ciulsJssgz/i2dUJGpNss2GqVWo632MxeP7C6qy
      pVxbFr+/WMf76J4qPIGAUz0waTZ4LcnWHgW8IpHdhJrsbDHIJ/bxOqX7Gsz15Y9yCJqnNdl4AmFn2dSs
      1ZajPbKgmXNDdhjoy2QVWtUMYwuCTsKAwqRg22EnBy7pTnCcRxY0V2ldZekTJz2PqNdLeS6F4IC/mdtS
      bapsUtu1j/SUARxupJ0sh+Wa6m4pzEZ6bg6ggDfdJfRGr6VcW1EyG+YT6Dr3pcheorqM6h3VqqGuVw43
      ORnUYa5PpGt1nAK/u+MI0Bi8omXAgLuu1rH8zo5cGnoStDLKV0uBNtWsM3QKA335Oq4ZPoUhvv0ry7d/
      BX0FP1MKX64UvGwpsHwpCIefWJjrU53BLfl2bynAtlN1QFMZkJU9CnjLvHwev2bdwjTf8ut0Tl1+bECQ
      i1QFGRRkIzQ7GgOZKN0bHdJc+7SAh+CjxagBj9K+AM4O0eG4v33fh+3vcNdPfEHAwlCf6hwynQrtvffT
      79FkcXvevM4x1mhAiIvy4NMBAeezLCEpWdhQmI11iSfStP714d0f0ez28x05IU3SZ6Ver0ub9tVrnQqW
      2SRNq/zP5k2ZVTx+PYbN2cafpAOFdcYyldGjvOjxrYYBmS71nFO9yXc1u5f1ZJPOFCuAm/59JYcNlD2t
      Dch0UcukWxKbvL7+Stsl3wEh52Jy377o/W38gBOmYXt0//CJsOE8gMJeblIcScA6vQpICh0G3dyEOJGA
      VZ0G/jvZ2FCI7ZJlu8Rs8uuzP5tXSak3KOaAIvESFk9VfinwloF50L02H7jX1OfNqmSu/AjDbm4qz333
      sWoiyUYFIa5o8vAXy6dAzHk1v+E5JYg559N/8pwSBJzE/gPcczj+ld/O6DDmDroHHAMehVteTRz3hySR
      pw1Snwe1Q7YAjRGSQL42SX3Oa5dOpMd6ybZe+qyB7RTiwSLyE96f6mGlZrDMzIPv3fmIezeoHbMFeIyQ
      XJgP1Q+sdu0Iepys9k2HfW5OO6fDPjenvdNh002ejADmIdqJBE5TZ5KglXujADjiZxRfm0XM7ASBW7X2
      Q26T5tKwnZ0cSEvWfkhuxjQM813yfJeoLyRhLcGIGBFhRZtXgsbiN8WoBIzFLDCe0hKSEd48mIfVJ/Oh
      +oTb5Lo0Ymen9txbW1Gb2Z7CbNQG1iRRK7FpNUnUSmxUTdJnjW6n/8M3KxqyEwepyEz/6c8BbTc+TtU+
      D7vnBkaqxpfYd4dvrGp8IyihfO16yHAVNuBRgpLJ286zhqwW6vNe8r2XXm9owo9o/4Gv8foAiMgbM7Qv
      MGpcrn01oIANlK7QjBrMo3l4fTUfU1+F9RX843PjO0G5MR+sFXl9B3iMbn7G60Pgo3Trc1ZfAh+nW5+z
      +hQDI3Xjc17fwjZoUeTtfX4R3X+aqtUgo80G5dhoL/AZkOOiLEXSEMejnlj/lHVmXCTROq3GL5bBeCdC
      s7UN0dowjqk7IZiwobEDms4PMqu+XX++iCibqzmgxxktvk7O2eKGtu37VXqhXlJXrzeQ1i4jOOhPiyC/
      jpv+36PVoUjyVNUYpKJmgIhTlb9so7Z3TXluXYDEqOLn8Di2xI5Fvbl/B+7t35tbk57MRwqyqZqTZzyS
      mJWfpJABihIWYcgeViwggx2Fsq9AT9gWtYooygTpVWiXRK2ks6whFjN3NUqa8OQnHPc/pXm55/s7HPOr
      vODKW9ZvnhTJNOwnuB4zojXYIddREO+PQGt6XNpvJ6yZRnDb37WqNGsH2a6uwNJcHWS7jjsXnm4Czgku
      I1R23HZPwzeI6hFpMe9uZlc/6EXTxEAfoSDqEOiiFDuDsm3/fJjcMH+tgaJe6q/WQNRJ/vU6aVvZe7kh
      uNdPTQ10RzfgY3Kq4Lu6dZ9/n9zfK5J+2RqJWTlpraOol3uxvmulp61Gatb53V8y2afzZVv9N+eZLGZ3
      t7TE8FrGRCMkkccxJhIl4XwSO1aXyvRk00DESU2cE4b4yEnQc71xPrm9jrq3dcbadMYyyb+k8StJ1CKW
      hzCrdfy+ZWheFyE5GgKytMeGqdOS1E6Q6tBBwvBkQGPFI27FojOWKd3SUlB+3zYU8SpPo01Z/YwOhYg3
      abQ6bDYpZdPLQZEVc5MRTzQyKcvWDlyLJNql9WNJSw+LBcziVdTpTv66ulI78sufF60Poi53sgdITKFh
      nRW/2ZBA/WxSmBNl2fbl+OOKToDtEOkhKRm3nQ5aTpGmtExTgOPglwHhLQO007E0RPNcjd6BW37V4JqL
      I4xVNETz6A+/KHvvOaDpPD7poip1zjD+b3T+7uI3tfWGOsMkip9eLghegDbs0f1iEd1P5pPvtJ46gKLe
      8a2/A6JOQg/AJU2reql4/3MtzmVtkxIO2IRY07zKxj+1OX7fMuTqWLRiG41/p9nCTF+z8basB/ek6+op
      yEa5E3XIdBHnaDTE9mziQ15T6zyHNK3EWR8NMT2bPN6Skr4BLAfxNnXvTf0sDsJxKQDq8VILmQPb7vpd
      tK7qiLa2CUABb0LWJZBltz+niyQEun5xXL8gV0oWpYBlE6/rsqInfMcBxuzXbk/WKQhwESuhIwOYCrKn
      ACz0Hwb9qr0Q3PLeo4D3F1n3y7HIu582GjQx0CfbZnUOKLVKMlnTnImo3Me/DqSb4ASZrtNJQ/T5cgRH
      /OSjimDatBO7TE4/SSUwvVXtKdPWHcHc9KCaxSDR3WR6H+22G1K959EMxVN9wvBwR8tQtOZpXmCs1jEq
      0sUbRLrAIxVlkXIjKBY2t13DNygNoGg4Jj+PXMvIaBdvEs3JqebMMl4t5cCgm1VD4WepNZ9SDos9AY6j
      uWzGaMJCYS9jHGChsLfp81bljjiJhBrwKHUZFqMufRFq6ilaIGy52/LCyVKDBK2cDDVI0BqQnZAAjcHK
      TBc3/YI/0hK+kZZgjiIEOooQjJ6/AHv+gtefFVh/lrIm7Ph919B04qltoAECzip+JuskY5v+TmmWv602
      Xxa7mj4d0lOm7bCnnJXXE6aFdpZPT0CWgE4mKABjcMqHhYJeYhnpqd5GWV9trqZW/6IdCtkTloVyLOQJ
      sBzkgyFNyrLRjobUEMNzcfEbQSG/bdPk9D0xjomYxkfE8ZBTpodM14ePFMmHjzZNT5sj45ioadMhjodT
      Bg0ON37Ky/VPwfW2tGOn5+UJMlzvLynlXH7bpsl5eWIcEzEvj4jjIadNDxmuD+cXBIn8tk1HtDulIyAL
      OZUNDjQSU1vHQB851U3QcXJ+MfxrGb8U/JWcOsLgHCMrzZz0mt1/nSy+RoQW60RolvvJt+lFdLX8i/T4
      y8JAH2Fa1KQc2+kJ1k5siUoddbxql9lUddfIWo3UrKSFavYatfbf1I22Taq3LecPi2W0vPs2vY2ubmbT
      22UzRUgY0+EGb5RVus0KddbPIS7GnxE0KCLEjEqZGtFOZk+8fbsLMKwjrqZKk3S3p5wPPULljSv/nonH
      t0h6yzQm6pv8XMflj0yorxDc6yfUXzDttasZDlFVgXekZoGjzRaLh+k85N43Dd4o3BzRcK9fFciQAA3v
      jcDM85722lXBTncBAVrBiBjBdSBu80ZX5XGX1rGauAsscLZqMG7A3eRa4GiSbf+DW9INARyjPY39NHd/
      TAJONESFxZVf0x53iHRdpTUvLGSCo6Yve/ntXVrU0dM5J5ghGI4hu267VWicRjIm1lO5rzbh0RoNHI9b
      EPHypy8X45h1Ho7ArGSN2vVhMZ23B6GTksDCQN/4UaMBgS7CTzWp3vbXxYcP56N32Gm/bdMqL/ZxVtEs
      R8qxdU+6mpu7q1yIZsCgRfnw7o8/36t3qtQGCu3SBsrhyhgPRlD74IREMHgwAuG9I5PCbFGcZ7HgOVsW
      NefZ+M0MABT1clN3MGXbTyPxM0QucdBPfHPKJUFrcpExjJICbZTaz8JA3zblFIBtWmM2yiZ3LglaswuO
      UVKgjVs28XLZFire7z6xoJm0lMfmcGO02XOlEgW9T816zIKh7UjH2p0s2HYoKTMNGO9EkBXCOaNwHTHI
      p17PKpK4Um8J1WmhJukEXQ9ZwGgy7Q4pw99wuDFalWXO1Tawx00v0QbrmFW4Lp9rynulCO74mxuUUe2e
      OMfYZyrrBrdxx6/qUnqr01GgjXcHaiRoZZc1E/a46YlrsI65XXjJ6DX1oONUsxDr+oUo7CjQxmnhTpxp
      jCY3X+7mlCNmTQq0JQeOLTnANuqtqWGgT72mwfApDPRlNcOW1aCLML40KdAmeL9UYL+0mcJLeEYJ2s7l
      cj779LCcypr0UBAT0WRxM2m/UhAecEer1+h2dh0UonOMiHT36b+DI0nHiEj1Sx0cSTrQSOQ6QidRK72u
      MFDU2741SJi2xXh/hHL1L9mchsRoDf4olMNbMR6NkHEvP8Ovmlwr6iRqlZXSeUiennh/hKA81QxWlGav
      osnDX/Qib5CYlZiNGocZqZmog5iT3Lu2UNs7u/3MSM8jBdmo6dgykImcfh1ku+Y39N0vXRKzUn9vz2FG
      8u/WQMApx5rvoip9Kn+mCdmrw7D7XI3eqHMODgy71accreIAI7XP3zGAKUnzVL24xbi8HoW82WZDN0oI
      dFE29rUwyHegp57bc1F/Zd2IyD3YtM+y56W2YSY7ddjjFmmVxTnb3uKYnzerBvFYhDwWNW3BJsZjEQp5
      ESEReh6LoN41iutDxQxwwmF/NJ/+efdtes2RH1nEzKkiOg43coZgLu73UwdeLu73r6uszta828p2eCLR
      R9oO7bET5yRtFjE3q7wqlrhFEW9YRTBYDwRWA4O1QH8XU59MwQYkCnH9MsQCZkY3Eewh7uJ6/UhWNRRg
      43Q14V4mY2BypDAb8ZmeAQLOZmQZcAtYPBYh4CaweCxCX4jjfFvyopiO4Ujkx3KoBI7VVVykXU4xHonA
      va+F976mvM5tQIiL+uDEACFnyegXKwhw0V6ltjDAR3up2sIs32n/6gW1qjVIzBow9404RkSidsEQBxqJ
      OqIzSNRKHt1hO6pbHzZH+nA6jbDCG4c8SeriXj9jihQSoDG4t4DvDqD2FZAd5a3PRHiuijG5KsJyVQzl
      qgjNVYHlKm/uEpu3ZM0wIrOLN3d33x7umymOA/2nOzRsX9dVzvEqDjZSdgi3OcRIzR2Ng42PsXiMkqzi
      WI8sbKYcD2hzsJFamnoM9onHQ52UzwVHemQtc7Nybnq7nM+m5P6BxWLmHwFdBEwyJha1k4BJxsSiPiLH
      JHgsapfERHEv+Q61WNzM6i4AvD8Co2kBDXiUjG333RPUusFEca9I2Zcr0trrDcpNMZibIjg3hTc3Z7fL
      6fx2csPKUA2G3M2jtaKuXunmE+r1sitP2zAYhVVt2obBKKwK0zZAUaiPMo8Q5Do+keRlrE6DdvpjSI0D
      jZw2Amkd2nSmPySwYcjNa3Ow1qZdUEV8LGCQiJWb8ScU8zZbbrPvaNswGIV1R9sGLErNfOoGCYZisH9I
      jT57a76ixgV0saIwW1TmCc+oSMjKabTgtorV80D6HGWR5lnBuJk7EHLSH5j0GOojHNnhkj4r9VmMDUNu
      Vh/O7b3J0j69at8HVG+o1LJOoi2lgARwjKYmVX/g+E8w6qavU7VY2JwlL9w5GtAAR6nSusrSpzQwFKAZ
      iEd/Igoa4CjtswtGBwHgrQj36lxqch/hREE2ap13hGzXwyfetfUcbCS+mqthqO9du6E0U9vRPjt5O3uP
      Ao6TsRIlQ9KEXAZOGOwTvDwTWJ6JoDwTeJ7N7+8WU+peBTqHGBnv0NssYia/l6WDHif9KbpD++wiTC/8
      flXxZwlX39J+e9D1nwSeGPTWwqE99oDE8aZMXR0E/6obGrHTq5ATZxnVXiW852EGiVmJNbHGYUZqbayD
      gLNZMh/XdUWWnkiflTPChQRDMagjXEgwFIM69QYJ4BjcJdsuPugnL3SEFUCc9qAgxkFAuAGI0k0Oskqs
      xkJm+rRij0E+YgvfMYDplPSszDNowM6q+JA6L2BlvYvD/vMo3cVZznF3KOzlFakj6HFyq0CLH4jAqQAt
      3heB3gFxccQfUPeZOOKXgyVOZdSjiJe/dhw0YFHaGQt6BxwSIDE461gtFjAzuj5gr4fT4YH7OvQJ0hOF
      2ajTozqIOjd7pnMDtR6hK7wRx3Ak+gpvTALH4t7Zwndni9B7TgzfcyLgnhPee468dvwIIS7y2nEdBJyM
      9dk95viat+T4bwxDAjwG+b07i0XMzPd+XRzzk3uhJw4xMvqLPYg4Q95bRRy+SOr183Ws9ty6pr5V4/H4
      IrZv7N4edqu04sfTLXg0dmGC3xK1PuV1ZyHFcBx6pxZSDMdhLRf3eAYicjrTgGEgCvVNUoBHImS8i8+w
      K6b38E4cYlSt5Bvc5K7GEy/4FrclVqzF7Au97j1CgIv8rOAIwa4dx7UDXMTS1SKAh1qqOsY2Le/m0+YU
      Js5TG4dG7fScNVDU27Qb5K0sAH4gwmOcFUEhlGAgxqGq1O7/a+LrG7hmXDzGy/Nekz8q/UEmJBiM0aQA
      sXOPWvzRRF1WaUigRuCPIZtD9biIuB8RJvHFOg8t6+fDZf08uMydjyhroT9k+Hf091pQBWRovPHSqioD
      Uq3lhyPIYde+fgyN01r80V7o7w6AhqEosuFrV62GhTpp0Hjkl8VMFPWSW3udRK37Q7Uvhdrn+FF2zLgX
      blnQaN2J9rlgxjnx/gghLYwYbmGar3QVqdqkff0zJJYh8sUMqWOOuN8fUFuKwdqyec0n3cSHPORHdIaB
      KPy668R7I4TUwmKwFhbB9aIYUS+q72zyeBtwL7a8N0JXMwTE6AzeKHW2CwmhcL+fvM4H4L0R2gngaL0K
      iHJyoJHeotIaV1/9nVYlM4BCQa+aZ2bWgUcU97KGXB2JWvOy/MkaUPcw6GaOpdFxtLYrNKc60HHcz22V
      B8Z87YBD5i3zyjvY4+b1V04sZuau+ocEaAz125iFW8dxf7OiKSDAkR+I0Az2kqAgrWIgTj8ZGhSr1+Dx
      2LNtGo3a2217uLnS0V47ewBvCtAYbfUXcmcbisE47LtcN6BRGM+FbXjAzes7bAf7DXkZq7aoLc2cJDIF
      YAze2BYb1zYLLLitTQ9j7pA6VQzVqSKwThWDdaoIr1PFmDpVvE2dKsbWqSKoThUDdao29pSlo34UzBiG
      wxOJN4L1j15DRnz+0Z4IanHEQIsjQlscMdziiPAWR4xpcURwiyNGtDhhI++hUXfIiNg/GhYhLaXwt5Sh
      o+zhETZjr08dtJztKdjUd/NOFGjj1I8GCVrJz9l7DPXRlyZaLGZmvCtnsaiZvurFYlEzvda2WNRMv48t
      FjRT3147UZbtzwnj5IsjBLiIDzj+hHZ1Un+k9lc7xjZN57PPP6L7yXzyvT01Zl/m2ZpW92GSwVh1vCLu
      6Yg4BiKdR48lsYjBCl8cVT1VjNsEk/hi0QukTfvs5MrUoYfs9KoVVgzG2adp9QaxjpqBeIzqF1YMxaF3
      zmHFUJzA0ozV/caXOI99IYEvBmMSHOB9EcjVsQX73Go+gC9X9JCd8bof4hiMFFYTnxSDcbJ9YJRsPyJG
      FIt1cBwlGYwVVoudFINxmqY7S0VgrKNmIF5oTSbG1GQivCYTY2oy9SVVNt8g1kkzFI8zxMYkQ7HIj7hB
      w2AU8nAAVvjiNJ1G1lAU11jx2G9Med6Uaj6q0ua1N8ZmtC4O+ZvEY+t12rWT35qB3+uK8ywW9G5qj4E+
      cjPbY5avWYXEmYvRQcepJqDjn8SJgx4DfeuYYVvHoIveh9A40EjuK/QY6CP2CY4Q4iK3/ToIO+lPQzzP
      QML2ChnaJ6T7nNH8GCRopTcBGmcbiVsuu7sty7+cFkeTm0AbBtwsJ+BivuWKvt3K2KsF3KeF+nas+1Zs
      U0PQJz16zPLJ/0qaidT29LFY/otxWCxqQaJxFtpYrG2mpgiQFs38RnyoH0s5hn7lPOACDf4osjqhzoCD
      Bn8URp6CBigK8z1q//vT7bxWWU82NScPjiRi/ZRuqO8ImSjkbfd4iFZZLWrGJRs45Ge/8Dn0LnfALkre
      HZTaD7u9Kbjl3OShCPVKqEuI8y3d3rOQ+ZAljDKtKNfGmVhC95BqPijXYk/XKcq1RdoWpVSnzgLm4xqL
      ZqFNXKUx2e8YhqJQj52CBCNiRGnxFBxHSYZikc/7Ag1jooT/pKPFE+3Ykw7JJs0BROK8r4G/vRb0ztrA
      m2qc/TPgfTMC9svw7pMRsD+Gd1+M0P0whvfB4O9/4dv3grvfBb7PxWlbuSRNmnbuIOJtypFbCixOszsj
      fWoW4IEI3HOQt94zkNWn/KTxpQi3k+npY/K7mL4eZrNKMU8LsrPjICN9RzN0n8JtyJ4kW/9eJGH7Hw7t
      fRi07+HAnofc/Q7xvQ7VNibsQrvzlNodv9ju8HK7U9MzUZz8i+Y8YZZPqyHI81kW6zGTDxqy4QE3+dgh
      SGDHoDVxzioBeUdnCf1JQo+BPvKThB6zfM2LC8fV+vQusYuj/gA36uVfMny11EUW7rqKfVyJNNpU5S5a
      HTYbYl3i0La9WfbWTkbTxBpoO8n7qUJ7qbL2UUX2UOUeLoWfK8XakRXZjbWbUWJMXhukZe2emjbL80hS
      HbSc7ZoMTptmkIiV0aaZKOQN2OF2eHfb4J1tR+xqy93XAN/NQAT0/oW39y+4/XSB99MFu58uPP105j7B
      6B7BQTv9DezwF7T38MC+w9w9h/H9hsl7DQP7DLP2GEb2F+7vruRA7IiaKOqlt3cWa5u17CJ3nm3Y5yZ3
      nx16yE7uQIMGJ8p+X1ZqN43TLAcxhsNbEVhjIWQkdPwztSujcbaxWbBEb9g1zjIy1v2AK34Yb6GB754d
      3xijbluicbix22VN1PLW23L1hsSMFde805N0Djcy5o0B3O8nzh8DuN9PPDEJwB0/8/wfk3SsnPNfNAz1
      8TLRe/KL9Tk9C72nvuifk6fpHdh0P73nrLPsKcfGW1VkgI6T8fynpzAboxg4sM9NLAQO7HNzngXBBjQK
      uaDZbG+OL7Loy/R2Op/cNKc7j7XanGmc3Ut4Pl0sKLoThLii2yuWTnKacZVFdSpb+1WcRIfiWa3JqtOd
      7PbE1ej22Svxx3quymIrOwjbTBCGgsMmIOo6L1dyzBRV5+/IcTTWaz4PMJ97zRcB5guv+X2A+b3X/FuA
      +Tev+UOA+YPPfMkXX/q8f/C9f/i88QtfHL/4zKs937zae80B17zyXvM6wLz2mpOMb04yrzngmhPvNYuA
      axa+a37Z7fhVqIL97vMQ9/mAO+jCz4euPOzSh679Ish+MWB/H2R/P2D/Lcj+24D9Q5D9g98elOwDqR6U
      6ANpHpTkAykelOAD6f0xxP3R7/49xP27330Z4r70u/8IcUM9iGagLbvN7a4cSVal6/q4CowcyycDYjdv
      NodFdBVAnLqKd+rxc5GS/T0KeLsRR5XWh6ogqw0at4s6Hj+pCcI+d7nnq0u9d5eK84vL7XonsqdI/iP6
      OXoJIoB6vVFarKOX8wB9Z0CiJOma5ZYcYkzXqybkKi/HL5rADVgU+flObKOX33ghTviQ/zLMf4n4fyYb
      llhyhvHiw0duObRRr5deDhEDEoVWDg0OMXLLIWLAonDKIYQP+S/D/JeIn1YODc4wRuu6atonwpoBCzN9
      j8/RerVWP6B63dcUpUm61rp6f3H8tM1bQdUDCieOLJmMK+8ox9aVRYZRI10rz4jY2r1b2kQhFgOXBu3H
      JOfZNdq0FyW/tNksZA4scagEiMUodToHGLlpgqdHQDmBeCQCs6xAvBGhqwAfm71iPpKO5oJp3B4kH3LL
      jv7r0/gnVBgPReg+ih7LqiA830B4I0KRRfJLjGJugpCTXtBNUHOK4ly9At0tgIjytNiO3xQLpi17UkZx
      siIpW8TyqA4CZdcBAwJcpBKrQ4CrSkmHYNocYBTxE12nINdVJipvSMuMANTyblNZ3uM8+ztNmgVOdRmN
      PyIYNzhR1Pb3ZbZOZUWXp+u6rIgxHB6IsMnSPIn2Nd19IgFrd0+0VdCmrJpROmGl0qDIipmJdhEiZWNb
      B7SddbqL1uVuJf9Cv/kc2rJX6aZ5vK+qumZ+qpnHoJydNaDB4qlGsyxSXpQOttwisKSKwZJav+67heVR
      LHOslDmW0mKABivKoV4z72eD7K2rND1EuzKRVadaZ6wuoKJsO4TxWoSs7GY2hexqUs8nhGnTvkki8Vge
      8mZWcPy6CwA1vWo/Llle1SJWlWzdBag/xUlC+gV+kxlVfUhPo55ybWp9vvxvqq7DNF8RxWqDkMNKVhuF
      qEnlBGBNc5JEz2U1focRnTFMq2wrG8ckiwuV1lQnQBv2dbl/JUt7yHAlsovKSUmDM4zpy16WKoKqBQzH
      JquFvJ3JP9LgTKN6L25XFvW23KXVayR2cZ5TzBBvRNjG9WNafSA4O8KwyIuv4mKbkn+6CZpO0XbB5d1E
      tlqo7a3SPK6zpzR/VT0EUgkCaMP+r3hdrjKCsAUMR77esUq3wZnGVIiofpS3plYY5hQ1KEBiULPLIg3r
      LsvzZtGQ7GyRhjYQ6zHLHgPpLCtUYMUoMnnLRc9ZMn70aXOmsUza80kZ5cNhQTM19wzOMcpqMlrFsntz
      wb5kSAHGUUWTXEW6sOPuemjv2tudHwb1YBHZSebwaARq/eewqFmk6yqtgwLoCidOLh6zjTqMlZlGDo9E
      CAzg8e8OeUjjjimcONx+p8OCZk59ceIc4+H8I/taDdYyy1uteEfyNYRpkYnNqiF1zjGqiYT4N6KuhWDX
      Jcd1CbgYuaBzjlGlKVGmENDD6LjaqOMl34BHxjFxSohbOkpZZormhWvV7SxXT1l5ELLXKTNsXwrZ4yBE
      GHSZkYtmvqOvWSiRbNYw78tnWq61gOGo1PifN96wUdfbtTnNd6hinTXNaXJYpzJp1iRnT2E2NYDa5zFX
      e8Itv8j+ZqSthpm+rqUlC3UOMB7Tu/kH2WvQkJ13ucDVinVc17RSf0RMTzOBSr4uHbN8NXuE4rCOWdRy
      PLRmXK2JOl6OEDD9qi5fomamuIgplb4J2k56a95DsOuS47oEXPTW3OAcI7W1PDGOiZyjR8Y2vbCz9AXN
      U0YPF+7dGm0iOfUA2rAfuJMCB3xG4MAdOBzwUcMzeaL1GZhpbVJXpUk/6UwxurRmL9VTSSFyVW9u2mdu
      j7t4LduJ+OLD6HcEBjT+eOGhRkb5MP7dHtzQR1lfZNFkcXsefZoto8VSKcbqARTwzm6X0y/TOVnacYDx
      7tN/T6+WZGGLab7VqhniqZnhYvQaXZNybYe1uIhWKVXXYYCv3rxnCTsONF4ybJemSa0GUH+NCLvS2pxu
      bE6kIueFTrk2cl4YGOAj54XJgcZLhk3Pi8dY/u+iOWz49fz9uw9RuSfkCEj77CId307DtGZXC8DKZjXY
      Olfj6bRQCz9GtzQY30dI1M1/daW2MrieLq7ms/vl7O52rB+mLTuv7kx8dWf/4fd7rvZIQta7u5vp5Jbu
      bDnAOL19+D6dT5bTa7K0RwFvt03G7H+n18vZ+B02MB6PwExlgwbss8kHpvlEQlZai5qgLerpk9uHmxuy
      TkGAi9Y6J1jr3H9wtZyy7y4dBtz38u/Lyacbesk6kT4r86ItHoiwmP7zYXp7NY0mtz/Ieh0G3UumdokY
      lx/PmSlxIiErp0JAaoHlj3uGS0KA6+F29ud0vmDXKRYPRVhesX58x4HGz5fcyz2hgPfP2WLGvw8M2rI/
      LL9KcPlDVmqf77pGmhQAEmAxvk1/zK559ga1vIe6vG+PsPk2/i0LlzStnyaL2VV0dXcrk2si6w9Sajiw
      6b6azpezz7Mr2Urf393MrmZTkh3ALf/8JrqeLZbR/R31yi3U9F5/3cdVvBMU4ZGBTRFhaaHNWcbZXLZ3
      d/Mf9JvDQm3v4v5m8mM5/WtJc54wx9clLlHXUZiNtGUagFrexYR3Sxmgx0nOeBv2ucdv+Q6xrvmwyrM1
      IyGOnGMkng5nUpiNkaQaiVrJidmDrnMx+0K1ScTxMKqhI2S6pleMqzpBtuteRUjrtBI0Xc85RtZNqHO4
      kVpebNZjppUZC7W9jJvlBCEu+k9H75T+I+qPxu6T6fXsfjJf/qBW6DpnGf9aTm+vp9eq9xQ9LCZfaF6H
      Nu2cPTsTdM9O+5MFV2n1XWaLxYMkmO2vS5v22+lycTW5n0aL+2+TK4rZJHHrjCudWc675Ux2IKefSb4j
      ZLrull+nc2q2nyDTdf/tajH+SUxPQBbq7d1ToI12Y58g1/U71fM74OD8uN/h33bJbwwA3O+nJ+Klp1Vo
      PlcTO382tZIac5L1Jj7oZ6WQqxiOw0gpxwBFYV0/csWca3SuSo1df5Cz7kRBtn8+TG54xiNpWed3f/1o
      BtxtyjZt4YL4yAOVQLHaq6HrW84ykjtOUK+J12XC+kuszhLSU+L1jrG+cUBl6KsH2VWgp/bjDEiR0eic
      O9Kf4yP9echIf+4f6c8DRvpz70h/zhzpz9GRvv4JJxl01mOmJ4KGOt7ofrGI5EBi8n1B1GokYCXXRXNk
      xmPOnvGYe2Y85twZjzk+4/GwkD3dputMEfaUaVOnJ1A86vuuIZrcfLmbUz0tBdmWy/ns08NySjceScj6
      8Bfd9/AXYGpaXY7uCEJO2YrTfRKCXPMbump+A5vI/WADRJzEe0znECPt/tIwwMfqkJmkz7rgaxeAlzpW
      PkGIK5reLuc/WMYWBbz0ilrDAN98+k+yTDKwiVfCjyDi5JTwjkOMjBLeYqDvz7tvtIVAOgcYidPdRwYw
      /Tmh116SAUycPIDTn5H2Rro/Nu+DHepU7bkX7eMkSZOoKPslv6P1gyYtqoijZqedXTr+FRQDMl3Nkc/R
      nv48AmB7c7qOvnzuXhOXv2as1MJgX7LKOT6Jwb5Nmqe77lDtV5nYHLnt8EXaHXJ+CAn73OJXxXdL2OdW
      rwCEpc/RAEfZVuVhH8k/Z+PPMcV4XwTKvhgw7bM3W3gdqvH79nkUcBx1BdG+SlWVwQmi83AEZglFy6Za
      IKz2pGBKG9ZnrtePfLWEcXdAMmu4x9+Mr8N+gu5wIsmboVZnu67LJFXvSeZxpXb7od7EmMaJJ7LdPm+O
      Ko5eZKNWVklWxDU15xELFi2wBkcs/mjM2hB0YJECakTA4I/y/1s7o95GeSwM3+8/2bspnW6/73JXq5VG
      Gu1K6ei7RTQ4CWoKDCZtZ3792iYJHPscwnvoXVV4npM42DEmvOyV4xYvma+lGIETfr6C/Yx3Y2+9m5C8
      onwnAyuabV74kdp/cv0vZQXimKnU1GvaaiKQaoSQyZBOpysx8vMV9MfVyM9X8IeE67XrPhhWNVvX5ubn
      qTiuKHc2kCrFzv91zlQrargGy3MVhnvqcfPAcUbXcJeyuHYCUzd6WjVliOm52tenML6HgR7wRaRgHb6B
      VdoBJd4VX9az39CXc9r3//7zP4hzghHf8KWJnU5eGcaEHu8TirGpph+zc45hY232sNAxnMmN0z4QOX8t
      7AvunNKMHe7kU4zznZ5x2emZMQ33rrvjH/ZdScGq+rTZWZ+fOU07ks9mRvWi42YleDyRJbRWmEfV5h1R
      XxhiOhT24FsuzDPy9v6Pr/nHa3m+pz639v2Ul12x6788AqWWS9nXcj4Pijn965gXktegXAQQz/2vX+Lu
      ZQxfk4A1hW+44RNeSUHqtH7xFPz+vjLUFGZovu8gN9knIOMMQ/2p9u3fGWtNCcsTA1PFLwtoFqJFgVAD
      HjdidNaLrhmx/K0K2HHIC+Zr4L1UUtyoE9aBVpUJhiVV1jecuGp1OTsCZxNTjPX1l4Fj/NayCj+nYeop
      ZgcUpM7h81e0CgGJ02fwNWFaF2Z1cFdmeVLh/EljE/YR4lxh8o0+PELAOb9qEp+wohmPiBQFXI2qfvuy
      qkYkYGtY6NksCcg5aS4vrqY8VwE7iRohzjUkQuK6geOMcLcmHGuETp5GiHMphrKIFKxrPnIhM1XYwR/Y
      +lFDVNG6w3qeLXbnJTekUMxS87COt76Tz3lmKn5KUy4zTl+F/4VB2eRvpqt2v5TTWdkRV7LuPD9/r/qD
      /0bbDo/Yeqmb9zovavtuOkXhRcrp6xius/3OHv6RF28f2TWLFDiXFBVCHTRpmoUFNzToUk4wuhnXulc8
      FczU8JmZq2pcBEKNYaoHTYw4+pYdPpOfkczWKpsT8Lw5USDUuBzDD6oCV/qG/XGVXepfq44k5igqs4eH
      uz8VlypiMHXiyycxODp9MNw+LGu5UWipj0CcK0TN4baAcT7/7Fxc5ynOZq0197guYJHPvd4ebrkLxLnw
      lhsxzge33JXibHjLjRj1hfVNsOEuDGOCm22kGBvaaFeIccFNNlKj7fBS7vBuT6nRVmXFisRHno7susRD
      BmW8YLZfzDFGLI8vwhgfllcUYVPfVpudyaCMF27JrdiS5aojqrxxRJX6dijn2qFUZoimJGfFMkRjjjFq
      elQ516PKVRmiEi9XULaykCF63Q5niKYkZ0V7RznXO9AMUQIxLnTMKqUxq9RniLIw44YzRFNyzqp80WKG
      6HUPTYYoC7PuH0rtD8EIZ4imJGfVDAjCKIBkiBKIcSkzRCWeq4BliMYca0QzRBmU8aoyRHk6sq/JEBUF
      Ug0oQ5RBqVed9snC1L0i7VPAI78u7ZNBqRdN+5wyvAm5yy/mIqMu7ZNBYy+c9hlhiQ9MG6OUZIPuJGbQ
      yKvJAEnAGSf8wcsZIOnm5Td8cmxqRjNAYi4xgrdUU0qyKZqUzb6ItsGNyWVfXDYBNxpPkMSjGIbStE//
      bzjtk0CxC0/7jLnEqOqEfNpnvAU9XuS0z2QrdsyIaZ/DRkVnYdI+yb/xty72FE3aZ8xFRkXaZ8xFRnXa
      J09TuybtM+Zk45NWGc1d9GmfPE3turTPlJSt37TSb5ETTfskEHXBaZ8Eoi4s7XMkOAvavbm0z8n/sY7N
      pH1e/v2Ieh4Zh+bNPfLvbZKn+a3eNRozo7hdB2/Q1DBbZeU7ufku1r2Dm6++rsq17+CsuF1n3TsZDEwV
      XRKrgN/0q1prLolV2knRWjNJrOM+qtcvvGLNa0xeFZzESinOhiaxpmRkXZvEOivhamFJrDEXGeFJLTej
      1U1npbmsaiIrzGJ1Zy7SecuKoX1uVFcP6DNjuWaxQFgp2GhXYTbyKsxmzSrMZn4VZrNiFWYzuwqzUa7C
      bMRVGG0SK8fOmPFGYJNYzxsVSawpyVjhsWgjrEZt1KtRm5nVqI12NWojr0bhSayUojYkifWyf2rAklgp
      xdnQJNaU5KzLo1OnDGNCk1gTkHMCSawE4lyb77hq8503wfNgIYmVbAL7GJ/ESrZg/YtNYiUb+merEjqO
      MaqmeFK2a7rtSa99YrzoygiT7Ur+jWW7MijjxYd+Ntv1ugHIdp0yvEnXZ9JsV7JJ02eSbFeyRdFn4mzX
      yQYo2zXmGCN4cSPNdr3+F8h2nTKMSfMZ8O2vaHu23TXjVDJGdUY98EUo7/VHjdJ7Rnmv0hn5Gn8hB5+k
      E2zqs/pfLdq5Xy1a5e/zrPj7PLvmN3B2/jdwve73er30e7035fWaN/F6zZv2es2bdL3m5V9NV9V7t7eb
      wD/97Pof74vHC46dN39fnvwh4BP//1pT+82msE391Pu9/130xeICAi9V+Ks4npbfscux82akbXh89L+W
      X/PnY7N9yUv3jvztc2ZxDgHHTs0P562FfVXZeX6s0AwPpURHtwgbfe3L1t5ledWbruirprZ5sd2ati+A
      2+vmHEklfxvHfvmHSanE1j6b3NTb7leLhTEKOPU/hrsR/U21pgwfBmJP4NjdFp01+cEUwPGRktT6R3hH
      pQnvCJEScOJ8fe6bF1Pn5qO9c0dmVS++gZRBJe/2WJm6D58xHoexQCXVdc1XvZlxZ+vevul1hXmXVNkd
      yr6vhP119aYGuUqfH8JN4P6+bzeAa0tFGqleZe3JdJ/yObIqqW7neoKujCclq++6OqsnJeupXtGLzjDv
      zvT9M8tnvZ/WPzOkf2af2D8zqH9mq/tntqB/Zp/TP7Ol/TP7vP6ZIf0zU/fPbKZ/Zur+mc30z2xN/8zS
      /ukjq/wn8+I2hayV59NuZ/xc2k07/PRocaHbpklVzRNbOv6JLd341JUhPw3oERxLze7Pwt9YDc5YGJT3
      tsOFvLx3zWdd671qKiQSvlYIX+mKd02JCyuZfxud9behRvjWawJRV3jNmpAcFqbuFRE8As76XYuvrREr
      SJ3f+d2X7Gu+L/qD6R5CPg5QgqE5u0+X0ZkvJGet3XGede7URacmOOd32zK/k9JPcM5vt0Xf6xud4Kz/
      Z6dVn8nRat3JuWYlMOYYo2YlkIUn7kNxp14+YWHi9jE0K+wcTvw+33iFn8MnfvdvY1royRNTJjIdzfJs
      /CvAOPJqB2scw5navsNVDqKuU4tITi2hd8D87Lw75TuDtKrfnfBVbYGHsFwB6rC5bbreIG/kyhATkHU+
      7B3TeX06HjFFQKhnedb9sDeh2wY5HtzeMY1+pheE9bi5vELlKGo7LX+E0nl3wluDHGLW9DEdHpWxO9Vb
      THPFqO9Q7aDX4/enhgbqM353wr/5dXxAEPYnBiRF9rz7yL+5L0XN9aqYk41PWuWT7AQOZQadeO/zwp8F
      VIvHq5GglmOPGI49oZ+3TW0BPuxPDNu2OSKGsD81dEefH1oCj62hVGIDxs6RSCxduNoFigYodpWYhX7C
      7ivfzYvcvwHJlSEm89HnLydAMwDE4UZme3CnZeALmmLEV5UtoHF7U7reNQjudo/4Q/Xss+PqX9DLmGDE
      5zvoyRZ75Ei+MsRUF68+oL62fVf4h38BwhilXptXxUN+rCwybkyoyLYFZm5XgDiarW399U13hCCfwRRL
      fXUT1slQ3xkjvnZbARq3N6WbY7X9pfwkU5hzvxZtW9V7hfhCEqsFO5VNepWFv9ls8s3WtN1OcREm5ljj
      qssvtzxsxXUXXm6K2JqaSy4CzvpXXfy45WErIpc9Ioz1IRc8Ioz1gZc6UnJibQtj8+3z9vJrisXSGEyc
      fXefXX+jEVZeLChnDHEV8PoDgWKXqgWEd+/Px85loH7BwZz70ioq9wQe3R/KuOgPMS36vGVvkPhyAnEu
      33dD10UfLTCj4Oq0d+2df/pAm+EFRnbWfL/CfM+a78Mz6go3uVA0+JTm7MMTGXyeMu4e2Xkz9CAvUXCj
      hn0tjvAD22+b2KrLn65CIM7VN9BXXwImTvii4oeY4n7eYrfgM29ibmJ8+PLnX/fht3hhRWgYYWz4Peti
      +4yDVsrLau9PG8PlzeK4b7qqP7widXgDX+V8CRL53aOAR/62849ICNd+rc2xjCtRENUIPw7oP8IoZDE7
      RRmvL+rHoP4D9o4o9frVqKzKqxb5Eoq4xDh8e7hyB/MBSqdo4g2Dr18OMbWtgCUzAU/9Tb0bzttf/dP0
      DFwg5pMK7l3Bj4Fi0MR7bJoXmx+rF5OXtQ2vAdQzhr//7f8dW55ij7gEAA==
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
