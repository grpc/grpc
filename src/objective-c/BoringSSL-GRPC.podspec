

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
  version = '0.0.16'
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
    :commit => "067cfd92f4d7da0edfa073b096d090b98a83b860",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nbT7
      vim20tG0Y/tIck9nXliUSNk8oUiFoOy4f/0FSIrEx94g94arTs10LK21KRDfBIH/+q+zx7RIq7hOk7PN
      a/+PaFNWWfEoRB4dqnSX/Yye0jhJq/8UT2dlcfap+XS1ujnblvt9Vv+/s3cff93ukt8udr8kvybxuzTZ
      xe9+fb9599vH5N1v7za/XcaX7zeXH9/927/913+dXZWH1yp7fKrP/u/2P84u3p1f/uPs97J8zNOzRbH9
      T/kV9a37tNpnQmQyXl2eHUX6Dxnt8PqPs32ZZDv5/+Mi+a+yOksyUVfZ5linZ/VTJs5Euatf4io928kP
      4+JVuQ7H6lCK9Owlq+UPqJr/Xx7rs12anknkKa1S9euruJAJ8Y+zQ1U+Z4lMkvopruX/Sc/iTfmcKtO2
      v/airLNtqq6ijXsYrvf00eGQxtVZVpzFea7ILBWnX7f+Mj9b3X1e/89sOT9brM7ul3d/Lq7n12f/Z7aS
      //4/Z7Pb6+ZLs4f1l7vl2fVidXUzW3xdnc1ubs4ktZzdrhfzlXL9z2L95Ww5/322lMidpKRvcN9e3Txc
      L25/b8DF1/ubhYwyCM7uPivH1/ny6ov8y+zT4max/taE/7xY385Xq/+UjrPbu7P5n/Pb9dnqi/JoV/Zp
      fnazmH26mZ99lv+a3X5TutX9/Goxu/mHvO7l/Gr9D6k4/Zf80tXd7Wr+zwepk985u559nf2uLqShT/9s
      ftiX2Xp1J+Mu5c9bPdys1c/4vLz7enZzt1JXfvawmssYs/VM0TIN5SWv/iG5ubzApbrumfzf1Xpxd6t8
      EpCh18uZuo7b+e83i9/nt1dzxd41wPpuKb/7sOqYf5zNlouVCnr3sFb0nXI2Wfju9nbefKdNfZUe8lqa
      q5gvZUJ8nTXiz+bd+M8m/3+6W0qnLD7R7Po6ul/OPy/+OjvEok7FWf1SnsmsV9TZLksrITOPzPxlkcqb
      UKssJjP1Xqg/KFFWq9Kqcly5O9vH26o8S38e4qLJhPJ/WS3O4urxuJc+cbZJJZw2gWTp/c9/+/dEluwi
      BS/n/8b/ONv8B/hRtJA/fdl+wevQv3gWn/37v59F6v9s/m2gFnfRLpK1DHwNwx/bP/xjAP7DcIi0plo6
      ZPBcr29W0TbPZFJF+1RWD8lUnUtaVoYO9Ii0ek4rjs4gLauqC6PNcbeT2Y3jBngzwvN5dMFPWZcG7Ewt
      6mOntEs79pCU8KfDo8zTdbZPVctG82qkY32SLVyeMsUm7LhZiYD8+pB75r9jqq7IiqzO4vz0S6Lk2NW8
      1EC4aog7Xy6jvIyTSBlU70Z2xaYGgtjBfHc/v1UfqGugVJk2Nxjv51+jKu3irWR3QbWJE60QC5g3WRlk
      t3gzwkslW1Gu3oEhd8Dlg4Ihhvrj1eJe9lyiJBXbKjtQsiRMg3ZVP8RHWc8XWcLQ6zjq36jeCs+tUNS7
      zQ6yfx9w5YMAjZFkj6moA2IMAjQG2+1xfv8ZFfE+ZYo72mtnX3ULo+59/DOSVbbg5XfLgEfJitAogwGN
      EnALvOl/qHYBN6CjPfayLrdlHgVE6A1olGq3DUmfE476n+P8yJU3LG4Oyje+PJOJKJbtGsPckZh1k5fb
      7119x7PrBjCKqGWPMK4S7k01eCvC3df7KE6SaFvuD1XaTMUQu4MjGiDerkpT4JuCHBETATFl/nhHTz+D
      hK1v8kMQDxIxS1gBsgTxcZMFSpX1XyofvIu2T7GsxbdpVZPMLg76z8P852P+5hPjjsT5IyMQ6EEitsPU
      qxkrzAmG3enPuorDksxxwJFE+zM5ATrU9W6fUlk/HqrsWc2yf09fqXZHAMRo+6vytz1W5fFAjmDigD9P
      40pLPUGOYAuwGPZ9YkZyNFi8fZmkvBCKxKxlM65iXnsHu+60iDd5GpVbcVCN4iGXA31qCMiBRhLZY5F2
      tYCaupDA/iCYIWEZGrvOhbp/RZGSu5uYxI21y4/i6VR0yT/MpAG7bN/JTsm4pqYRVymX7bKtrAWoVpvH
      IqjywnMr0mflFWabRyIc4ires9wNiVnbGpdRY1s46G8LgqjV8xm6XqMRe5/ro+2GFUAXIDGaZkOw7C2K
      eE/dgSjPRM3SGwY4ivxTfMzlkDQW4oWbSo5kYqzoKNIqiev4TYL2Njh6+jPihupQ1FukL7LbkKQ/mfKe
      xyIE9gZACRwrK3ZltI3zfBNvv3PiGAI4hqwM8vIxKIqlgOOoia6mhuAWIEOAx2imc1jTHpgEiSVvXXgs
      W4LEYvQITxxsZPYGNRT2/jhm6pH207FOyhdWkpgGOErzPCV+os4+OTRs73pPMj/LYQ477V0LHI34RBNA
      EW8uZC0jv7P93hZR1s12LXA0mX2z3WtQLWIpvHGS9FA/BQRpeG8E7m3XcNffPBHtvpGX25hVBkGJG6tI
      5cim3h+i5Yo8AaKzkPmFLnxxPVW6L59T7gSHSbt29UEUb7fyTlPVGur1Ro9lmQTIG94foUqL9LGsM8YA
      C9Eg8dpqanfMc1acAcf8m+gpo3eWdBYzl3JQsOXd5I71m/m3WReMxAi90YAHidgMRprbJbK/ecFMhSdO
      88UNO0aLe/yqrx7gb3GPv6tkAkL0BiQKu1B4SoRaAJzyrC2KeIvjfkN8JGeiiFeE50gxJUeKsBwpxnKk
      CMuRYixHiuAcKSbkyK5Xycs/Jxhy1++6BZrRoSwZzYzJIxFY84XCM1/YfnaavBE8dY8j/lPflz3/BlvA
      aOfsNDr3pJH87Fg9c2qdHvV6WdMGNo9EYM3VDiRiFdljnD/yEqRj/WZ+kugCJEbYsw5AgcR5i5x/PjHn
      R3JoWb5Ex+J7Ub6oB8eHbvaFc5NwGRY7MNoUv0hz1QnktA62AY7SPn1n6TvU4+Xe/9H73nweOEWBeZCI
      zdRuXCScp+uOAI3Bf54ixp+niGHVKbOm0XHEH/RcRUx4rqJ9JyTzGgYkyrGq1JdUH4gbxlRgcWRW33f5
      kBdFE8Axgp9EiWlPosSbPokSxCdR+ve7Yn2I6ycRElf3IBFL0dTksp5tJoh5aWtL4FhpXOWvzfOybv0B
      pykHLEg03lM94Xuqpz7cxblI1dqQqmt20yTqXp5tWi1OwDEnfCWPVRpLLCAtTQMcJXssZFumOlDn7yP1
      GOSxihNWywibkKghTxvF+NNGEf60UUx52ihCnzaK8aeN4i2eNoppTxtPXxOp7A3sqvhRvUjLjWVIkFih
      TzbFtCebgvlkU6BPNptPRFj20vnxCFFcPYZGUQ44UqGevbWpGNSzhzxjEUUUJ89qeZZIk+CwlgyO3SwA
      rFJxKAvByhSGAInBe+4tfM+9RfMSSb8UlrPYH7Ug0cT3vkcakNUBDR6vezk1NJ6lQeJ1G2VwYrQo7P1x
      zLYBt0fDUX/A6gcxYfWDCFr9IEZWP7Sf12rkWRayxyee4osPH6Nyp49/BC/qmBW7mq4/Lfu4smQf9ykv
      um2Bo50qx2FVKrPmA0VYzNDVJmLiahP9e2rIXxa1rKBDog0WfzRV8JOnlLvWxaNC4kLrutldQdyGR8+K
      R/ViSlnJEcW+2b1IcEMDKiRuVR9Uc7vL8pQXTRcgMeoq2wZPC7kWOFq37Ei9LBhQbbsWLBo7d3pzozkP
      HjJ2hE1oVNX9attb9VoZt6sKiqbGDOku4DZ/9DqujyL01/aSKbF4jYTt8EYaVuCFRTM8EyOKN4knvNGO
      ajJG1j8BoU4KJI6ss5Mnlr4hfdawbG4q8Djpln/9isXNlYi5Yol6vcFJozuQSNWR1ww1IOzkT677ZtW7
      XugbdAxgkzcqa82sGF0ze1RD7h3V21KATZbh+3YU/Af9wZlJj9mj2er2PCxEoxiNo/pTgXGUAo6zXM3C
      EswQTIjBTjbXMiUaN/FcCxwt4BVGCx/1s1POdoxHah8fc9MONo1HfYt4eCQ19Gs3paxfo6eMPgcOSsxY
      3eZWkdpgtX8cNDz+okQcUcFxtSdt2/iguveckK4FjkZ9G1jnMGO5jzavNW0A6tKwvX33lrwxDIB7/Lyp
      EUThicOe7sYtnmiHNCDNFDzi1suwCApkmMaitnOJYfFahyfS20wnTVR6rqMdS7Fjtjjq5zy9B3Cvn/Vu
      LubAI9EWLJokbt2rvZEr6oIu2IBH6bcjYzx89XnwiN0QPc92abPuiNq0jrl8kfcpP9I+9ZuJc3kAjvsD
      b473njzFIrRysxR4HH6VMtCwPRPtoxZuH0bn4QjE9xA1DPY1K4l5VUeHer0hvQpLgcYJqcPFWB0u3qh2
      EpNrp2H2nhvHl0NFQA0kvDWQCKuBxFgNJORYIk+ijXrbqXjMUzWyYQUCPHDEuuT36k+s3xztyirgZgMa
      OB59vsokTSv9BWPoveKA/f28e/sF7Ovn3dNPbS4XHw551r59rjJsTdkd3OdwI7H28fPs4ac+UrMM3WsD
      x82/0m0tVA6SvXDaRPWIyoqbqy+pTa67HdFJkWx4xB3lZWCAxgBFaUbp3aSwaqLzmh7HdUCR6tdDyk4r
      DR5xM9PKNphR2pUYTxkpcXrIcqkFMu1WfCTbgFm+kP0fR/Z+pF8lcH0hezuO7OvI22MR21+RvbeiZ19F
      xoYG4D4G22NdP1Xl8fGp2S81T2kz3ABu+pM0Tx/VmV3RtkqbKdU4Vz0IUg8alVixyuYQDzmc+U76ETpn
      GWVzznjtSMNMXzvn2q/p3dY/1S5faXMKkhrzUYKMuaDIzWxv27mg3QEAR/3q3QXVVpOrZMxhRQrcTXR8
      J9E320WUsINo8O6hE3YOTatK9nuZx284sOX+eSirZgmHaun2srBWspCSAoAGMwr1WYT7DKI/NlAtbmm2
      gKf4XNq21+/0V2RphcylAbv+GEx1LgQ5gmOAovCaVf++p+2W7sNrBv0mNPRUAi1ANPbzk7HnJrz9W7G9
      W4fnDKEjJr8Ji8p9LjPleczwna4Z7/Zjb9ekMMOBKiyuvQ6GGdPRAPG6Nxeq9MdRVvOy0ifuBoJKwFgh
      y7QRBRTnTZ5skZ5oPTYbUND3fNM5xxh1j/iJwhPm+pirQiwU8LZLnjev9CNfABz1M+4gvhqbua8yuqdy
      2H7KY3spa59Xsudf7pnyFgbc3Sv69GUILu2xDwdcsEMMCjzOcFArM0ovAGM8p8Surs5hRurhKibpWk9v
      7jNm7AHc9TtjH2oERwDEUF14sldBgIv+DAl9/q99EP314d1v0Wp9t5w3q7Gy5CczBGACo7JWG/hXGXSb
      d+9FJI4HNaihqzXYde/IpWUHlBP5j0w8pXRXx7lG9t4AI7uQNx8/k9sVibiefuAW5Sm5jBmw62bvJzCy
      c3nwruUTdiwP3q18wk7lnF3K4R3K2305T+O+qC6/p0W0kUVRTR1wRmUjNjc6YzYX3Re9WYlzGkTRN94D
      cI+f2WG1eSQCt1IxYMx9zPPQJLIcSKTmHe5adu5EMyXVZAHBigeakKhqcBTXxyodhpismIAHithmb14P
      1aQBO+sIGpMErNqybLJXY/1m8tI2UODG4L/3P3biQbOF8CYrqU7FACbWzgG+MxP6z4Sa0Si2KUt8ggE3
      vUNUQT0ikW5VqRl2x26mznhdOJ8LitzO9xpvV9NDAhIoVju7xBr3GjDqVq/kMcq+SWN2zshuIH3WZjac
      r25wyM8aoaOzWOIprtQcGm+yxaRRO2NfWpeG7LzaD6/3gMauO6GcHAM1TYuqBgesDORxTYvMKhGIB4jI
      3THi0b9bhLYSPH5MI/GdtlIXwAE/+3GqS8P2Y5H9oE/RDiRo1d747x9BMUJAmrF4nBzsGtwoARvrjp77
      E3Lmj/+8n4Czfrzn/Ggf0hfHOTDo5rQ56Kj9hdG7fAF7ly/0vtoL1Fd7kVVWyu5QmrRpV+8shD6FxRxm
      pKxgvjVqgI5T27SUKNVIxyrH5lSdQiyPiBJZW5A8LeJ4lJw13WCzjrnt0RGVLeS6gGZWbVZxENRE8Jic
      qAF7wrq0azfmx3iLODwaM57qCR0PCXHGaqBMW55tqrh6JWdmnbOM6qC14VEjddwG4IC/XXvVLoYTZL1B
      m/Z9/Jht+9mcfvu0mpT7UYkdS20sG+dRKQsKdXrBgU039xw7/Aw74ntjzvtixXFvDv5J982lTfshTUld
      KPV929DcLpqkQSxPVW7VmT7NROehFDVvCa5HA8drKyn1AO6U4eivBY25nMjPWZK2l0htsR3YdLebhso8
      3v/qaJdnj0819SmVVwTEbGbW8vQ5zclRBhTwth0snlhjTXNFrDQqp55gHqCHnpenfcApUQBu+4X9aP9f
      xFX/iMKM021FOqyqpERwYNutNhOXkfP2lRia2mRtc1taq5T6QoFJ2lbOCWHY6WABJ4N5TwVrPqRO+vcQ
      4Ao6Y2nKyWLNd144V/wCXfE56x6dI/eIczIZeipZyIlk/tPImk+ht2HIISAJEIv8LB078Yx72hl+0lnQ
      KWcjJ5wFnm42erJZ+KlmU040E7w1rwJb89qc/9WeFazm+qjXa7CAmXf2mffcM/UhvcaJoPqGczAUeqJZ
      0OlfIyd/BZzI5T2NK+wkrrFTuJrPu+OJWZnLgAE39zyskbOwws9PmnJ2UvOd9gWvdPvUHQ9EDmILoBi7
      stqmzcRSM4ci4kdGHEACxKKvIEV3HxHkVZECWBX5NqcqTT1RKeg0pZGTlNTH/0q+n59HL2X1Pa7KY0FO
      HZt3I7DXO46cnRR8btKEM5OCz0uacFZS8DlJE85I4pyPBJ+NFHIukv9MpNDzkMbPQmq+UR/J0vroetiv
      DI6cLsQ8WQg9VSj8RKEppwm9wUlCk04ReoMThCadHsQ8OQg9Nag/8kffTpX+Np5Hg8Tj3W70dKL+w5Bl
      r6gEiaX22lXD0K0cw8j66FBmBS/VIBEYk7kGaezUJf6JS77TltrPhilCTj1v81CEtzzLiXOOk6Cv4RTQ
      Gk7BW20nsNV24WchTTkHqfnOU5poU/bye7uM3ESCEigWL//jOf9tXhCmnKL0RicoTT49KejkpJFTk9qz
      jhgjXmSkG3b60pSTl97mvKKpZxVph7c8qQeD1NWOEI9GCFl1J6auuhPBq+7EhFV3gefmjJ6ZwzsvBzsr
      J/CcnNEzcrjn4+Bn4zDPxUHPxAk9D2f8LJzmG+7LbeTKDHIAkagn7iCn7fBO2sFO2XmbE3amnq4TcrKO
      /1QdEbKCVPhXkAr6Ok0BrdNk9TTgXga5fQTaRvUnxq5kOocbydtDOrDprkv10Ju/ggjizQj8U5R8JygF
      np40enJS4KlJoycmBZ2WNHJSUvgpSVNOSAo/HWnKyUgBpyJ5T0QKPQ1p/CSk0POIxs8iCj6HaMIZRGrd
      SvSU5nmphtvV62m3KGIY0GFGYsxbgzPVLzEtEdT3LYNaGEZSKMBwPF+8P01EkCfQHNYxs5SIq5vFZCkN
      djCvb1a8H++AppMugyysH+yAplOdqBVtjrudzJAMM4Ab/ufz6Jydoi7sunlSzMZNYRe23RchqXDhT4UL
      phSzBaTChT8VAtLAmwIcIWwK+O3IL08uskg7/2Cq08JQH2X1DoAO3uwi4VynhaE+ynUC6OCVrf7V8tv9
      +i769PD583zZDOXb4wF3x2I7NcaIZiye2jf3DeL1Gk+8JE0PzYWxQ/UGTxS1uL845jk7yEngi3Hc8/XH
      vcd8OIontlrBHreY/s4ExHrMpM0rYdqwr5bre/n9u/X8aq3KjfzPz4ubOefejqmmxSXdb49lUjRiHvBp
      zHhqpefi/ktfR+wP1JKPKbA4au1xnfICtCxqPh6Y2uMBc8o/JTypIjErJ9O6NGqnZU0DxJzUDGiSmJVa
      Sdio4W22fLydfZ2zszJi8EZhtM2YwheH0yZjCiQOpy0GaMROLEgmiDkJBwI4IOIkvPppc7iRWthdGHEf
      ygM/FU4w5qYVeRNEnM166pCCqQuwGIQNuxzQdYYVv7GSx80ceL6g1f4nxPVwsxaeq8RTtiPfmQZyXdSW
      Y4AG1+zqSg7Couv56mq5uF9TjztHcK9/+sYDIOx1E2oumNbs81V09XV2NdnXfd80bDfbKC221ev04w8t
      zPLtNucXlyylQVrWuuJaDdK0JilZ1yGmJ91uOJemYZaP4YI8JftelJ57IZrN0psPKO9FAajr7QJyvBpq
      eo/FSxUfqMqBwmzRIU6S6QuqQNh0c64TvsqAa8SvcHV7Hs1uv1HqxwGxPJ8W62i1Vt9vDyIkGW0Yd5Oa
      CoDFzY/NS4g1V97huJ+v9lkpzY+L4l7CFBWAer0hqSzgVP56z84eBop6qVesgaiTfOt00rbe3d3MZ7fk
      6+wxyze/ffg6X87W82t6klosbn4k5jETxb0ZW+tLB+rtMlHcK/ipIHypUJfRp1uuuYEt92dmJvuM5rLf
      57cy3s3if+fX64UcCsbJv0hmgB+JQG+aQMNIFHKRgQQjMYg3wcVH/NTsDvAjEQ4VYYkObhiJQi1eAD8e
      gbjEcUQDx+O2cC7u9fPyFdbamR8z8xTa6i1mH7ipYqKol5gaOog6qalgkLb1dj3/XT0D2h9ozoFDjITH
      OjaHGOn3SAMRJ7ULoXGIMeMJM8xHvtsDhxgF8zcL9Derqucoq9KPv3DFHY746V0Rg7Sstw83N/TM1FOQ
      jXjTOwYyUW/3CbJcd5/+e361VvtEERb6uiRsJaedxsFGYvr1FGyjpuGA2b6r9XyYWCBWkTbsc1MrSxv2
      uel3y6Z9duqdM1mfmXwXLdjnplaBNmy57+Xf17NPN3NukkOCkRjEhHfxET81+QEeixCQPt6UYaeJJzX4
      6eBNAcrLowBqeVfzfz7Mb6/mnMlYi8XMXCtgXPMuc41cYZvd2rSJk4RmtWCfe5uncUGspyGBLwa1O2rD
      sJvacqFt1ukDwmoTm4ONlE3FbA4x8u5Ugt0fcpWF1+TDhP879g/vYdTdH1+8j8V3ZgjDAUfK0+Jx+juy
      LglbqZUu2uZ0H9CninTQ44ymn0EMsX5ztDuEyCUO+wWvlhFY/aI2/GUK36HGaPMa3S6umd6Oxu2hpUNM
      Kh32t6JYbN8imvLAEeWA92H9+ZITpEMRL7XDonG4kVvQT6xlXn8851bXJop6ib0WHUSd1DQwSNvKfMay
      Rp+xsB6sIE9TmI9Q0OcmzQdJttvRdYqCbPSMgzxv4TxkgZ+ssB6nIM9QmA9O0KclrEckyHORkIch/icg
      zaeyentMi7SK8+zvNFE7VdEjuA470rf7Obm/fYIgFz0/nijIRh1fnCDIRc6RHQS5BOe6BHxdapd1luzc
      sj3cLv6cL1f8J2eQYCQGscJw8RE/9aYBvB1hfcVqIjQOMdIbCoPErPtDs01dVPPUPY746blEAxFnxrvW
      DLtGci4YOMRIb1IMErFSqwWNw42c5sXFHf/nS3Y1YbK4mZwNNBK30jODjlrePxerRcA8uIt7/cQEsWGv
      m5osDm3ZaQdca4jlafsftRz+qM1CST4TxbzP73nS5/eOsY7KDeXsKwuzfFmd7qPkIiPZThDiouwB4ICY
      kzhto3GgkZ5xNA40HjkXeASvTh3twLklLYcYyfWGDiLO7CJhKSWHGKk1hMZBRt6Pxn4x6+civ1VtfsEq
      Jx2IOTnlpOUgI+t2IPfiEBN7nj0F2dR2xXSbojBbtK1/8oyKhKzHgvebWw4y0vb/tDnLuN90uy6Snz0Z
      JGYt+NoC8LbNl0zvv2klWuMso+wl77M6e07p1YSJ2t5jHaUlbU66YwATo7UfMMtXx48X1Bc9OgYwienH
      I+uMbUr3h7zZP5B6EwxSsz6sv0hg/S1a3H6+i7oXPEl21DAWhZC2CD8WgVIjYwIoxh/zb4trZioNLG7m
      pMyJxK2s1OjRwftptlpcRVd3t3KoMVvcrmn5BaZ99umpAbE+MyFFQFhzL+6i+HBojnXK8pSy3TyAmt7+
      BKNtXeUUqwFazjyNq2iXx9MP1rQwyNduCMq0arDlVhudNAcFN18hmU3U8lKT001F+ZdmuNgcxkLcTBUV
      IDHaE64fj3EVF3WassJYDiAS8UBqmzONSXk6YZHiGyjTlpY7ikZ+3eTVjjCkx8gGZLlywi4nPWA5Ktpd
      tOrJ7i9RnOdUi2JMU7PWhrAUSGdc0/Rt4AcCsBzIloNryYqspnoU45r2ahKCkUYnDjYepncMLcz1qd1d
      ZH6dviTIAV0ns063UMyrjhSdvk00xLpm6gkCNucYqT/c+rVP6c/kuCdl5g4xPeoGFaS83BK2pSa3fCfG
      NKls2BxOVdBSSOdsY/1ErhZ7CHBROngaA5iaDaRIL7MAKOYl3g4DRJyJ7EhU5StL27GImVogDBBxykE4
      z6lAxFkRDtVzQMRJ2kzeJV1rSe+RaJjpI2Z2J5+rRmCTldEhziqiqOdcI6MDqGGuj9a3aAnAQji/QWcA
      04HsObgWVSdujjuqqsNcnyi331NyoreUbftJ9Py0Dcf9Jq3I5VHDQJ8qUbINYSg70rQyBj7gmOdQkjKE
      /LrFq+UIpIzQEpalrsjNyomxTMSBzsEZ51Ard7dOp2YdN8+0p6WK4pyqaSDAxZnlMUDbKWjFtQEsxwvv
      ql6QaxKculvANbcg1tvCqbUFuc4WQI2tTuTY0yQSsB302lWAdWvTh8sJp0obEOCSSd+cV0nNAw6MuNVA
      4EDYJxWEETfbCzupI3UBzmYI8myGAGYzmr9RR9A9BLgOZNHBtVBnRgQ4MyK6CQli70XDYF9a7tQ4/1gV
      HO1Au/aCsJRAZ1xTPw9BziED6bESZ0aEd2Zk+FQc0m0W5zx1B2Nu8gDJQl0vZzZHoLM5/VCsO6GJ9Igc
      FVgxnspjnkRyRMRJaRsG3eQsN2CIj/hgRedAIz0jaJxtbO+k/Iwm7DHLV9D72CfGNNWpYFTsA2XajurY
      Z9JVtYRpeabOnz27c2fPnCR6htPohTGwegFHVuQsBeSltugSH5n0EOTidLlNUrPezP6YX3y6+PBxsq0n
      IEv0OSsI1Y/FgcYFpdNgYqDv4ZBQ5lRtUHPeRp9uFrfX7Xv+xXNK6E26KOwlFS2Lg41Z8RznGSkJQBq1
      M5Mh86QCZZ7RxAzf1fqvKJ1+uMdAOBbibTkhjofwctpAOBZa8nSEYxF1XFGvpmEM0+/z26tPzToQgmqA
      AJcgpVHPGKavd7fr5oIpix5tDjYSs4LBwUba7dQx1KcqGVFTXgBFBXiMXVlF+zI55kfBjaIp4Di0zKBj
      qC/K1TxJwtR2tGGPNyLKRPRSVhSrRpm2hGRJHJp8IR1iesT2YlNQLA1gODZZQXO0gOmQf8lIjgYAHMRj
      AWwOMB5iuu0QO6btZsO6toGzjUm6pakkYDueCGs8ToDtyFPWD+sx27c/ZDSTBAxHsw6QoGi+7xoo2/Pr
      DGAiNicDZLoIiz9uzffw239T64wTYnpoja3Txm7LY6Eq2Jfo77QqVYIJks6hDbvM47TaqAVMR/ZMEWTP
      Nk1N5xNieo6Uu2281Sb/nRZPcbFNk2if5bl6/Bk3lVyV7WVPv35tJg8I+ik6M/6PY5yzOigWaVp/UtJE
      ftugiaXQKX+7qtzLjkxRP5b7tHolqQzSsD5uKVlFftukT2+tqnuRRqTq3GEtcx1Vu+37Dxcfuy+cf3j/
      kaSHBE6M4/TNlgfCsRBL3AkxPLJto9UdLWA4SA9Dbu3nILeqryjrNGKPeIBsV5E+xuqVKZrsRNm2ktRp
      bQHHURAvRgK241C+XNAkinAs9BKjUbBtF8taS83L8rQabvuJGRwac8i/qUaTZlGEYclTWiFpvm8aSCcx
      9gDgOCdLzg3LPq7Ek2xtSCs6TMzyie/UHk3PmKYyIY4ROwKyRD+O2fR3Ym3OMdJa4Y6ALBdNm0h3tRxk
      ZAr9PlY3BhbgMYjl22EdczP1KqiX3FGYLdrkajF4wrOeaNReJlxzCeR8cj0zQIjrnCU7x2yscmmwiDlA
      jHj3x5yokwRk4XWgXdhxEzsFJ8TxiB8VUSMJyFLTNW6+E8cNVXPcQBZWlug5x8iortxa6pDRuhItYDpo
      +dLOkzJLUX9Jhxge2uS+PadfFDJ5KLz6vmugloABMl3HPbULc0JADzWBDc41vsr+MdWmGMNEG4TYI5BD
      rFoc1fmLjoXai4TUHgK0aefO0XhmY0i72p2+7xooCwYHxPSI9JiUURWTnthqFGZT/+cx5Tlb1jATL9C5
      MtYlea6l/TNtWGlwppHaM6rcXlFF7hFVQG+IeAzuQDgWxlSHjjk+2ryUAOalBH1eSkDzUrQeid0bIfZE
      nF4IrQdi9z5UD4KaBh1ieOoyso5mJRhdGHR3Z60xxB1pW1ldXYMzjEfahMDRng040h4gHe0nSEdaVjja
      eeE5zo8pse3tGcNEnMay5rD6r+yOxbbOyiJ6ItRAIA3ZRZrvaG24i2reh8/R1/nXbouXyUqDcm2kRyIa
      45oeq/KFalIMbGrPGOL4WtK1UrroA+J61AtT1TM50TrM9O3TPeUpX0+YFlFXREtLOJZ8G9dEjUIAD+EJ
      8YA4noL+swrodxV5WlA9uf5e59WnT810KGWaWGdgU7Qpy5yja0DESTq81CURa7mtyftNowIsRpa0z0lr
      wpvCuAGJcuQn0BFJIdKQ1IBclzjE25TqaiDXdTz/SDVJBPR0Z1zJIZ386Of04a5HAcbJU4Y5h377Bfke
      SwT0BP92VwHEeX9B9r6/AD2MNFQQ4KKXkyNUPuQfGdekIMB1SRZdQpbgm3rpv6fEMxY1xPRQ3j49fd8y
      ZMSXqAzIdoltXCXR9inLE5pPA02n/I9s+s4AAwFZKJtFm5Rlo+zK1gOAo2041KB++p5zIGy6KYtMTt93
      DRE55w+UaSP0r7qvmzyxT60hpocyLDx9Xzesuu5VWqlReJJW02UOCnmzuttr+SkWlFkv3ABEUb0geQm0
      XpTLmma1z1acFaJbdflKqU4g2rYfXqndKJ0ybbQ6c+XUmatmdVhcvBL7+yaHG6M0T/eEHdgwHo6gcmBo
      FNsBROKkDJwq9JGQBSJO7u8f/d1Rtj/k2TajD4hwBxaJNlixScR65GuPiJdceHvIdeWxqEkdPQNzfeVB
      zdIRV3mB8IiblY1dw1gU3mB8zDQWlZdpIIcbiTRS7RHQw+/YowowTp4yzHkKuC7IiWqNVPs/Bv92/0i1
      +xJlpNojoIeRhvZIdUVdQq4hoIdxTfZItfszuQKD6q6QkSpmMKPQxhIrZyyxUouETwsZ+rYnfaR1njGH
      E6l5Ud3qDBMDQQpfHN7PcQVmDNKYaWWPmVbt7kTqVRmKpYdM1yFNv7eXWsek1DRA0ym+ZweKSn3fMtTT
      nyidvm8bKE9GBkKzzJfrxefF1Ww9v7+7WVwt5rRTKjDeH4FQIkHabyc8CUNwzf91dkV+Bd+AABcpgXUI
      cFF+rMZYJtL+JwNhWSh7nvSA5VhSNngcCMtC2y1FQzTP3e3n6M/ZzQPpFFaTsmzNHgGpoN1/G0Scednt
      mckS97Rlb9fy5dn0Z/wWpvmWN9H1YrWO7u/IZ+FALG4mZEKHxK2UTOCiuvfb/fou+vTw+fN8Kb9xd0NM
      ChD3+kmXDtGYPc7z6UeSASjmJc1wOSRm5SezL4WbOWPZtPLMJxqzU3pRNog52dnBkxOabVDUo2l2SugG
      LApt5zeIdcxfH9bzv8iPswAWMZOGHzaIONXmLaStDWHaZ6c9UYNxxH8swq5f4/0R+L9BFzgxZEfxm2zh
      qQ/2IBh1M3KNjqLeY9PJiTbq5wlmAMPhRFqtZ+vFVWBGhSUTYnFuOWLxR+NnYkwzKV7w7/Pm7PWX5Xx2
      vbiOtseqojxagHHc32xJ3R26xw2iO/yRiuM+rbJtSKBO4Y9zKNVESBUSp1M4cbab7fnFpdrLpXo9UO+L
      CWPutAhwd7Dr3m3Ux+dcu4Vj/ssw/+j1B9lR91Ms/xddvKNqT5xrbHsiqm/dHNtO70UDBjdKXQWkiQGP
      uNU/CbPxuMKJsyur77JA1OoQ5+yxKKs02sfJc/SSHdKyaD5Vm/qpFeqU+VeO3L02dfAg7/bpqON93O5V
      wsTkFmsAMSevXjLhETcrL0AKLA4vP5vwiDvkN/jzc/clVpfUYDFzM079nr7y3Ccas8umb/qWZACKeSmz
      /TboOtXBF69t/6k9po7bh/GYvFG78+beIqyt8sZtLzQ8qOEBI/KqPY3ErOQTPxEc9DdVerfZWFYWjBCW
      AYzSpB5lB3WIRc1qzV3ALbYVYJz6qTnZSX6X8LABxl3/U6xWutLHzQPoONUaxFjsicKOcm1tx43c3+s5
      x9hUq+JVUN7lBlDX2xxOtcvUoahZnEebI2U5tMfhRMqzTRVXr5z7pqOOd99ML3O0Gula0z3hDVMDclyq
      RuHVdhrpWo/7iDO303OOsQwZAZX+EVBZbKmVmUIcz6HMX8/fv/vA6/9YNG5n5CaDxc1H2uNKkHbtctwh
      ZPHelD9Zl27hjr9KGPVOCyEutfdMnR3y9JJySpZH4cZJd+0Gu3JIEKmvN5sRkpbVj4nwmFmx5UaRqONV
      80XqVZ2Q3hnoACO9Tc9XEHq+4u16voLS8xVv1PMVk3u+gt3zFZ6eb3MMXRJy9RoN2gP7jWJKv1GE9RvF
      WL+R133Cek7d36NsF8XPcZbHmzzlqQ2FE6fOxbmsoal15AnTfOtldL389DttT3mTAmynnZfJwhMIOElt
      mA4BLvV2FWGpqYlpvqf4SvXMiRM7BjXYruer01TV+6kunTFN6XbzntptsznHyBQiviS9UA8QWFKLdczv
      A8zvPeaCfn9OjGkqmNdXoNem6jrCFJ2GgJ7oWGyfUsohMyDsukvZ4TjEVVaTL3UgNeuXqIk02dV93zVE
      h+OGlIAWZxrL/eEouzdE30BhNjW/8ES4JxCMumnnnICw4aYsueq+bvD9Dv60ZNQx2CdzUbxP67QShC3n
      UIEVo34XPZKcCnAd1N/cIq7nQLUcAMcP8i+SCOCpsmfODztxgJFcaHXM9f2gmn7YDnUoxK+/nf8WXbz7
      5ZJmM1DDe9qSfch3BLMLG27CgsD22yZN3E9VQwxPu2iY9fts1PAKelkSUFkS9HIgoHLQDHuaN5Zopg4y
      XYRTmbuvGzxtQWUP6I4m1QXlNB+d0UyL5fxqfbf8tlovqWeIQixunj6McEncSilELqp7V/c3s2/r+V9r
      YhqYHGyk/Hadgm2k32xghq9bKB/dzr7Oqb/ZYXEz6bdbJG6lpYGNgl5mEqC/nvXDkd/M+7nYL23myA6U
      h5ogrLlXs2i1INYeGuOauraTKusw10dJwAFxPU2bRzU1kOlqhzDq1dS4PlYko4Wa3qQMUbu0Y1efEJUK
      cTzPaZXtXommFrJcsnG8/kISNYRpoeZcN9eyBk0Whxh5wybUYEchDZx6ArCQf7nT3zv99UD2HCDLD/rv
      MvuN/V+pAygbhJzEIZTFAcYfZNcPx0J9JGJhoI+8DAhiTXPAwAykEbu8e4wiDeCI/7jJsy1b39OmndjW
      Oe0ce0gIsKCZl6oODLpZKWqzplkw6jYB1m2CUSsJsFYSvJIqsJJKbdbdNp00KO6+bxqIw+KeMC30jgXQ
      q2AMr3VocM2veLPSNocbo112EFxtAxtuRk/epGBbSTxjB2Ihs2rF6E5FYbao4vmiCjUKphH8xcSRkQPC
      zp+Ud54dEHISWiEDglykUZeFQT7ByjUCyTV1yc3bJ9K2EsdZBgS4aFWihdk++oVBV0VpLQbCtnB+mPur
      ot8/dydeyj7L0/Qz01zSsRaZqA8XF7/wzBaN2D98DLH3NGj/O8j+N2Zf3j3cR4RFvToDmAjNtM4AJlqz
      p0GAqx0mtyPwsiJbTRzzlxVhP2EAhb2yi7CLt8yr7mHMfayeU5VHePIT7bVT5jYRHPEn6SMnjwwo4mXf
      SPQ+tgWPsEW4SwJWNR7fvIYks2NAovDziUED9ibFSE9PARTwitN+trt8+itwMI3Y+dWJQSP25j149QKJ
      OvxYHUG1K6s9KxJoMqL+Mf/WzTXTxi8WiDhJIy2Tc4zyhmcyKzXjEJFuq+kbpaECNwapBesIx0JsvU6I
      4+FMZQOo18u57Q4PRFCNZlWSk3MAYSdjzgrBET953gqmIXtTDqll2WFBc1psm+pKMMw9C5tpk1suiVnJ
      k9EI7vgzEZWH+MeRWgR7zjHK+3lBeCXHpBzbadqY1XTDAjQGv7h4586775CmFk4EZGH3ZEAejEAePJmg
      42ynqtkXbeOInz75j+CYn50/PE8Bum9we2EOC5q5danw1qUioC4V3rpUsOtS4alLm94ko5ntOdDIzxUW
      Ddu5TawJj7ijeKc+lPdaDhWyIibNC07zOVdAe3BiQIbr63z95e663RohS/Mkql8PlAoG5I0I7RIiwoHD
      OgOYmjehqP1eG4W8pLmpnoFMhB2sDQhwJZucrJIMZDrSf5894qCvmjMgwNWcBxNSfHyayfGIUw5jKiBu
      pobFNTlGi0E+EcXqbWX1Kn1Nz20mDvvlEL7pNHDkJxYw74/0HC0ZwETrEwLrI/u/ltv6opm/IPt6ErA2
      f7/YbjZka0+iVhmXaZUkYBVvUw7F1HIo3q4cCko5bPtk+0OVCpEmbxIb1yHx65JfcC3eiNB18bPkoiDs
      I++AoFPU8rOE4WxBw9mc2HXM8jrraglKPnNhzX198eHD+W+qD3WIs+kTpiaG+k7TedPf20MFbgzSM1aN
      cU3EJ6QGpdsW97Pl+hv5VQEHRJzT18pbGOKjtAYWpxlvf1/cEn/vgDgelVnbR9DEOQEYB/3LEPsSdzcn
      VZxKWlo8yo8EMQKkcOJQ7ltPOJYqfZRVjTptMs+bGjlPa+otBB1OJBF2T8XYPRUh91Rg93S5jFazP+fN
      HtXE/O2ipldta5NWVVnRZhwc0mfd8bU709uOAZuPKU4Ng3ziVWacPVer06a9/Rm0w8lsDjdGBdcZFaa1
      2Q+3/UhQnDpnGY/Flv3zHdh0N/P61FvVQ4grytWfOMKG9FnJBQvAXX+R/hy+1WzxRw3hGswo8o/sW2iz
      llm1LJ8Wd5w8Z7OAWf0H16yxgHk5u71mq3UYcDc7lZRsu4mb/uZ4PnKRGSjMRi40Fur1kosNxAMRmvOB
      eYkxoF4vL1ksfjwCL4EgiRWrPKhB6j6uvpPsA2b5KrW0pAlJytY6hxuj7YYrlajHuzuwvbuD5T1yctwR
      zGtVGouyYFfMAG779+WzatUJ25LZHGjstpfjinXc9otaHR7AMGug6RQxJw0GyrLJ1pZanE6MZvrzPprN
      Z9fN2ZQx4UQdB0ScxNO9IBYxk0YsNog4VRdm+m74AIp4KfvnOaDHGb1k9VOUZFW6pex+PuZBIlLG5RaH
      GMtDyrtoBXqc0WNcPxFW0iI8EkGkhDdvbNDjjMQ2rmvmZesCJEYdP5Je8AFYxEzZxdcBAad65E3bpwdA
      Aa96U0lW/NUTp6bTYcTNTWGNBcyF2nmbmx46bLo/qZeO1uUfhKUQBmXarhb3X+bL5qY2x9PRXu7BBGiM
      bXYgFnAHxt30NsulcTtlLYCL4t66yrleiaLebr9LSp8QE6AxaCueABY3E3sJFop6m0f9hwNtvIQr0DjU
      noOF4t5nRoUC8WgEXh0OCtAY+zLh3l2Fol5iT8ckcWuWcK1ZgloryqntEIuaRXgeF1PyuPpSSA3Q894I
      wfnRlHhjqe1Y+RWmZgCjBLWvI20r9z7g6R9S0/hrmaA7OnInmTULWqvwyr5b7undHqiv0/ztc1bEOWEv
      KZeErAtqg9VTmI11iR0IOR9IJ77YnGm8Trfyjn+KRfrxF4pR50CjKqUMocIgX3PH6L4Gg3zUuzxQkI1+
      R3QOMiY35HrBAB2n6sFyCoyFgl5GYp4w1Me7TLDUdJ+xbtIAWs7sMRW0H90QkIWetwcM9f1195mplCRq
      pd4Vg4Ss5KzTU5iNdYlwvmk+WlFWsRkUZmPe7x7FvLy0PJGYlVFsLBYyc6248U/aGkGLw43Mu6XBuJt3
      xwYWN3PTV6dN+7xgtesaBvnIqathkI+aogMF2eipqHOQkdGuG6Dj5LbrFgp6GYkJt+vaB7zLBOvn7jPW
      TcLa9S/3f8y5c6g2i5jTn4eyqlniFkW81Jk2A0Sc3OcNoACJQX2GZoCIk/qEywBRZ308RBs55Imq6Gez
      xJwZwvGMRxRvFFGQI6pXfZsTKt8qdC/0XsNBfH+LZNY1o/HE28QT1HhvkcSgD7iCZtKeU5pPIOJ8+p7s
      oj1P27Gm+et1wLM4BwbdjDr+q2dlx+kz4vMxDUN9xFbTJGFrc4IqR9qAoLM7HpUh7UjQSn0C9hVbJfOV
      t5blK7aSpfuAlul7CHQRn9t8RdandH8nP1nROdDIetJhs7CZV8LRsk166d/EHB+7DvLUP5xUhFNPvRjT
      7lbAUJqw42b8ZvDXMu6GeyfuP80jQTrz0qQs2x9Xq8sL2QR9I9l6yrbNv100H9JsJ8q1sdZEGCDiTGgt
      ns4hRmoNbYCIs90R7DttbY9L++yViKMyTg9RHm/SnB/H9OARmy/uH3fnxCYDc4xEai4pMFLnGInEeFqM
      OcYiCRGJOK+Ja9R8Hk/E/vygkGTUJUgsYquvc7gxyhKuNMqwKxVvVG7E5HLT7N+0bffiUiuxuOEMyYRY
      j2kxbCIQHNSweaKrJJG1lvo6aWPXEc+0iIfjJv15eIuYrWkkakhNKCbVhOINakIxqSYUb1ATikk1odBq
      sC61A3+ZYSJEfYPb5+qmxw9pBnDdhPhvFXg8YnD7I8bbn1gI4gNODUN90fVqxnQqFPe2275x1S2N25f8
      q16CV91MfDLaj46DjJxmAWkDKPvDaQxs4uy2CeOQX81khQQweSBCktJHlhqHG8nzTQ4MutVm3AyrwlAf
      91J7Fjc3S0JT2so/iAcidMvzyeaOw4285NBhwM0aKyPj5Gb0Of3UUJtDjYxa8ARiTma9rbGYecm92iV2
      tefMND1H0/Scm6bneJqeB6TpuTdNz7lpeu5L0zoXqmyopQy0fQm9FjhaVMUvrH14PQ5fJPqevLgCiMPo
      QIB9B/re7g4JWNsONFnZYqiPV/lqLGDeZ7KvVjyGdCRcBRCHM58Dz+WoyZjQvAw4fJH4edlVAHFO0yFk
      +wn0OHl5xqAhe7OLRXssJl2uwbi7vTNceUvj9uZ2cOUNDLgFt1UTeKsmAlo14W3VBLdVE3irJt6kVRMT
      W7Vml1TiUzQDhJyckT8y7m8Gwazy15Og9W/GL3aeQDZ/ZqUeknLEvepNDPA9kxcvaxjq490PjcXNVbpV
      y/q48g4f9Qf9At1hRmKtwkfW33NW3sNr7k9/JS720TDXR18ci63bZ66GR9fB81bAY2vfh78TU88AISc9
      BfE19Gobz3bvhijOs5jUnbBZ15yQ30kaKMumdpWKUxGdX1xG2802Ek9x00qR5JhkYqwo2x9k3yOj7mg0
      STh+DerU1zf4xZ3GF2+7jzb5Ma3LkvZiAG6ZGi26fJt40aUvYl1FT/v4lBr8iKbHE/Fxu2dHkazfLIc4
      RdJsShMSY7CMRBMBmb/jRyLI3Hl+ERSjMUyI8j44ynssym8X/LvesohZld/gGtCWTIwVXAP6hOPXEFID
      uprxeO8vf3mLeJ3GF+8NaiTA44nIzZsd6zcH1kiOZSSaCMiM/hrp9A1+jWQYJkR5HxwFqpG2T7H838W7
      6FDmr+fv330gR3EMQJREXkmapO/DqifQMjVaUAU1agSuojjmOf+3GjRg/xl+436O3rm+d0hz9xjiqyuW
      r65gX0rYQdjEYB+5AkR7Y+0H5Y51fRIDfLIDwLkfLYb4GPejxWAf5360GOzj3A+4n9R+wLkfLeb6urac
      6uswxEe/Hx0G+xj3o8NgH+N+IH2D9gPG/egw07fJ4+/pxYbYSxoo08Z44Q580041HcQc0iGuh3gnOwTw
      0Pbb6hDQ854heg+bOMl04hAjJ8E6DjQyL9G9QnV8sGriKbITY5qaI+ObubbNK+l4aoD1mGnP9S3U9bYz
      ebwr1lmPmX7FGop7y82/uF6Jmt6nWDTV2VNcJS9xRUoJmzXNp0Pd29BRnD+WVVY/kSpuzAFHYj72958+
      r3+B9bDfpS17QtpKTn7d5j/Q+A8O3/TyiZKGMU3tMe0h9xs2QFGY99p3kvzwMes+26xprrYX0S/vqJX3
      QLk2hgrw/EJzWHmPmm/cPKPmgi5+ITok4VpofS5oDqqdDSNaJOFYPtDmY1oCskT0X9VRpk1NFah5g2Zh
      9z4mZRybhc1dmVUPkauEozcEcIz2s9M3xfGgtvdJWdEQFRa3OT6G8bYSbNCi/LWe317Pr9U6nOhhNfud
      eDIjjHv9hAfIEOx1U1bygfRg/7y4X5F25e0BwBERNnYwoMH1+/x2vpzdROrE2BXpJrkkZp1+a2wOMxJu
      iAPCTspbMDaHGAlv2NscYuTeHs/daRfBl+qYmFvCgMGj8MV5jvNjQIwGR/y8TIbmMW4W8+SwZikly9mQ
      iFX0iV9w75+p8MXh3z/huX+rh0/r5ZyXvXUWN9Mzx0DiVkYW0dDB++WP68m79KrvmqTaDjAuEoqgQxxP
      XcXbmihqGM30dXY12SC/a5Kc/bhsDjIS9uIyIMRFWFxmc4CRku0NCHBRFkoaEOAiZG+dAUykHahMyrKR
      Fh4OhGVZUFNp4aYQcZGhzlgm2tJCDbE8lFXSPaA5lquVeuE0nl7yesKypAXV0hCW5TEt0oo4F+KAlpM/
      5YXglp870QLCtrvMX9/LwvqcTt831gFB5/6YM4SSGmyL1epBfjW6XqzW0f3d4nZNqtcQ3OufXoZB2Osm
      1H0wPdi/Xk+eepFfNThaddcDpoNS2Z2+bxrWVVyIXVntKZoeMl20ym4gdMuH6fgHg6Om5wc3PT8Q0/OD
      k54fOOn5AU7PD+T0/OCm53z95e6a8iLLQDiWY0H3NMxgaoYLV3e3q/VyJgvTKto+pdM3m4dpj51SS4Gw
      xz09owCox0uonSBWM8tPPtOSoCdsS7PLGe0AXwcEnaSDvG3ONubl9E2PBwKyRJuspJsUZdsot/MEaI75
      enU1u59Hq/s/ZKeOdDNdFPUS8rINok7KD3dI2LqINh9/UZ1SwhQrxvsitO9p8iO0PBaBexMXnnu4aEqF
      7F0SuqUYj0XgZZIFmkcW3Cyy8OUQEZgOYjQdKK/UuiRmpb0eCrGa+W69uJrLr9LymkFBNkIO0BjIRLnz
      OjS47j79d7TdiAvCehUNsTy0SSkNsTx7mmNv86QN2wfCtCS0X5LYv0L+R6Kyapao1QyC4rJQ1Lt5DVF3
      tGlvniFQToE1INNFO7BzICxLQc2cLWFa5B8utpsNRdMhricvqJq8cC2ElVwa4noE+WqEdTVSS03iDnE9
      9c+a6pGI6RHkOy6AOy61VE2HuB7iveoQzXM/v1VfUm8Rx3k+LG8S0bYsJg8GRzRuvM0xy9X+au2OuoIa
      x8Jdf1N9i5Tq7TDER6h3TQz2VaTW2yUBq0zr7JFsbCjAdjjKyrg5moasHFDXy/nV8O993NfZnuxqKcwm
      8/C/eEZFotYk2+2YWoW63qdYPL2/oCpbyrVl8fuLbXyI7qnCHgSc6oFJs5FiSbYOqOttR+KqBpAVwL5M
      jjm9AoEcbqS9rMvKLdXdUpiN9JQPQAFvuk/oRbSlXFtRMquRHnSdshPLScgOc32irraxSCndcYcErYx0
      bCnQlm/jmqFTGOKb/iTcwkBfwU/EwpeKBS8ZCywdC8JW3Rbm+uoyL1+m73pkYZpv/WW+pC4+MyDIRWob
      DQqyESoajYFMhPG8AWmuQ1rAXcTJYtSAR2lftmGH6HDc367VZfs73PU/y6iEuXgLQ31RcdwznQodvPfz
      r9FsdXuu6ujJIxkDQlyUiXkHBJwvMoekZGFDYTbWJfakaf3rw7vfosXt5ztyQpqkz0q9XpfG7KzkAHDT
      v3mtU8G6cpM0rfI/o60sc5t4+vNIm7ON32WPbFfSbC1jmcpIHa47vVUyINOl5vnVKv+rxb2sh5uEplgB
      3PQfKtkRpezDaECmi5rn3Zze3OvrL7SdXR0Qcq5m9+0LWX9Mf9IA07A9un/4RNgkFUBhLzcpTiRgnV8F
      JIUOg25uQvQkYFXn8f1KNjYUYrtk2S4xm/z64s/mNRNqAcUcUCRewuKpys8F3jywDCpry5Gypj5vVuVx
      5ScYdnNTeekrx6qNJBsVhLii2cNfLJ8CMefV8obnlCDmXM7/yXNKEHAS+w9wz+H0V347o8OYO6gMOAY8
      Cje/mjjuD0kiTxukPg9qh2wBGiMkgXxtkvqc1y71pMd6ybZe+qyB7RTiwSLyE96f6mG5ZjTPLIPL7nJC
      2Q1qx2wBHiPkLizH6gdWu3YCPU5W+6bDPjenndNhn5vT3umw6SZPdgDzHO2gnNPUmSRo5RYUAEf8jOxr
      s4iZnSBwq9Z+yG3SXBq2s5MDacnaD8nNmIZhvkue7xL1hSSsJZgQg3LEsFeCxuI3xagEjMXMMJ7cEnIj
      vPdgGVafLMfqE26T69KInZ3aS29tRW1mBwqzURtYk0StxKbVJFErsVE1SZ81up3/D9+saMhOHKQis+b9
      nwPabnycqn0eVuZGRqrGl9ilwzdWNb4RlFC+dj1kuAob8ChByeRt51lDVgv1eS/53kuvNzThJ7T/wNd4
      fQBE5I0Z2heYNC7XvhqQwUZyV+iNGr1Hy/D6ajmlvgrrK/jH58Z3gu7GcrRW5PUd4DG6+RmvD4GP0q3P
      WX0JfJxufc7qU4yM1I3PeX0L26BFkcX7/CK6/zRXq00mmw3KsdFeYDEgx0VZ6qQhjkc9sf4u68y4SKJt
      Wk1fjIPxToRmaweitWEcU3eqHWGzQwc0nR/krfrj+vNFRNm6xwE9zmj1ZXbOFje0bT9s0gv1kib5fHYE
      B/2c898R3PT/Gm2ORZKnqsYgZTUDRJwq/2W7bCvLC8+tC+wY1AL3K1Defm2KC/2nnyjIpmoznvFEYlZ+
      ckIGKEpYhDG7Ook5LIJtsKNQ3nUdCNuiVvao88Upr+e5JGolnYkIsZi5K+VpwpP3OO5/TvPywPd3OOZX
      94Irb1m/eVYk87Cf4HrMiNYAhFxHQbw/Aq05cGm/nbBOGsFtf9fS0awdZLu6DEtzdZDtOu2m1RcCzu7n
      E1R23HafrTeI6hE5MVX/UL1LTIxwwkCf4PmE6bu7WVx9oxcdEwN9hIKiQ6CLUiwMyrb982F2w/y1Bop6
      qb9aA1En+dfrpG1l73+E4F4/NTXQXZCAj8mpgu+E1H3+dXZ/r0j6ZWskZuWktY6iXu7F+q6VnrYaqVmX
      d3/JZJ8v123z1OyPvlrc3dISw2uZEo2QRB7HlEiUhPNJ7FhdKtOTTQMRJzVxegzxkZNg4AbjcnZ7HXVv
      EE216Yxlkn9J41eSqEUsD2Em7PR9y9C8YkJyNARkiV6y+kmFyNTuaepAIcLwaURjxSNuX6Azlil9pKWg
      /L5tKOJNnka7svoeHQsR79Joc9ztUspGcaMiK+Yuk1+kbLFuUpatHVgXSbRP66eSlh4Wa5mb19JVWJKz
      pyzboZx+kFoP2A6RHpOSke110HKKNKUlmgIcB/8eCO89EHVcH2m/tUU0z9XkXWPlVw2uuTjCWEZDNI/+
      wIqyX5QDms7T0ymqUucM4/9G5+8uflEbMKhd7aP4+ecFwQvQhj26X62i+9ly9pXWUwZQ1Du99XVA1Elo
      gV3StKoXjQ/ft+JcDm/lX39SvDZrmjfZ9Cctp+9bhjwr1MlD0fT3nC3M9DWbxcp68EC6roGCbJSSqEOm
      iziHoyG2Zxcf85pa5zmkaSXOCmmI6dnl8SMp6RvAchCLqVs29f3jCVv8A6jHS81kDmy763fRtqoj2nok
      AAW8CVmXQJb94ZwukhDo+sFx/YBcKVmUApZdvK3Lip7wHQcYsx/7A1mnIMBFrIRODGAqyJ4CsNB/GPSr
      fpAtPxyLLKW0UZOJgT7ZhkayhaFWHSZrmjMRlYf4x5GUWXvIdAWcK4vgiJ98DAZMm3Zi18bpz6gEprd+
      A2XauqMPm55Os9AiupvN76P9445UP3k0Y/FU3y083MkyFq15KhcYq3VMinTxBpEu8EhFWaTcCIqFzW0X
      7g1yAygaj8m/R65lYrSLN4nm3CnmicggDLpZNRR+Tk/zKeWYvx5wHM1lM3r9Fgp7Gf11C4W9Td+0KvfE
      yR7UgEepy7AYdemLUFNPaAFhy93mF84tNUjQyrmhBglaA24nJEBjsG6mi5t+wR8RCd+ISDB7+wLt7QtG
      D12APXTB688KrD9LWdt1+r5riA5CkNtAAwScVfxC1knGNv2d0ix/W23+8UA5OWkgTAvtZIeBgCwB3UJQ
      AMbg3FELBb3EuzpQg42y2thcW6z+RTsibCAsC+WQsB6wHORjwkzKstEOCtMQw3Nx8QtBIb9t0+T07RnH
      REzjE+J4yCkzQKbrw0eK5MNHm6anzYlxTNS06RDHw8mDBocbP+Xl9rvgelvasdPvZQ8ZrveXlHwuv23T
      5HvZM46JeC9PiOMhp80AGa4P5xcEify2TUe0ktIRkIWcygYHGomprWOgj5zqJug4Ob8Y/rWMXwr+Sk4d
      YXCOkZVmTnot7r/MVl8iQovVE5rlfvbH/IJ8TreFgT7CRKZJObb+2dBePBKVOup41Z6rqequkbUaqVlJ
      S7Ds1Vftv6nbWpvUYFsvH1braH33x/w2urpZzG/XzaQeYRSGG7xRNuljVkSZEMe42KYBwUzRhJhVmqT7
      A+V8zgkqb1z590w8vcWPtUxTor7Jz3Vc/siEGgLBvX5CjQHTXruaBRBVFVgGNAscTZ2XPV+GlDbT4I3C
      vSMa7vWrDBkSoOG9EZj3fKC9dpWx031AgFYwIQZlaO+VeGOp3LdP61hNZQVmL1s1Gjeg7LgWOJpk2//g
      5mtDAMdoz77tZ7NPScCJhqjguOnPQ1pl+7Soo+dzTjRDMB5DdlL2m9A4jWRKrOfyUO3CozUaOB43S+A5
      QV9yxDHrPByBWbkZtdrDar5sD4AlJYGFgb7p4yMDAl2En2pSmm39+VItE5m880MPWI7DkehQwOD46+LD
      h/PJO7y037ZplScOcVbRLCfKsXVPg5pnTV11QzQDBi3Kh3e//flevZ+jNgtoH/9TDrfEeDCC2oclJILB
      gxEI77CYFGaL4jyLBc/Zsqg5z6a/uA+gqJebuqMp234aie8hcomDfuJbOC4JWpOLjGGUFGij1MIWBvpk
      BcbQSQqzUTZZc0nQml1wjJICbdy8iefLNlPxfnfPgmbSchebw43R7sCVShT0PjdrFguGtiMda3dynmwx
      RLqlzDRgvBNBVgjnjMx1wiCfetWoSOJKvfFSp4WaFhN0PWQBo8m0O6YMf8PhxmhTljlX28Aj7ohcAh3e
      E4FeZgzWYz5un+KK7W5ox95UAIxqvecc45BpWBWIjTt+VVfTW7WOAm28Eq6RsLWmvLPqgKCTXT5M2OOm
      3zCDdcztgkpGT28AHWeX6pxsq6OAt4629U+ysqFAG6e17znX2GQM1s8eSNMazW5+v1tSXlQ0KchGOfLW
      pEBbcuTYkiNsoyaehoE+yr4/Fgb6ODcCuw+EeQmTAm2C90sF9kubSdiEZ5Sg7Vyvl4tPD+t5tJqvyalo
      wah7Wx4LrrphcTNp71QQHnFHm9fodnEdFKJzTIh09+m/gyNJx4RI9c86OJJ0oJHI9Y9OolZ6PWSgqLd9
      G5IwqY/x/gjl5l+yJQ2J0Rr8USgHyWI8GoFdR3jqB3KNq5OoVVZ45yH3tOf9EYLuqWawojR7IM0e/qJn
      eYPErMTbqHGYkXoTdRBzkkdCFmp7F7efGel5oiBbM/LIHou4PlYMrYFDfup9ahnIRL4/HQS5mr5EmWS7
      LE3oUp227csb+p6lLolZqak5cJiRnKoaCDi/ztdf7q55v15jcTPnegcU8MZJ8i6q0ufyOzUrWDDsPlcz
      G9T5PgeG3epTjlZxgLF9eVMcszrdkLU6DLmJY8OOAUxJmqfqpUXGTx9QyJvtdnSjhEAXZXNqC4N8R3rq
      ub1Q9VdWwURKZNPXkr1otZU42anDHrdIqyzO2fYWx/y82XKIxyLksahpS58xHotQyIsIiTDwWARm78DB
      YX+0nP9598f8miM/sYiZU0V0HG7kDKdd3O+nDqJd3O/fVlmdbXnFynZ4ItFnTRzaYyc+C7BZxNys3qxY
      4hZFvGEVwWg90GxkQh8rOjRiD6tkRuuYoY6gPs+GDUgU4nsGEAuYGV1ysDe+j+vtE1nVUICN002G+8eM
      IeyJwmzElQAGCDibOYiAAmbxWISAQmDxcATmRn4eBRKnrahIO99iPBKBXxuJkdpIBJRj4S3HlI0RDAhx
      UR8pGiDkLBm9bAUBLtoWBxYG+GibHViY5et3TCc/nTRIzBrwVARxTIhE7dAhDjQSdXxokKiVPFbE9vC3
      PmwOueJ0QWGFNw65EnJxr58xeQ4J0BjcIuArAdS+AXKGgfWZCL+rYspdFWF3VYzdVRF6VwV2V3nzwtic
      MGv2Fpm5vbm7++PhXtUy5FXfNoua5d8e04remwQNaJSub8KYNkIcaCRxpGcSh4bt27piXbviYCPl9ACb
      Q4zUfKxxsPEpFrLbl1Uc64mFzZTjPm0ONlLL3YDBPvF0rJPypeBIT6xlblYiz2/Xy8Wc3JOyWMz8LaAz
      hUmmxKJ2pzDJlFjUZSaYBI9F7byZKO4ll1CLxc2sjhXA+yMwGmHQgEfJ2HZfmaDWDSaKe0XKvlyR1l5v
      0N0Uo3dTBN9N4b2bi9v1fHk7u2HdUA2G3M3j0qKuXunmHvV62ZWnbRiNwqo2bcNoFFaFaRugKNRHyCcI
      cp2eBPNurE6DdvrjX40DjZw2Amkd2nSmP5yxYcjNa3Ow1qZdlEh8HGOQiJV743sU8zbb/LNLtG0YjcIq
      0bYBi1Izn3ZCgrEY7B9So888m6+ocQFdrCjMFpV5wjMqErJyGi24rWL1PJA+R1mkeVYwCnMHQk764H/A
      UB/hOB+X9FmpT6lsGHKz+nBu703m9vlV+361eiOvlnUSbdIGEsAxmppU/YHj72HUTV/rbbGwOUt+cudo
      QAMcpUrrKkuf08BQgGYkHv1ZMWiAo7RPeRgdBIC3ItyrM+3JfYSegmzUOu8E2a72uNrbu2tONeXQtv3h
      E++XDxxsJG6koGGo7127RT5T29GwPWNdbIZcK/nO9xjsE7y0FFhaiqC0FHhaLu/vVnPqji86hxgZO5HY
      LGImvy2pgx4nfQ2DQ/vsIkwv/P7mUUPC1be03x50/b3AE4PeRji0xx6QON6Uqauj4F91QyN2ehXSc5ZR
      7fjEe15okJiVWBNrHGak1sY6CDiblx/iuq7I0p70WTnjWkgwFoM6roUEYzGoE26QAI7BXSDv4qN+8sJP
      WAHEaV9MYRw5hhuAKN2UICvHaixkpk8mDhjkI7bwHQOY+qRn3TyDBuysig+p8wLeY3Bx2H8epfs4yznu
      DoW9vCx1Aj1ObhVo8SMROBWgxfsi0DsgLo74jfwpWDFMxVicwBiY/3DccCq9AUW8/DX7oAGL0s6H0Dv6
      kACJwVlPbLGAmdHFAntXnI4V3Keiz2v0FGajTr7qIOrcHZjOHdRKifCyLKaUZcEva8JX1kRoKRDjpUAE
      lALhLQXkVfUnCHGRV9XrIOCsS/rktsYBRsZa+AFzfM37jfz3yCEBHoP8xqTFImbmG9sujvnJPdqeQ4yM
      vucAIs6QN44Rhy+S2pRgG6tN766pbyx5PL6I7XrZ2+N+k1b8eLoFj8bOTPD7vdanvK4xpBiPQ+8gQ4rx
      OKyl+R7PSEROxxwwjEShvgMM8EiEjHfxGXbF9F5czyFG1e6+QSF3NZ54wUXcllixVovf6XXvCQJc5OcO
      Jwh27TmuPeAi5q4WATzUXNUxtml9t5w3J9FxngA5NGqn31kDRb1Nu0HehATgRyI8xVkRFEIJRmIcq0qd
      C7MlvgCCa6bFY2x74DX5o9IfikKC0RhNChCHC6hlJFqZZ9vXqObncFvjjyfqsgqK1Aj8MWTzqx51EXfF
      wiS+WOehZet8vGydB+fx8wl5O/SHjP+OoWwHVXiGxhsvraoyINVafjyCHOYd6qfQOK3FH+0n/W0H0DAW
      RTa07TrbsFC9ZiTeQVYdWd1VIUEhDRMalfxSnYmiXnKfRidR6+FYHUqh9qp/kt1P7oVbFjRas3hHNr6C
      Gafn/RFC2lEx3o42r2Pza5kT7vcH1JditL7UtkQJiNEZRqLwa6+e90YIqYfFaD0sgmtGMaFmVN/Z5fFj
      QLloeW+ErpQGxOgM3ih1tg8JoXC/n7xKCeC9Edop52i7CYjSO9BIXf9PnS60/c6MZDjQSH+nVckMoFDQ
      q2a2mXXgCcW9rEFeR6LWvCy/s4bwAwy6maN3dOSu7QbPqQ50HPdzW8iRUWY75JD3lnnlHexx8/oOPYuZ
      uW8qQAI0hvptzMyt47i/WY8VEODEj0RohntJUJBWMRJnmH4NijVo8Hjs+T2NRu3tpkzcu9LRXjt7CG8K
      0Bht9RdSsg3FaBx2KdcNaBTGk2gbHnHz+g6Po/2GvIxVW9TmZk4SmQIwBm+ciY0xm+GUbEEzFTDOgybP
      UBcW+Zzdzg0w5g6pzcVYbS4Ca3MxWpuL8NpcTKnNxdvU5mJqbS6CanMxUpvrW4ke4vpJMGMYDk8k3tjZ
      P24OGWv6x5kiqK0TI22dCG3rxHhbJ8LbOjGlrRPBbZ2Y0NaFjfnHxvshY3H/OFyEtNHC30aHju/Hx/aM
      PWR10HKulw8r8in2AwXaOPWjQYJW8pqCAUN99IWdFouZGe8YWixqpq/wsVjUTK+1LRY108uxxYJm6lt/
      PYXZWHPWDm3Z/5wxTn85QYCL+BDlT2iHLfVHaj+8Y2zTfLn4/C26ny1nX9tTmRgPwjDJaKw63hD310Qc
      I5HOo6eSmIFhhS+OqvwqRiHEJL5Y9Axp0z47uap26DE7veKGFaNxDmlavUGsk2YkHqNyhxVjcehdf1gx
      FicwN2Mti/ElzqNlSOCLwZjcB3hfBHJ1bME+t5pt4MsVPWZnvISJOEYjhdXEvWI0TnYIjJIdJsSIYrEN
      jqMko7HCarFeMRqnabqzVATGOmlG4oXWZGJKTSbCazIxpSZTX1J58w1i9ZqxeJwBPCYZi0V+dA8aRqOQ
      Bxuwwhen6TSyBrq4xorHfvfM885Z81GVNq8kMjYGdnHI3yQeW6/Trp38/hH8hlxzYgK9mzpgoI/czA6Y
      5WtWV/HPhXVx0M+YSdJBx6nCxd+J0x4DBvq2McO2jUEXvY+icaCR3BcZMNBH7HOcIMRF7lvoIOykP8vx
      PMEJ2yFmbHeY7nNG82aQoJXexGicbSRur+3urC3/0i8rJzexNgy4WU7AxXwfGX0PmbFDD7g7D/U9Zvf9
      5aaGoE+qDJjlk/+VaCfixPJfjJN1UAsSjbNAyWJtMzVFgLRo5k/iY/1UyjH6K+fxHGjwR5HVCXX+HjT4
      ozDuKWiAojDfePe/6d7Om5X1bFdz7sGJRKyf0h317SoThbzt/h7RJqtFzbhkA4f87Fdzx966D9g7y7tv
      Vvthty8JN5+bPBSh3gh1CXH+SLcPLGQ+UreS6SnXxpm4QncOaz4ot+JA1ynKtUXaxrRUp84C5mZ5UFbs
      SrK3JwHrad1J8524SmOy3TGMRaEeXAYJJsSI0uI5OI6SjMUinxgHGqZECf9JJ4sn2ql/HnKbNAcQifP2
      DP42YdA7hCNvDnL2T4H3TQnYL8W7T0rA/ijefVFC90MZ3weFv/+Jb98T7n4n+D4n/daBSZo0redRxI8p
      R24psDjN1mn0CWWAByJwTzR/9J5mrj7lJ40vRbhdV0/Pld9x9fVbm5WbeVqQnR0HGel75KF7UT6G7Enz
      6N+LJmyPy7H9LYP2thzZ15K7pyW+n6XaxoadafeeXLvnZ9s9nm/3atInipN/0Zw9ZvmceQvyXBloGI1C
      PrwKVsBxVL7h/o4T6zFzr72HR9zkY7gggR2D1mA7KzVk/ZQl9Kc5Awb6yE9zBszyNS/FnN7HoHfwXRz1
      B7hRL/+S4aulLnRx17aowbJMafq2ujpoOQ9xJdJoV5X7aHPc7Yi1rUPb9nZ/oeYhAE2sgbAzT5/T/DQP
      lqQcu6XwxVGfM/rYiAOO1Hyu7QLFiWQ7RiPRF60ijrFIP45xnu0y2dyHRRs8cES1lxV9/t2GPe7mKpo7
      yo4wKMbisBYVoZaxaEfZir9RSEPlidsWDXbJsh12JHJVCdaRnJ3HkV3HuYc94uc8svYwR/Yv72b9GQ8Y
      DdKyditnmiXaJKkOWs52XR5nhGCQiJUxQjBRyDsMz+L8saTLTd4f4TnOj2lIiEbgxmDNOuI79YiAuRTh
      nUsR3FkPgc96CPash/DMejD38Uf38A/aN3dkv9ygswFGzgXgngmAnwdAPgsAOAeAdQYAsv//ULqSI3Eg
      bKKol97eWaxt1m4XefBuwz43efju0GN28gAeNDhRDoeyUjtF9XPGxBgOb0VgzSwh80qnP1O7MhpnG9tT
      KdSBEjTjwNnGZhksvaugcZaRsdoTXOfJeHMafF/69JYzdZMvjcON3a6kopaF+ZGrNyRmrLjmnZSoc7iR
      8VwPwP1+4vM9APf7iacjArjjZ571Z5KOtRmmqT4ZL1VsHPJzLhk+SU77gJdJvKfIWZ+zEsObQ/jnxzmw
      6X5+z3k7YKAcG2+tqgE6Tsbz/4HCbIxs4MA+NzETOLDPzVkLABvQKOSMZrODOb7Iot/nt/Pl7Ca6nX2d
      T7XanGlc3Et4OV+tKLoeQlzR7RVLJznTmB0IW4P0gObYZFGdyh7JJk6iY/GiVgvX6V529uJqch/CK/HH
      eqnK4lF2Yh4zQRgAj5uAqNu83MiRYlSdvyPH0Viv+TzAfO41XwSYL7zm9wHm917zLwHmX7zmDwHmDz7z
      JV986fP+xvf+5vPGP/ni+KfPvDnwzZuD1xxwzRvvNW8DzFuvOcn45iTzmgOuOfFeswi4ZuG75p/7Pb8K
      VbDffR7iPh9xB134+diVh1362LVfBNkvRuzvg+zvR+y/BNl/GbF/CLJ/8NuDkn0k1YMSfSTNg5J8JMWD
      EnwkvT+GuD/63b+GuH/1uy9D3Jd+928hbqgH0QzWZbe53Y8qyap0W59WEpNj+WRA7GZPj7CIrgKIU1fx
      Xj1/L1Kyf0ABbzfiqNL6WBVktUHjdlHH0ydeQdjnLg98dan37lJxfnH5uN2L7DmS/4i+T16PAaBeb5QW
      2+jneYC+MyBRknTLcksOMabbTRNyk5fTl5XhBiyK/HwvHqOfv/BC9PiY/zLMf4n4vyc7llhyhvHiw0du
      PrRRr5eeDxEDEoWWDw0OMXLzIWLAonDyIYSP+S/D/JeIn5YPDc4wRtu6atonwkoJCzN9Ty/RdrNVP6B6
      PdQUpUm61rp6f3H6tL23gqoHFE4cmTMZV95Rjq3LiwyjRrpWnhGxtbuWtYlCzAYuDdpPSc6za7RpL0p+
      brNZyByY41AJEIuR63QOMHLTBE+PgHwC8UgEZl6BeCNCVwE+NbukfSQdfAnTuD1IPuaWHf3X5+lPuTAe
      itB9FD2VVUF4voHwRoQii+SXGNncBCEnPaOboOYUxXmUlFGcTN4hTUMsj2rCKSvmDQhwkfKUDgGuKiUd
      PW1zgFHEz3SdgizXYypzTpxnf6dJs0CqLqN6TxKDBieKOqClzLaprDJyOS6ffiYnxgMRdlmaJ9Ghprt7
      0rJmdbqPtuV+I/9Cz1wObdmrdNc8pFaFrZkhaUbSlPMYRzRYPFVtl0XKi9LBllsE3mExeoeP9ZaZQw1y
      sG7S9Bjty0QWWrXyVq0WryibpWG8FiEru1kvIbsh1NNoYdq075JIPJXHvJkxmv5MHkBNr9pFUOYktaxT
      JVt3AepPcZKQfoHfZEZVH9LTaKBcm1qxLv+bquswzVdEsdrW6LiRBboQNSmfAKxpTpLopaym74ukM4Zp
      Wx5eyaoBMlyJ7GBwfqvBGcb050Hed4KqBQzHLquFLHDkH2lwplG997kvi/qx3KeEIuSQPmsk9nGe890t
      b0R4jOuntPpAcHaEYZFJUsXFY0pOUBM0nULtuNZU6WSrhdreKs3jOntO81e10p+ULwHasP8r3pabjCBs
      AcORb/esMmNwpjEVIqqf4kLPDEuKGhQgMai3yyIN6z7L82Yhiez+kDrTEOsx17L3STk3EBVYMYpMFrno
      JUumb1Bvc6axTNpTqBn5w2FBM/XuGZxjlJVvtIllt+aCfcmQAoyjsia5inRhx931zN61xZ0fBvVgEdlJ
      5vBoBGr957CoWcixf1oHBdAVTpxcPGU7deQ2M40cHokQGMDj3x/zkMYdUzhxuP1NhwXNnPqi5xzj8fwj
      +1oN1jLLola8I/kawrTIxGbVkDrnGNXQPv6FqGsh2HXJcV0CLsZd0DnHqNKUKFMI6GF0XG3U8ZIL4Ilx
      TJwc4uaOUuaZonn1WHU7y81zVh6F7HXKG3YohexxECKMuszIRTPPwRrPOKxhPpQvtLvWAoajUuN+3njD
      Rl1v1+Y036GKddY0p8lxm8qk2ZKcA4XZ1ADqkMdcbY9bfpH9zUhbDTN9XUtLFuocYDyld/MPstegITvv
      coGrFdu4rmm5/oSYnmZKk3xdOmb5avYIxWEds6jleGjLuFoTdbwcIWD6UV3+lNm/VmcyUip9E7Sd9NZ8
      gGDXJcd1CbjorbnBOUZqa9kzjol8R0+MbfrJvqU/0XvK6OHCvVujTSSnHkAb9iN3UuCIzwgcuQOHIz5q
      eCFP374487elehdfCLWz4EEdnZXvmodVk50IP0TYXmTRbHV7Hn1arKPVWgmmygEU8C5u1/Pf50uytOMA
      492n/55frcnCFtN8m00zVFEznMXk9Ycm5dqOW3ERbVKqrsMAX717zxJ2HGi8ZNguTZN6CKz+GhF2bbY5
      3dicM0e+Fzrl2sj3wsAAH/lemBxovGTY9HvxFMv/XTSb/b2ev3/3ISoPhDsC0j67SKe3NzCt2dXilrJZ
      6bLN1bgwLdQCoMk1JsYPERJV+K+u1Kve1/PV1XJxv17c3U71w7Rl59Wdia/uHD78es/VnkjIend3M5/d
      0p0tBxjntw9f58vZen5Nlg4o4O22EVj87/x6vZi+AwHG4xGYqWzQgH0x+8A09yRkpbWoCdqi9p/cPtzc
      kHUKAly01jnBWufhg6v1nF26dBhw38u/r2efbug5qyd9VuZFWzwQYTX/58P89moezW6/kfU6DLrXTO0a
      Ma4/njNToichK6dCQGqB9bd7hktCgOvhdvHnfLli1ykWD0VYX7F+fMeBxs+X3MvtUcD752K14JcDg7bs
      D+svElx/k5Xa57uukSYFgARYjD/m3xbXPHuDWt5jXd63Rzz9MX0FuUua1k+z1eIqurq7lck1k/UHKTUc
      2HRfzZfrxefFlWyl7+9uFleLOckO4JZ/eRNdL1br6P6OeuUWanqvvxziKt4LivDEwKaIsDTO5izjYinb
      u7vlN3rhsFDbu7q/mX1bz/9a05w95vi6xCXqOgqzkbaUAlDLu5rxipQBepzkG2/DPvf0Tbwh1jUfN3m2
      ZSTEiXOMxDMZTQqzMZJUI1ErOTEH0HWuFr9TbRJxPIxq6ASZrvkV46p6yHbdqwhpTTibweYcI6sQ6hxu
      pOYXm/WYaXnGQm0vo7D0EOKi/3S0pAwfUX80Vk7m14v72XL9jVqh65xl/Gs9v72eX6veU/Swmv1O8zq0
      aefsaZigexran6y4SqvvslitHiTBbH9d2rTfzterq9n9PFrd/zG7ophNErcuuNKF5bxbL2QHcv6Z5DtB
      putu/WW+pN72HjJd939crabvIDUQkIVavAcKtNEKdg+5rl+pnl8BB+fH/Qr/tkt+YwDgfj89ES89rULz
      uZrY+bOpldSYk6w38VE/K4VcxXgcRko5BigK6/qRK+Zco3NVauz6jXzregqy/fNhdsMznkjLurz761sz
      4G5TtmkLV8RHHqgEitVeDV3fcpaR3HGCek28LhPWX2J1lpCeEq93jPWNAypDXz3IrgI9tR9nQIqMRpfc
      kf4SH+kvQ0b6S/9Ifxkw0l96R/pL5kh/iY709U84yaCzHjM9ETTU8Ub3q1UkBxKzryuiViMBK7kuWiIz
      Hkv2jMfSM+Ox5M54LPEZj4eV7Ok2XWeKcKBMm9pdnuJR33cN0ezm97sl1dNSmG3F060g33q9XHx6WM/p
      yhMJWR/+ovse/gJMTSvO0Z1AyCl7BXSfhCDX8oauWt7AJnK/2gARJ7HM6hxipJVXDQN8rA6eSfqsK74W
      Ki3UsXcPIa5ofrtefmMZWxTw0it+DQN8hDOydAY28XL4CUScnBzecYiRkcNbDPT9efcHbWGRzgFG4vT5
      iQFMf87otZdkABPnHsDpz0h7I91FHDV7wOzT6S9JGJDpao7yjg70Jw0AO5jTbfT75+5FZsKJLRYG+5JN
      zvFJDPbt0jzdd4elv9bTD1j2OXyR9secH0LCPrf4UfHdEva56zI0fU4GOMpjVR4PkfxzNv3MSYz3RaDs
      3ADTPnuz7dOxmr6XmUcBx1FXEB2qVL0uyQmi83AEZg5F86Za+qt2TWBKG9ZnrrdPfLWEcXdAMmu4x9+M
      nMN+gu5wIsnCUKtTM7dlkqo3+fK4UvvRUAsxpnHiiWx/yJtjZaOf0bYsqyQr4pp65xELFi2wBkcs/mjM
      2hB0YJECakTA4I/yyKy3YIk/FqMGdnh/BPEWv0aM/ZpmbxDmL2lZ1CyiWNXU6s7Vr8wIhsMTqSxC0koT
      YDEOZVbUza5svBAD74/Az1cD74+gsoQstWE3BlR544oo/XGM84BwncGIEu/Uf3W7fsUFOQbIQxHat77p
      5paDjDLhTmHpWg023dRhlc4Ypk32WByb+r2p6Ak+i0SsbQvM0rao4Q1orL0ttOr6HOs0ermdfaY4Nczw
      tY0mbTjZM4CJmt81CrCxuh/ePkf7YZE+koWSgUyynlab6Eb7WHynO3UasJMLuY5BvuOGLjtuAJPqZjX5
      n+zrScTKuttgr0/1nPSCJCsWsh51jEYi1ye4xIzV9KOK9IWiPjGG6SkWTyrlmn5GdHh/+Uv0c6/2+40/
      nF9EQrwco6SKd/W7XwmhpkvBa+nGQTbHvw6/0LgG5iQAOvbvG3F5GW0zSbC68IibPODFFEacw/f0ldp+
      94xpanpoTbV8LFRaVakQKaXdQQxAlGbnLmr5s1Gvlzr3AvJjEWj3Exb4Y9BzO6YYidPMpwSFaQxTooQn
      HDr7cxplEFtlHQN99akADrW/YPghDRCP0cqaoOls7z8jVQzQcKrd1sqme9T0jshFGeSNCN2dpnV8Bwhy
      NZ1Y6vEACA75WZ1hh0XN9M0AUQEUIyue3wXFsARgDEE6F8MBIae5AytdbfJQBNpgZIAgV7v3H13XcpCR
      XKwNDjSSBiEDBLkYVZlFItaQW47sjol8QWVsfq2Bqsy47byYiHfd1BUlkM2a5nY+LLyQ+zyeiG+SlNOM
      +lW0T2/+vvjwMYqff170ezASRiioAolD3WEXhBE3qQoyOcQo+x9hV6wLPDHUXoFBMU4CJEbb8SF1EyB6
      zE4eH3ok3lhJKfu2IXFaARLjlIc/sAL09Ij91yA7Vr6CchKQi5KLDx/Of2NMgNug66QPym1wcKqNxB6b
      yRJZC031GRDkarYmo9saDPKpcyTpOkVBNiFE+p6uazDLJ6+3JqfcCYJc9JQbMMhHTrmegmz0lBsw09fM
      mhET7sQAJnKyDRRgoyZaDwEucpIN1GDLLuKAPf1g2rLz9rQDUMBL3L3N5gAjbcc1CwN8tB1pLEz3bbm7
      IwIo4CWn5BZNySQoRyUjOSrhp0PiS4eEuUukS0JW2i6RNgcYOSUq8ZWoJGiXSIzHIzBTGdklsv+cvEuk
      S0JWaulIfKWDukukAQEuap2VYHVWwt8lEoQBN3mXSJf0WZkXje4S2X+Ds0skCIPuNVO7RozkXSJdErJy
      KgSkFqDsEmlAgIu5SyTGQxFou0TaHGik7hIJoICXtUskTFv2kF0iUQEWg7RLJICaXvZ+jiBsugP2c0Rw
      y8/bzxFATS91P0edgU2U965szjLy9nMEUNtL3s/RwhwfcT8pk8JspHc7AdTycnZ5cECPk3zj8V0e3I+n
      v4IHsa6ZusuDzTlG4kuuJoXZGEkK7m5gfUZOTGh3g9NHhFc/NcTxMKohdz9H9Wfyfo4GZLvo+znanGNk
      FUJ4P0f7E2p+wfdzdD6l5Rl0P8f2Q0ZhAfZzNP5M/+loSeHs52hzlpGxn6PNWUb2fo4wbdo5+znaHG5c
      cZVW34W/nyNMm3befo4u+f9bO5cet20ojO77T7rraBKk66KbAAEKaIpuCVmmbcG2pIi0M5NfX1KWLV3y
      UtZ3ld1gxHOoFym+fJm2fpVKvwZONJ4jgagLjudIIOrC4jmOBGdBizcXz3Hyf6xgM/Ec7//+gnq+MA7J
      xX3hr20SMfFrvWskZkbxPB/8hsaG2VxWXsnTq1h3BU/Pvq62a69gUDzPZ92V3AxMLrJYmwn8qV90t+Zi
      baYSCe7WTKzNMY3o/BNnLDnH6KzgWJuU4mxorM2YDKxrY23OSri8sFibIRcY4UYt16KVNWdTbVlRQzbR
      ipX1XFL9lhVV+1ytLq7QZ+pyyWBBYqQgl47C5OlRmHzNKEw+PwqTrxiFyWdHYXLhKEyeHIWRxtrk2Bkz
      fhPYWJvDQUGszZhkrHBdlCdGo3LxaFQ+MxqVS0ej8vRoFB5rk1LUhsTavKePDVisTUqlbG8y3RvnQ2Nt
      xiRnXR4cc8owJjTWZgRyTiDWJoE4V/4NV+XfeBPcrk7E2iSHwDLLx9okR7DyysbaJAfsxoiEjmOMoiZj
      KnpnfOxNruXKHzrSwkTvJP/GoncyKOPFPyVs9M7HASB655ThTbIyE0fvJIckZSaK3kmOCMpMGL1zcgCK
      3hlyjBGcLImjdz7+C0TvnDKMSfIM+PsvuPfsfZfUU1Ed1WlxxRegvNe/NULvgPJeoTPwNX5iCG/0E2zq
      M/JVkGZuFWR0UIGL1RICJg94TaFJrik0a9btmfl1e1a2xtCm1hhe5et3r3Prd6/Cuatrcu7qKp27uqbm
      ro5/NV1V711q15l5+97Zf38srus4dt78Tddr5A6f+P9pde0P68I09Zv1qf8ubLE4gwSfyuG/4nRZ/utb
      jp03I/eGx0f/SV/1qf+dXN1sF/8EjlKhzf0p0T2wie+gtvqkl0cKewDU0RQnd7rdHtHcGWLadRo5F5+c
      8FVtgECOD4A4gChHt9SUvpxVZfXyRStThpg67UqCviL3446wHnVc/nUNMOIztvO/TANUAzFazttPanNq
      yqPaunLufxKrF0fa4Nip+fNwtDBnkZ3nxxya28akaHslwEZfeyzNS+aff1fYqqmNKspSt7YAfjI754hy
      8j/H3C+v4igV2dqNVrouu48WC9uZwKn/i9pc6i12H+5MaGqLzmh10AXwNsQktf7Zn/9W9+ePSAk4cZ43
      tjnqWun39sW9h67GXmyN0ZS3PFW6tv0TxcO7LFCl8nWvj38/oYoobUjnYpX7MjSdcqdiXVNCmlWgSeVX
      GXPR3S+5m6wqlW/n3kdZNp5MWU21r2VWT6asl3rFuzzAvDuTl5JMzXp/WSnJkFKSrS4l2YJSkv2aUpIt
      LSXZryslGVJKMnEpyWZKSSYuJdlMKcnWlJKMKSWNa2l8qLIoD/rW9t8CfTKeTtmBVnsEJpxGW5HScWmj
      Ohdti7zsCT7KoW8oCm7Dg+ONQFckwCKf7/j1UZ5x5xTlvYIrf3C88YyEU4xA4vxQ+XdkJ5QJMnp8cD9f
      zx1dQeujUm0uu532IxWu+eqb2YuL7XPTJFfJHlEdv0dUN+7zdIs0CXxfOJaa3Z+FD7oBtoUZlPe2tyUj
      yrrbZ9zdO0tyiCR8Xr6OVl3xQ5LFnU2Zf2qZ9aemRjgaD4GI66d6+SP7pPaFPejucx8XDJAyNGf3UbVk
      5jvJWWv3DLNOb4VqgnN+dyzziYR+gnN+UxbWym86wVn/906qHsjRarJKNDcRcoxRMjfBwhP3oXgRDzGx
      MHH78Fsr7BxO/D5a+Ao/h0/87t9at9A+LlMmMCHjxw+AcajWdrDHQ9R1aRHJpSX0Dmh/D8kpDzSEhuSE
      x8avHwB1GGWazmrkQh4MMQFNxVvqkFb15XTCFD1CPcv3e7ilJnTbIO+DSx3S6DO9I6zH9dUEKkdR22X5
      8PuQnPBA3+qWOqT73sDuUpeY5oFR36HaQefj01NDA5UZn5zwVz9vBwj69MSARIAeko+89Y+472Mv381l
      yoym6/2jiM+gMyj1SmbQQy5tfJMq39JOoLAx6MT7qgrfcq4W16gjQS0nixhOltCbsqkNwPfpiaF0XVvE
      0Kenhu7koxNvgc2lKBXZgNp9JCJL18+/g6IbFLq2mIU+Ydcoce0t929A8mCISb9bdbwAmhtAHO7bYQ7a
      WPCEphjxVdsW0LjUlK53DYK75AF/qDY+Fmf9AZ3GBCM+X0Avptgjb/KDIaa6OPvtL2pju8Jv0QcIQ5R6
      jaqKz+pUGaTemFCBrQTalg+AOJrStH5u2b0hyDOYYrGvbvqxJdQ3YMTXlhWgcakpPQz3ip5kDHPuYQBZ
      IL6TxGrAQmWiUmXgL5uJvmxN2+0Ek3EhxxpXTcM987A5SibgEjjrXzUV9szD5ohMggUY60OmvwKM9YET
      XzE5sbaFNqrclPdVJYulIRg5bfeaPdaq9KMrBpQzhjAXcPycQKFLdAcSV+97b0M2ULngYM59vysi9wQe
      3e/CUPjvyUj4w5G9RrZmIBDn8mW3L7roJiIzCi6f9qV98fuMtBmewcjOml9XmF9Z82u/q6OffhXc8CnN
      2W97r/hY8bh7ZOfN0JZ9ScGTPMzZr6UFt9V7bmJzXb6PEoE4l22gT18ERk54Uuw9uUPFcMSU4O5WITcx
      +l++bKu971j1s4TFad90lT0s7v+mDXwuV91Vuw9oVWYCD/xt5zdl6WcUjVFYjL6kIMijn3K2733dYDA7
      RRmvz9TXDPYd9o4o9frxlr4GdgcPGvIGaOS9rT5x3XtdmwoYAkrgkd/lCW9pxqCR99Q0R+O6oUettq5P
      6nu6oJ4xRLncOtBAtUex33/7H2QijAAVlQQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
