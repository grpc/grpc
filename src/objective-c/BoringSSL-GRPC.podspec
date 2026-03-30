

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
  version = '0.0.43'
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
    :commit => "617634bc015344093f5ea0b0a5b653c924cfa20d",
  }

  s.ios.deployment_target = '15.0'
  s.osx.deployment_target = '11.0'
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
  s.header_mappings_dir = 'include/openssl'

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
    ss.header_mappings_dir = 'include/openssl'
    ss.private_header_files = 'include/openssl/time.h'
    ss.source_files = 'include/openssl/*.h',
                      'include/openssl/**/*.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = 'src'

    ss.resource_bundles = {
      s.module_name => 'src/PrivacyInfo.xcprivacy'
    }

    ss.source_files = 'ssl/*.{h,c,cc}',
                      'ssl/**/*.{h,c,cc}',
                      'crypto/*.{h,c,cc}',
                      'crypto/**/*.{h,c,cc,inc}',
                      # We have to include fiat because spake25519 depends on it
                      'third_party/fiat/*.{h,c,cc}',
                      # Include the err_data.c pre-generated in boringssl's master-with-bazel branch
                      'gen/crypto/err_data.cc'

    ss.private_header_files = 'ssl/*.h',
                              'ssl/**/*.h',
                              'crypto/*.h',
                              'crypto/**/*.h',
                              'third_party/fiat/*.h'
    ss.exclude_files = '**/*_test.*',
                       '**/test_*.*',
                       '**/test/*.*'

    ss.dependency "#{s.name}/Interface", version
  end

  s.pod_target_xcconfig = {
    # Do not let include/openssl/time.h override system API
    'USE_HEADERMAP' => 'NO',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
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
    base64 --decode $opts <<EOF | gunzip > include/openssl/boringssl_prefix_symbols.h
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oALS9W3PbyJbn+96fwtH9MhPRZ29LLrtc
      c55kma5S25bUolxdNS8IkEiS2AYBGAnq4k9/MgGQyMtaCayVOhM7esoi/r8/kPd7/vOfr7aiFE3aiuzV
      6vn0j2RVNXm5lbJI6kZs8qdkJ9JMNP+Qu1dV+epD9+ty+eXVutrv8/b/vDpf/fJL+ubX12fpL7++fy/O
      xOb9u3dZtv51c/bLm9fvXqdvsrPf3q1/+7d/++c/X11W9XOTb3ftq/PXZ+9f3e+EQbw4tLuqkeo5/eiX
      fC1KqV7uUCr7V6169KJO1+r/G375z1d/ikbm6qXO//H61f/SD/z78NO//+//VyOeq8Orffr8qqzaVwcp
      FCOXrzZ5IV6Jp7Wo21d5qT+jLvK0XItXj3m763wGyj804++BUa3aVD2eKkGt/rUxH3yVtsNL6/+3a9ta
      /p9//vPx8fEfaffG/6ia7T+L/ln5zy9Xl4vr5eL/UW89qL6VhZDyVSN+HPKmj460Vm+1TlfqXYv08VXV
      vEq3jVC/tZV+68cmb1Wo/ecrWW3ax7QRGpPlsm3y1aG1Au34jurTzQdUsKXlq3+/WL66Wv77qw8Xy6vl
      f2rI/1zd/3Hz7f7V/1zc3V1c318tlq9u7l5d3lx/vLq/urlW//r06uL671efr64//ucroYJM+YgnlVbU
      F6jXzHVwiqwLu6UQ1itsqv6VZC3W+SZfq08rt4d0K15tqwfRlOqLXtWi2edSR6tUL5hpTJGrhJa23Z+8
      7/pHl64+3NxdXf+uElFy8fFjcnu3+HT116s6la2Qr9pHFWSZKFtlqRKMCj4VhlUp/vHqqtV26q32Uv9B
      g/JW5wKdqFQU79N1U+mPS8sunan/5a16rWZ72CuefLUSSiw6I/Xu//i3/8hUjikF+Dr/K/3PV6v/Df6U
      XF1fL+76B4IM80GVFP/jP14l+v+s/m1UXd0km0TlXvgdxj/2f/jPUfC/LYYULZUySEbO5YdlkqVtOhdy
      fN4m5GXeUgj6eZtQiJICUI8b+ru/b+9VCB1+/hRNsq8ykYhSZ8tsNhIljC4f778sk7XKN2Wb7IUqBWfT
      faVDZeBAjhTNg35/Os5SOlRd5Carw2ajMiaHDehth4ez5Jwfsr4aoDOxKI8d0r7ao8eERDgctir3t/le
      VIeWyDWUHnWnqoBCMMG22GOzAgH5+pg4C8eYLlV1kZanxfFLkuww1FFUIxw1+i7u7pLfF/fJl6sPc/mG
      xOfcLS6WN9dUVK+yaUWVZol+WLdiVBOVwnS1I/nmVrXG1A86ZChVnqsbibeLr0kjBr/lYrm8mv/9kBYg
      r/Iqiu7obQfduBRcvCeG2BGvDwJGD/3Hy6vbPxZ3SSbkuslrSkaB1SBdl1qp6rAkZZ4x8KYc5a90a5PH
      1lKUu85r1WqPePMRgHpk+VbINsJjBKAeuoCXu/S7GB5mOrkY1I/9LYFv+P6UlOleMMGDOkhnv3UvRtn7
      9Cl5GHrgPAODgLvkZazLSEBdIqIgGP51s4mIgEEdoFdtta6KJMLhREBd4kI/FPK5TFJVGzHIgxKjropq
      /X0opXh0kwC6yFaVGmmTcZOOpXccbr7eJmmWJXpsSo+rqPAjNi0nMIDfphECeFKSHTEQ4KnSx2t6+FlK
      mPoiH4JwEMc8YxnkGcLjBgsUKneLj4vr+6uLL13kkKi2FOVqY/FEGu3BCVMupXhUre5MPMVZnTCon34i
      E4XYdoPkPDOLEXSStWqCnNfFQZ462xGeAG2+e98lfSn3nhZ0f3r7+rcIOy1H+arbq0JANCo37/QQPs/G
      oUy76QFuPZ+iqtha/1PIWGefGH6LU7JL1o3oBsDTIuYdIF74Daq1rFWHU9aVHv6PsLZAYc+6yR90KH0X
      zzGOBibsJ/NtqYNEJw09qqIq9n2dFLlsY+xx6vTb5OU2SYttpXrKu303iydjXwVAht+jbQ6q85aW613V
      UOvGMCrkG1kHyRl1kH5mr19on7brnWoqygO7pARZQWcjVXJahFMw1Pugc9+G6dWLHfb9X7pt+rovTbt0
      TqL7cpB/Fsc/m8HnFa2+HOQPNY7RClW5kGEEchDHfpj/8oJlcxTDbPHUNmlclHgM2En2n8kxGKQ+d70T
      qk/IrV8gAODRj6ypb9s21aEmO9hygF+ItDFCT5IdXADm4cYT08nDYH56ZpVnoZUYtar7dQcs8CD22f0E
      cN+qUTV7XeiVD0QLiIE6gc0JybSEYah3W0gdf2UpyANVGMT32qiuxu6YdckfZqsBOrXbPGh8UjdwYTTm
      qVRXjzmQezqWMkTlZWZXjzjUaZPuWexOiVH7EpdRYjtykN9nBNnqlUB0vKFG6F2RLlnoXopwj1U1va8C
      EhCX0/K6pK6KfP3MMnIhsJf6U3ooVLs2lfJRlU8rjpcHmemVHKRoyH2PSRrszunm2FKUyxtcA/SYQ2Sr
      AITAXnm5qZJ1WhSrdP2d42MBYA9VKBTVNsrFQcA+eqqsKym4mdUC4B7dhBBrygeDIF4q6uK9XAjixWgZ
      HnUwsTzsVctn/V3w0q8hh/nMVqchhbk/DrlepLk7tFn1yApymwC7dCtM0h11Zs9Tw/Shlabyi+pOsePW
      p8BuxJVngBThFlKVYkMq0EUAK7J9Cuymske+eY4qpRxE0CcTdbuLMOn0QQdutBtyn9+tERueKKp1ysqD
      IMT3KoXqQbX7OrlbkgdaTC1EfqQDH31OI/bVg+AOpNhqn65/SNJ1twuDiDakQW6yraosAt7pww6NKMW2
      anNGRw7BIH59MbU5FAXLZ5Rj/FWyy+mVmanFyJXqs695kTxow2R+NJuACY/YiAY4iGPX3+miS+Y/eWY2
      IuDTPbhie/TyAF/3BSL4vTzAHwqZCIsTAXFhZ4pAjtDbmwSP2ksRrmpVrohTUbYU4cr4FCnnpEgZlyLl
      VIqUcSlSTqVIGZ0i5YwUObQqeennKIbY7ethI01SVxWjmrH1iANrXFIGxiX7344DUZKHPskR/rHtyx7n
      gymg2xk7jM4CYaR+OzQPnFLnJA1yWcMSrh5xEOsdq4NkiRF2N0uW5BkPflKH6BHoMJcf5oYecWCNw49K
      hKo3OAupFxCZazp4AYSwEGeZb9Niy3MatGEyPzJMAOIRN4MGIBCflyjnzmaWc0laFNVjcii/l9WjXo5Q
      D2N5nEjCYZh3pNscvhSFbvJz2gIuAXbp13Sw8IM0wOXG/2S8d79HDkhhHMSxmyhIy4yzZsMDIB79wgtm
      KWDKEX7UbJ2cMVtnPBOTsCwC4hI7JyjnzQl2jx2aRr+QbvlyP8lGYD4qye+H9MhzMQCwR/T8ppw3vylf
      dH5TEuc3zeeH7F2n7U7G+JocxLGSXYmuyttuWoAXti4E9hJpUzx3s7DUsyTCFMSNN1csQ3PF+sdNWkih
      Vx41Q/WrmmL6OAa9q1zXXhzDKSb8JttGpFLEhKVNgF2iZpPl9GyyjJ9NlnNmk2XsbLKcnk2WLzGbLOfN
      Jh8fk0LVz5sm3e6pG2cwCOIVO3Mt581cS+bMtURnrrtfZFzyMvXTDknabGNdNAN2KvXcZx+KUW1tiDPl
      KJM0e9DL8KTIom0dGOLNX3Mgp9Yc6Af4e3UgAOLBW9cgQ+saup0MotkfWqEXBolSci18CuIWtwkDpSBu
      8vupVR2RcQEM7jccSRPr52AQv0NTV7wUN0hh7o9Dvo6IHkOO8iPW0sgZa2lk1FoaObGWpv99XTXZeApA
      RI2GoDBfedh3GTrRR+zpGah0L2RME24Cib2HPtQxqUrVkpa79Pztu6TamH1YyXuVKSr2NkM/RX1D9zmC
      5+5SYLdjVWdsLOXVYyAI84xduyVnrt0yn8v1EQhlq4r1GLeREnbTBV+2E9yVYwEU4vsy+08nabh77H7T
      MArxbdpaFzb6NFeemwlAPNomX0cP7fkU2G1YxKePNYmotnwK5sZOncHUaM8zxBToMAl11Y3pvr2hy31u
      xwMEzfWMaS7htLB7m7YHGfu1J8gcL14l4TKCTuN61jg3izPTUb6Inwy6HfQglyp/IqyOCMRHldnZjoXv
      lCFqXDK3EbiPWPPfX2txciNTLlhJg9zooDEZiJOe7GbhtRBm8ictQrMVQyv0BRoGMCnoylqBLidXoDOO
      QTipAJrKw7f9KMBn+sSkrZ6iJxfL67M4iw4x6dMdrx/noxGwz93yIi7ALMAMD3aw+ZQ5btzA8ymwW8TG
      Y0c+yWeHnMuYduqn57lhB5OmXV/CD3fSXb/+0oL2Odnl9BkNEGJ7LS7/SD4v/l7qUy8oeFOHEKkb5i0h
      wtylMskO3bUYOqqqcpNvicuhpliI8z5t5C4t9MBO8zw8LVm+IAlxJW7kMXUIkV59OVKbOxy+nOgbUU7T
      tOO0NMVnAgX7GjPg67Tu7kphWPoU2I2apE0dRqz2yeq5pQ1g+GqY3p+4QD4CFZAH+LyhNQQR8GFPTuGU
      gFstIsJMiyfYZh0go4ws0pRrPxYd59czAk4vMxw5Exl4j74vzvbs5Sifs6oGkAf5rJMYMAbuRKtBbSVO
      3eu7lxrqgkuYgLvETFyFOLjjMMRT5BvRrQekNs2mWCHnveA77UWYTBwLBuQ4PzJygnGiG3KRhZuDwH34
      Rcqohum57KfquG0YUz/tYE2QxtpZMNib2JA1ZDCv22XAK7YGaZAb06JxEKhPTP0hp+oP+UIlo5xdMo4z
      T1yfUO6QEaWfDJZ+Mq70k1Oln1T9mCJLVnrfa7lVmeXAa71CHNixrfg9iqM2TE42VRMR2QAG9qN3Vm2l
      TaUfNQGdMBFxYm3wtNqIk2qDp9RGnFAbPJ1WH5M63FDarbhRGaGl3MgVYvhO+rKnflfRYfUvsW6lTkSq
      E0CbZwmTfFfWObiBM3D1T3q874U+JYByfAv9kL7Oarj7jOTkiifYSVFFGnQEyKUb7ximZ3Tro2jpPj4D
      cmqfa8EOK0M8wWaGlUuwXfo1UbucFDgnkcvSK8iKbmsE89RjBOH46CVx/ZG5JPYoc3gx5zRPnNFMf0vg
      /WLOYJ44f5l3FjJ2DjL7DOTA+ceMA4HAc4DWh7bdNdVhu+v3AgranBYgt/lZNV4MRwGbOoeoGiaMDZyG
      zOb1I9enfRLr9mlcuq57zhSTKRbk3I2Z980k2hIvQI7y9c4s3TogF8cYw3Fa73ifYOgcYuTZ3tPner/Y
      md6E87yjz/KecY63aBrVJ2Be2+mJHfZTXTXd0ixdb+5V2d4QG8QwwXahzhH5c0NbUeoL6PttId0ldBSe
      r3bp7WvzaAFamvfVAN2c3tZNFUl28AiQC/WMHOxs85hzzcNnmne/6mKiW81ZqVZnk9NqZZiAuLDnpmEC
      4GJskzsdYkdPPyAFcGPP+E3N9PHOmcfOmB9nxmL7w2ES6vqQ5kVfsnKP1JmEYd7cWcw5s5fjM8OtY8Od
      Nf0KQKYdiMJ83VWHTE8PA/gdi1PmUA3GAJ26HXnj4UnEM9NQCOgVs/0GQUA+LzLjTJpp3nYHN9FPxjV1
      HjEZlm4RgUeZz1ON+dNN3dSpHkiPOOgD3CIMRjnM7w9ZY/MNOczXcZ62h0YYC4zZbigM8T5eAhwbTSAI
      9hwmcvheFsD3YK4xdaQAt/+y1XPykBYHOtuWo3xGuYHv7WLe34Le3RJ3b8vUnS3G741KTtWeCe/FADvm
      3KcZ98BsjcOa6AvffHWAPl6kx7YYEbiP6nOmZYzLCQB6qII3zxjoTocRqRdX20qfejzDiTFPC8gBvh6S
      OI3Y9VvmpfhB9oExk36PqpIQL2B44iCO3QuJulrvWEajPMSXukmosiff4kgIubRNulG1hHpWtXVbvpfN
      QRz7cOUHnKEPOkQFnY0I+rxA4EEg39Mbf6V6eQDAQw/6kblaBLDoK3LQlZzGD8lfb1//lizvb+4W3b6M
      PHtiWgAk0JW1bjS8XnS4dGsvE3mo9TAoHW2IffaGXAttgPpH/SOXO0FnDTqfeDzgmUo86jAip44clT6V
      fTbdxC1n3c8P5Larkvic05B0UghyWWCJfTb7PLuJm9Gib0WbcSNa9G1oM25C49yCBt+A1t/LcRy3pV9S
      DOl9B8aMM3r3Wbe++zjYyJo4cOUBPrPj6+oRB24BZ4kx9kEPxsQFkcNAnLqTtVTroZTdhFo3qC5ZfiAJ
      cQVGZlieAAdyLDM9S8jrhdpqgM66ztZWAlRjsyiZa2jDZPKGCRDge/BPY5u61bC7JmiVV1Sm1gAk1nlu
      oXsRT79xexAT/a6G3jhroNaZ6ifoXDPegNVNb/GakyEW5DxMjZhnPtEtAQjk1c+NsMbPLDHK1geFMPK+
      rcbonJbpqAxRu7l8PrqTQ3zWKBw6ByN3aSMy7qCtrUbpjNtIfDVE55V+eLkHTWdk+VbQG9k4aZ6r7gCw
      ElCANc+ZlSMQDuDIPcduGz7DzthfmG5FIr/T9mABcoDPXgzmq2H6ocx/0Kd6RiVINc4hOy0TYVhAmCk/
      Tgr2Cb5LxHUqk3f7xtzrG77TN+I+3+BdvsaP9I0Cnhhkc+octGf+yGhdPoKty0d6W+0RaqvFDUmHRqP1
      TtjYlVIYw3caelJU+CCzeXnJPNvEEnpM48oLItRQelTV16fitMThyG68n8TpJSDHmKzKM1oDHCZ4LvoT
      WIMkrtYj65EGIlBLPE7fniWSepHPAhoZ+gDBWlKjLECyXXXL6VBnxBGuUWXTinzVpM0zObGaOoeoL18f
      lzhQ+3mAHOD3K837zQSSjLfUNn2fbvP1afTndAh0S0ovKMT16g+i0gt/+yW/NBNX7dL1VSrqAb1omTrY
      4Yltdin0nY+qF3u3JG3NNHUu8ZEGctrC+moNayiClCp8tU2vVViLY6rciaIgtRV8tUMXgtRc1M97hEZv
      h1jvnLs4iVgY4nqR63OwLld9prW+D7kbQK4r5cba6hXAwH6qDjt7002yHjMmfSP9FMtzfsgz0b8iteXi
      iW12f0WGyq1jtbwp8u2upc7wBUGAZzdiWYgHUZBdRinA7RuuPLChtckNsfhrvBJvLB0EDXSSoTxOjgLk
      Lr9bGG7Eph6zlzQPEOH6SHeZyL+IO0sRhO0zXHAx7lyhOHhil60vHFPORb+9m4a2tS5Z70/Lf4r+WMO8
      yNucNsQEEzCXiNhGIa5XX8414iBp7Xtb6VLb17q1R14PbQkBJnkeFbvnPuKO++D99t2P1KmtkwhgRd0f
      DRMAl0fOGz9Cb3zGiqMzJI6Od9nTcZ0M5ZEXtbhagDw23Hl0Rw84HJdA0YPjpMSoTCTO44WBoQXIu4qB
      1CKARd6tOKoA2rhvi71lMMQBHBnzXaYOJ/IizhQDbP60BiAH+LGl5NmMUpKzl0xie8m6++i7sw66WQjq
      +1pagKw3lPfXYJLBoxTkyu4UdN1FWleZqCvi0hOc4rvR66QEqpE4V68bMoBnXl1PpuL33kfceR+87z7u
      rvupe+6jb5+fcfN8/0h3YA0vu1higM29aX7ilvn4m8nn3ErePdMfBaLbEP3F22QTFwB5bKpGxZAexu7G
      n2W6ZfgAEMCLvnMAPcNTklfDS2A1vP5bVH+sneqJtV1bZVOkWzr5KPSZ7HXsE/er65//lX0/O0seq+Z7
      qhpuJTmMXb3vwF6FPnGjevRt6jNuUo++RX3GDerRt6fPuDmdc2s6fGN6zG3p4ZvSY29Jn74hvXuiPZCh
      7cHnsA9imbgTnHkfOHoXePw94HPuAI+//3vO3d8vcO/3rDu/X+C+71l3fTPv+Ubv+D5d0G1e3kI/YySA
      Qfx40Y3eJX76MWY7BApBvHRvRp+Ds37md4tQEOjJXJs6dUc6/3700N3o/W/jFAenNnH1kMOL3YBOvv38
      5W8+59x6Lul7CyS0t0DyVoFLbBV4/M3hc24N757Rk91jO5u+DAOFQF68/IfnvJc5doly5/gL3Tc++67x
      qHvGJ+4Y728GZ4wOIKMCcXeVz7mn/GVu9557s7dx1bHuL5JX4UN61CFmNbicuxpcRq8GlzNWg0feMj15
      wzTvdmnsZunIW6Unb5Tm3iaN3yTNvEUavUE69vbo6ZujWbdGIzdG826Lxm6KfplboufeEB1zO3T4ZmhJ
      X3kvoZX3rDoarp9r1RY4r4uDfNDzdNtcX6VGwkIAx4NcewE1l/4T42xvU4cTyRcseGKb3VZtd3Urd6Un
      pLcd+DeCh24Dj7wJfPIW8MgbwCdv/466+Xvi1u/4G7/n3PYdf9P3nFu+I274Dt7uHXuz9/St3rF3a0/f
      qx19p/aM+7T12rJ+jfVwlvawQoJoAzJsJ8bYOTha/pjSAkE/7xLkODWW5OVDWtDWTIAAx0MvrSUxtcBi
      PJy/OQ5FkIfwPK1HZiER1jCOykJa2pF8/2XJ+3hPaDPpMIjC+mBPaDP1DeLJ6rDZqETPIANyi6/aRGfs
      EPXFPpsHxWjcEPbFLvs8JhTOw6FwzoRitIhQOA+HQkQYBEOAA4RJEd+OfHl2nifGnYtzmY4M5VHWUwHS
      kZufZ5z3dGQoj/KegHTkqpbF5d3ft/c3yYdvnz4t7rrOfLKuatXQO5Sz955OYKb89H00L+B3wgT8MiHq
      7sXYVidCwEWv2isPRcE2OQJCHoc9H3/YB8h1VbPJShsiH+SOj1biAFvO388GaQNk0sHvsNqiL+/ub9Xz
      N/eLy3udI9V/frr6suCkminUPF9SSgpQZrkR00AIY/vpNcRXt3+cSp99TS1TMATmoy92aQXPoNei5EPN
      xB5qjKn+lPGgWolROYnWV6N0WtK0hBiTmgBtJUalFhKu1OJ2Ry5fX3xdsJMyQgi6MGp9DBHy4dT2GALx
      4dTygBqhEzOSLUSYhMMAXB1OpGZMX4yxSdnS0iFE1W4gXWMIihE2rWVg6XBiXKY0AZgH4YBKT4gwqYWU
      o/SpcRl6Ki9zkzCeehkJF0yz3OSKp1S5yzfk+O5EPosVzU4MX1xeqg5j8nGxvLy7uu2aXpQPRuRBPqEM
      hNUGfbFMLr9eXM7mDc/bhPVqnYhy3TzXLQVkyBzeZnV2/p6FtJQOtW24VEtpUzNBxg0SmyPWK86rGTKH
      x2BBnIodF1UgLmR39U/3A2XnGyD1uYMhh2tIbe6hfGzSmoocVRgtqdMsm79ECxTbbM57wm8Z8Y74Gy6v
      z5KL67+Ti2WyvNfSZP7hYqAYYDOIHufD1f3RY70T8xf9gGKcTaoqAC1O3nZbWFsufJDjfD46RKVUbb40
      wD3sk9Uz4SJcFIB7EJrPgDTIjYlJCcfk11t2ErSkKJf6xoYQZZKTh6l0qTc3XxYX1+T3PMkc3uL629fF
      3cX94iM9SB0tTt4S05gtDXKTvGzf/RJB7wFhj0O0yWHCJWcHUChGqQnPluJcyY9PGYpPGRufcjo+ZXR8
      yhnx2VbJh2uuQSd22J+YGf8TmvN/X1wrvy9X/3fx8f7q6yJJs3+RyIB+woHeJAEJEy7kYgwCTHgQI8GX
      T/CpGRfQTzjUDWGpGk6YcKEWFIB+2oG41HcCA/txWx2+PMjnpSusBWL/zExTaEvk6uItN1RsKcolhoYp
      RJnUULCULvX6fvG7nk3c1zTmqEOIhAlCV4cQ6XFkCBEmtVln6HAiowHgqQP0Qxz+EOLnvODIsdAgp9VR
      hxAlM8YkGmMyKsbkRIzJuBiTUzFGb6ZZSod6/e3LF3pGO6kgGjFJDRqIRE1MR5HDuvnwX4vL+2TdCMJm
      AF8JU8lhZ+hgIjH8TiqYRg3DUebyLu8X42AbsfpwxSE2tSJxxSE2PbZcdYhOjTlbGyKTY9ERh9jUAtYV
      O+xb9ff7iw9fFtwghwATHsSA9+UTfGrwA3rHYbn472+L60tyt8zQucTeqrNVHduMhnXEIfa6EGlJzKMQ
      APaglixomXL8gbDmxtXBRMoRd64OIfJCM8PCkJzl8Jw2TlK8Zn/4SYyyE/Xn9FDog9Pkd6aFxYCdClFu
      5+939pUwlVo44GVC/wN9mMMUBpiJeGJjlTZMTjZ1DFzJYT61dkLrpfGH10zga5SYrJ6T66uPTO6gxumx
      uUPOyh3uU0kq1y/hpjmwo+qQfLv/9J5jMkgRLuE8EVeHE7kZ/ah1yPfvzrjFtS1FucSmhSlEmdQwsJQu
      lTk/cI/OD7AmBZCZAObwPzrm3/2Q5ZsNHadVEI2ecJC5As4EATwrwJoKQMb/mYP+6Eg/a3gfGdM/jcDX
      lcyfWMReinEZEwThWQHn126JZQy+A0AeqmjeilI03UU2mT7HjG7jMxAnZvAflSFqUlalbNMyS5uM72BS
      EDf9eUnLsuilLvfv2wW5H3UUQSx6OXNUQTTqEPxRBLHIJc0ggliS814Sfi99OwULdubQvl1f/bm4W/Jn
      8yDAhAexIvDlE3xqpAF61+H+klX1GzqESG8AWEqESo9FQ4gwqbF2kiE8ciyNOoRIr8otJUKlZltDhxM5
      1a8v9/if3rOzsa3FyeRkYChxKj0xmFKH++fV8ipifNiXB/nEAHHFQTY1WDy1Q8/yLeGYJEPicPq2UyuS
      hzckmKHziG1SrSi3Sjoyh5e3Yp9k5zmJdhQhLMoZFJ4QYxKHtQwdSKRHsKEDiQfOCx7At9MXsXCipNch
      RHL+NoUIMz/PWEilQ4jUnGzoICLvo7EvZn0u8q368BVWPhmEGJOTT3odRGRFBxIXdUpswZ1UEE0fmE2n
      aRVGS9btE4+olRD1UPK+uddBRNpZt67OIe5XwwgCeW7OUmLUko8tAW5ffanw/knL0YbOIarW7D5v8wdB
      LyZsqcs9tImoaGP2gwYgMWr7Uebw2nR7Tt1YM2gAkoosMklpXJLY10V3RiY1EiylQf12/4cS3P+dXF1/
      ukmGDcEkOkqYciGELaKfcqCUyBgA8vi8+PvqIzOURi1O5oTMUYlTWaFxko7cDxfLq8vk8uZadQkurq7v
      aekFVofo80MD0obIhBABxQb78muSCknd7+3IfB5xY7Uj83nMndWIHOYzdlgjcpu/yWuZnL1/l5yrKmVD
      QdtKm7ovMpm+e9sPfXW7kvXTFDpMgF1OnVVi4ED6aYfuIrSmTAsVqG1DaGHMBjLegR2+09QZb/Mi5rjX
      Pm3kTr2VcUccxw3ATPgdVkW+jrY7UWC3Wj0nYr/NgwS9or7LZSBOxn1+m6ZS3RQx/zyDSRDBk504cRri
      3odI9+igZdn6GNiP0v90dThR72joygAu+gSAPWj9R18Zoka9u4MAfN7/GlufeQTYJao+c/TTDvH12RSQ
      8Q7s8GXVZ67uRcxxr5eoz2DMhB+/3AcpsFt8fQZBgl5R3zWjPtNPvUh9hoEInuzESarP9PMvUJ/BGNiP
      VZ8NOpzIrxNcAOzBrM9OyhA16t0n6rPvYn/2+vyXoT6ipylfjzmorm1a8+CdFOOqLi2X20mD3MhKMczC
      nPn1PkSY4zK+Gr04m+bNegN+0pusbY8PRte3AdCkJ7NuwjmYY2S9i2Im/CK/b6r2PT0XX/8GUahvbC0Y
      AAGev757H1Mk23KEzyuQT0qEyiuOT8oQNb4wxlGIb1RR7AJmeEQWxGHcHH92cptTCuvnXqIQRjhTjvwi
      CsYgfvEFMEgJu8V924zSt3vsRQpflIS5vkDRi3BsxyYtM9pRlbYKoyW7x2b+0lFIi5K7+9jSLMv1lakq
      cVJ25M5A2b4q+Z/pCwJoHZFRBdDyMm/JMC0CWK36GLmpmj0ZeFIC1EOdEROcIfN456oFwAnBkw4kMkLx
      KAN5rG8ehT7z7TveVx91IJHz1YMM5HHTj6UNk5NVUa2/yxiDAQH68OLtJPSYb97zUutJBxIZ8XaUgTzW
      V49Cj/n27DzhplhLi5IZIWBKUS4rJGwxyOaGBB4KzBBAv56bdy0tSGaHKRiexS6TaZfrkrPz95LdvwiC
      CJ7k9v40jeTOaUTORLLe46XDIzC+7inrRuxSuSMPgAdBMz3pw9cTqCnfuDb9HN7EG0SH8ozQZU88hEkT
      ri8Ql7PiMGJyYoplOF/dJGldizLrrq4v0/mbxwGpzdV3O63S9Xd9yUxBoVpCh1mItEk2RbqVJOIog3j9
      hfdMqiF22HoqvBRPbf8IiWxLHS41OP1QVH/ptoo1Is3U//lxIGySQgGIx2OTq+J6e0hVO6AVgmXjMAAn
      nQ4JG0hdnU3MKhUDZUlYjm+rbJqoNhSMetzW6zsDSUfsWCKHVRCuvjwJHEZDi0VnjfTwlyQtCipFa2xS
      dw4ZZVDG0Pgkva+OARtkIE9fRKeiYv5JYJDWJ28yKm+TAZSaTKl9CqmzYWh80l5vlWREwFEHE+v521cc
      mc9jR2cgLpm1jyPFuKqEllXJA/danyx3hzarHsnUo84jUj/c+dqdeMoOe1JiHiQ2R0dQSUrLvcKltOQ6
      +qixSToZqiqlVRakEDJ1LrHdkQvwkwhgUbahGBqA1F2ISjoyGpBiXGJ0WEKEmakmT1M9s7CDFiFTM4Ql
      RJj1gcnUQoSpG38sphYizK6lx4J2Sp9a0dtOhszmERO7l851JbDKq6RO84YIOul8IqOpash8Hq1t0SsA
      iuoTkjlKA5BqMqf2KbpMXB02VNQg83myWn8X5EDvVS7tich5cgmH/Uo05PxoyECezlGqDmEgB6VNZXTR
      wN5ZXZEShHrc0evDjUgJoVc4lLYhVytHjUMidslqr0dGLdz9Mp2adPw0040EpLI8o2I6EcDijEdZQpcp
      adm1EziMR95bPSLvJDllt4RLbkkst6VXaktymS2BElvqIpMGUQKXQS9dJVi2SiG+kyjqeZegWoFFJWkB
      cxQBLBV5ya6SLTUVeWKErbsSddW0LPZRjLDZXJhJ7etLcORG8kZuJDZyI8njKxIYX+n+Ru3Tn0QAqyaD
      ap9CHauR4FiNHIZIiO0pQwbzRLXRIw+HpuRgR7VPLwlHMJkan3QaGSGnkFEZoBLHamRwrGb8VdZinacF
      Dz2IMTa5y+ZIfS5nfEmi40unzqFOeBvVOqUcLYQCHI9ddSiyRPXROCHtikE2OcmNMoRHnJQydSCRnhAM
      nUvsY1L9RgOeZA6vpLf6jxqb1AravIV+3iVIRtUwqmzaoVYxQvquXmFTHqhjgg/+eOADJ5Af4FB+ZHQW
      H8HeIjlRAqmxz/zECauTCGJxuhG20qB+ufi8OP9w/vbdbNpJAVGST6T1aI4OJF5Rmh22DOR9o60ac4UG
      8zr58OXq+mN/A1X5IAjtW18Kc0lZy9HBxLx8SIucFASgGqUzgyEPhAJl7NSWWbzL+78Slc0JqEHhUYjR
      cpR4HMLx+qPCo9CCZ1B4FNmmDfVtOo1F+n1xffmhW4VDQI0igEUM61EEsPREYtpsybhBBxBpYX/SACRJ
      SgsnjUX6enN930UM5VhNVwcTidFg6WAiLehMGcrThalsKdeYoADcY1M1yb7KDsVBcl0MBOxDSwymDOUl
      enOCyJjYQW3R05VMcpk8Vg2FaqhsWkaiZJ6a/CKDxObI9fmqpFA6gcVY5SWN0QtshvpLTmJ0AoCR1OpD
      SfFu6QBindJpdeqR1qsV691GnUvMxJqGUgKXsSOszzkKXEYhWB92kvk8TqgfVS5tX+c0kBJYjG7tKgHR
      Pe8TEsIpw6YGIBErp1FkswjLgK7t+5f6f1NLoKPE5tCqbq/GXleHUhfXj8lP0VQ6wCQJ56ktusoxtLKt
      F9iM/IECyB9cNTWcjxKbc6DEtnULg/q3KHdpuRZZss+LQk+Ep12R2eR71T9qn7shFwJ+Ds72/3FIC1Zz
      x1Ha1CdKmKinLTUxF3r5r9sisq/KdlvtRfNMQllKi7pdU5KKetpWH/cM6bgQCaly8LQOuU2azfrN2/N3
      wwNnb9+8I+EhwITH+etf3kd5aMCEx5vXv55HeWjAhMcvr3+LCysNmPB4d/bLL1EeGjDh8f7st7iw0gDP
      4/CO+uJK4VGIpexRYnFU64hWX/QCi0GaeLx25xyvdW9D1WPEPtUoclml2Kb6Wgca7KhyaRWp29MLPEZJ
      fBklcBl19XhOg2iFR6GXkoYKpm1SVVPpGQwe1pC7fGICh3qt6m+6oUSjaIVFKQQtk3TPOwRyr/MosTly
      l28o+aQXAIwzMuTMohwP9SGtC7NlDk9+p7aGTxqbVGXE0YpBAVGSH4d8/v0/rs4j0lpwgwKinHftKTqr
      10FEJjDMYzWBYQDuQSwnPK1H7iY7JPWVBxVGS1aF3lKS8ahHNUqvMi65AlI+uZwZRQjrjAU7w2isfGlp
      EXIEGOHuDwURpxQQhdf58sUem9i4OEo8jvzREDFKAVFaOsZPd/KwomIOK4jCShInnUdkFFd+KVXntNZE
      L7AZtHTppkmVpKhfMkgsDm2ayZ1dKksVPBS9ft4nUHPAKLJZhz21CXOUgBxqAFs6n0g6n8/QWCRaZ8bt
      yfTHJerGX3Io9dEcpPoQUNt07vheYCSPdNP28XmfQFnkO0psjhSHrOoOHqSgRhVG0/9nK3jMXmuRiS/o
      vRnrlQLv0v+Z1j21dDaR2jJq/FZRQ24RNUBrSIr1oRHEAnQUOayWON8zKDwKY/jFlHk82liZBMbKJH2s
      TEJjZbTWjduyIbZqvBYNrTXjtmR0a4QaBoPE4rRV0t3bubj+9nVxd3G/+Egg+mKQfXV9v/h9cccAD0qX
      ymo2WzqLeKANLhzckYUDbSLz4M5kHmhJ4eCmhYe0OAhiPX7SWCTi0JozrnZ6pD9vnVRlA1qIvDmUa30o
      b7LLGWxTDdG/i/U6/U7n9jqcqNfgVM2KCx7kAT5pxB4SB9jyx0EIwiYMRA85SFFsmMnkJA1ySevAYbnB
      //Yp+br4OhykNhtrqXwaaRLX0PikbVM9UklaA5O6RRslh9crfSqlXTNKfI7e7Ns8kANtkNm8vdhT1iWc
      FDZFtg2R0is8SrFOWyJGSwAOYU3LKPE4Jf2zSui7ykKUVE5hnklw+eFDNwhPmZwwNTApWVVVwcF1QoSp
      OnrzW7i+MkTtD6Zv0y0ff0IgPtW61Zvz2oawoQcFYB551q8gaQmnaeAExOXAj4hDKCYOLxAVh6m4IA3t
      WCKfVah+GD3X9CqfJut0LaiwTuSzDmfvqCQlATmJKi62KjTrRv30NH8QKoAAfQrBIBfQt5+T06aSgJzo
      b/cRgM+bczL3zTnIYYShFgEsev4+QPla/ZHxTloEsN6TQe8hSnSkvp8Rp2t5nqzoX97LAF67ecMCDjqQ
      +J5BA0JU91XJJWonslld43Z+q8iQ2BzKERjH5x1CTtzGbYlcllynTaZ633mR0XiG0Gaq/8jnn5Y0KiBK
      kp9ndJJWOTTKmbonAcDo63E9rDj/xGBQbLO7pYEq/SaEBrOrs4mUoYHj8z4hIZdBo8qmET/M+x5i78+Q
      2BzKUNfxeZOwHDoCotEji5lo5sM8KcTN274NnexSSRnJxwmAi25Hq1egtcN9rU3Wp5mmeSmHHQ3PlAIK
      Urv0+pnaPDZVNo1WCi+9UnjZb1Utn4k9U1uHExNRiD3hnFtMDzvoFBjr4jIAJ07IwKFC77M7QoTJ/f7J
      707yfV3k65zepcYZmBOtu+sqQ1RGZxdFID4H/usfQu9/eIEPOEx9Abk4Ool8VpHKltSJsGQQj9b7N1U+
      raqHKxE5mdoST7BZ2dwnTLnwhrumSFOuvMQOMXwn0pjKSQJy+F1QFAH6FIJBLgTAOicHqjOmcvpj9LeH
      x1SGhyhjKicJyGGEoTumsqRuZTIkIEfvRdXLsBi8oxTkMr7VHasZ/kwuZqESNmasBiMALtSxGksG8Mo2
      L1QHrZHkZo8hBbjkMSBbBxLfM2hOTNH6wUuvH7zUG4mOixRPDRuxpXX8MIbn1B375HTkiEYQIuTD+xwf
      EPJQnUY+X4ltNmksYemOJSz7k0j19mwK5SSyWf1S1n4LcpH/VPFL2SSDEyCXQ7tm0o9KhyrE9z6IiU1k
      S2gz5fe8pqD08w6hnb+e4fi8S6DMy48Kg7K4u7/6dHV5cb+4vflydXm1WJJWbGD6sAOhpALVYTphHQYi
      N/hfLy7JB2BZIoBFCmBTBLAoH2toHBLplMVR4VAoJyueBA7jjnI0/qhwKLQzGQ2JyVkml/29RVflpiJG
      mq+1yWmWnXU3fRLqI1tm8za0CFy69zXrP1GuST0+bxBurj8lf158+bYgpUlL5dC6E82EpAW9K0SYRTXc
      zsACn9QOva+EipzQ5rRlBu/uS/Lxanmf3N5cXd8TS2VAi5MJmdZT4lRKIvClJvfv2/ub5MO3T58Wd+qJ
      my/EoADlQT7p1SE1Rk+Lolrz0J0U45JG+T0lRuUHcyiEu3kz1RThkY9qjE5pMbtCjMlODoGU0B3aqBd4
      sUPCJEy6yDZt83UX27p/lm5EpKkPxN6BdiY4pPXIX7/dL/4iL1QAtAiZ1JV2hQhTH3dJOjYfVofotLUS
      sBzhH8q49zf0YQf+N5gAz+P+j7vFxcerj8n60DSUiTBYjvO7S2aGK8O5JiYj7FQe9qLJ1zFGAyLsU1fd
      be0xPgPC80nbaq8KknW1V40gvXdxves2MT6K9Dtp/HgeDvPvGnRsu6Mao6ueu3oZNv4k9/jr1frs/L0e
      TG6ea2qqtsUYW5QR7EHsszcr/fMZl+7IMf77OP7k+0fRUfYuVf9Lzl9TsUedT+xrO92GpF5OhRN8l/qQ
      pA961czP/V4V9VvVnRGNpDrBFNCtFs1GD6EW+XeheqPFg2goRwJNk3zXtjGiTv+TnKchhOezyWuZnL1/
      l5wndUNtPNlin10131Wmb8W61f+9Fsk+zR6Sx7wWVdn9qE8T19vDKEPyDLb/ZvQOB9jT0H9kJnRT6nMP
      P3+K7qB19UyZrgpBbQ0BBM9lu97rBJKSm1qjEGPy6glbjLF5ZaEtxtjElbaAFiN3/clUyOS7eObxTULQ
      Zd0+RTgoNUanzGm4Qp+przV87tvS/TXm3PZsgBR0He4jfwlbFxX07V803tTigI68omkL3RFp/6a75/rQ
      tSfC2SE4AXTpCvHhIOK8KhkuDgF06cKQcicVpEXJes1wRES7CNBHtploGga9F4LMdtfdKaz8CWP9sNzn
      71K9n4A+8jEKPaZel53KPRE4qHxa38Alt4tPOo/YFazyWVLO/wGkPlcqxl8qfdS6hqeSLTHIXiyvryLo
      phzk//nXeQTeUCP0t2fnH/4nysEi4C5/fol1GQmIS5RBiP3h69UZH26qEfp5FD0Yx19v/vyw4OMtOcS/
      vfzy9VtE3Np6yOHu493F9Ue+g62HHJbLxS9JRAzbethhuXgTY2DIIf6fqiTh4001SO8j6b8//neEh8eA
      nNaqa5pnomzztEhWB8oGwQADctJDoIUecKAbnKQQ90l19pd/XPADygF4HkW+atLmmdM+MKUed8+ZEd3D
      c6H9nzmvaCh9qtgTTr+yRB5LN655bX9D6VMP+65JSh1pO+k8YhUzulmFRzerck1Nn1riceqqeD578/ot
      b2zAUeN0RmqytDj5QFuiBKp9eiMSqZqkq+qJ9eqO3OM3GaOt3IsQlj5jt83rQrzXF5qzyDbC9xGcQmZQ
      AbRNf6VVJtaJNu/O0SJttp0C4Z55uea6KKnHHY7W5BecPmCGR94v/o22GjiY40FyPbQSoLb9sTERo0Qg
      A3R6mRE4SRiBky83AicpI3DyhUbg5OwROMkegZOBETj9W57FvL2hBumRI1dyzsiVjBtlklOjTLzBFmyc
      Zfh7N6ckhWBiT3KUn2+S9CHNC0bbGkJ4Pm0hz94ku+/ZRl/zoR9Xzwlq4CMU0I0xq3iUebynqiFsyTQ0
      Bun+Lvl49+F32k2gtgqgkeYTTRHAOt69R+YdhQCTVOOaIoBFWaRoaACSPk2EkJdsmcHbpZd61LWf4Vap
      /2n+TLkvDXLJ/V4cgfqU1e6RyddSlCulFG+Y4E4bJie/PMXAlXySHxn6LmbC7yXMPKePi+Vxent2XJga
      myTWqzfUzrOrw4mEqT1A6nGZL4q+J/818bfMxLleUMZ6VUfrkd9EkN/MJ1ODw5c7/JKeWo8am1Qyv79E
      v73kf3cZ+mbduiQs0zAkIIf4aqMKph3K7mT3+TUnKPbZleow1mmTt+QPH5UG9Q/SfT/D45a+e1MCoHve
      JyT1YUWKTkdnE6t9fVDdWyJvVGE0PRu9I8QpJEbZdZplbHYvttiU9u7wuKU/3S9NC0ZTBvNUKkz3Qq/j
      pGQ6DOB4tK+TLYmpBT6D+s29xOfUVEoNMH6Qv0hJAE6TP3A+7KgDiORMa8p83g8q6YfL0NdX//rb2W+k
      m8gBqcU9Xvo6pjsC2RdbbEJPrX/aVhNvbDMkFqffcsr6PldqcSU9L0koL0l6PpBQPuiGvbqzZ2ikQWSz
      8p+U8lU/bulpW+FOApPRhbpMCKc+mBqDdHW3uLy/uft7ea8FtKoD0OLk+UMcvhKnUjKRLzW5y9svF3/f
      L/66J4aBrYOJlG83VTCN9M2WzOIN26yT64uvC+o3e1qcTHtbVwpymS+LvifvFbG362YgasqiVVBssJcX
      yfKKmDcNjU/SNSiVpDU+aajjqLBB5vMoUTFKfE5XN1FJnchnSUZoSS+0SJX18LxN6Ls9+rSNtD00pK9z
      pDY3q2LQvtqjUw4UMSQe50E0+eaZSOpFDktVqB//IIE6hU2h5kc/L7I6Wo4OIfK6WijBdSF1tk4KgEL+
      cq+NePxrTebUEOUH/bvstubpr9ROlyuEmMRul6MDiD/IrB8ehTqN7shA3mkDCgN60trkiM4cqEboKvYY
      WRqQI/zDqsjXbPxJbdOJ9a5X57K7kYAWJPNC1RODbFaIulqbLBllmwTLNskolSRYKkleTpVYTqVW636d
      TupID8/bBGJX+qSwKfSGBdCqYHTJTdHIWlzyRrJdHU7stoZzsZ3YYjP6J7YKplV72oHzkBYiU3o/tgqj
      JQ2PlzQoUTKJ4BcTe2meEGY+UU7Z8oQQk1ALWSKIReoBOjKIJ1mpRiKppq24afuodKnEfpYlAli0ItGR
      uTz6i0Fvpf/WX05R6q0A3WLpQp/nY9TvnDMreHT/7X4KquNPL6Vxgt0P8+T3T3V3CXyiWlS7KpvPc5Ue
      tcxlW5+f/8IjO2qE/vZdDP2kBuk/o+g/MfrdzbfbhLBByNQAJEIjwtQAJFqlbIgAVt+J78cHqoZMteUY
      v2oId4wBUpjbH969KdItBz2qEfq62qRrZpicxBj70DwInQJ58KM6SKeMViNyhJ+JLScFjlKEy04maCrp
      szXhmkNfCVD1WMTqOSaYPQLiwk8nlhqgdyFGGsAGpABXRuVLOZEv9e/8wspSI/TuLEO95VfVwDKvSt08
      2LOcQJLl+nnx9zDOTuu7OUKESepl2jqPqCI8V0mpPx5WrJv5x7ijAN+DVD8OCo9CrBuPEo/DGcYHpEEu
      J9o9PeCgq+SmIgfnKISZjPE6RI7wyWN2sBqid/mQmpc9LUgW5borriSDfNLCZNrAnq/EqOSBeETu8XOZ
      VHX640DNgiedR1TxeU7YPGyrPNpxyJxVdcMA1IOfXYLzBsMzpGGVowKisFsyoB50IHfNbKHHrNbtOT1U
      BxVI0yHNwGmZx+snEdhB6soRPn1aBpFjfHbqDczPHJ9QvzEy9VEG81R8cHhK5vG4bVhPC5K5NZEM1kQy
      oiaSwZpIsmsiGaiJurY4o5Fy0oFEfqp11DCd20CxxRPsJN3oH1Vcq45WXqakEeV5PO8NaFNulshifV3c
      /3HzsT8KMhdFlrTPNaUABPWWQ7+kLs0o1clJA5C6/cXUXoMrhbikccOTBiIRbiezRAArWxVklNJApAP9
      +9z+Gn3lpyUCWN24Xkz2CWFm+xEHbKZQgG+uBxVaskcvg3gySfU5MvrIpJae2mw5zK/KvlHDgR+1AHl/
      oKdopQFItBY1sF749NeuaahHf8i8kxKgdn8nNpscJUpdr1ZMqlKiVFqTzFECVPkyuVvOzd3y5XK3pOTu
      vqW3rxshpchexBvHIf5txS8OHL3lMHRs8uy8JNyk5wlBpmzVbxmD2Qstpi6O9VmPbT6UPZR05otttm6/
      JnrOlMI8iUDW23cM1tt3EOvNe8Z7KRHEent+RmcpkcXqTqFWCaqPrm42+GmfJXKX6v+U8vFA8JiGhbzV
      Zx4f1/8Z5w3ADO+P52/fnv2mW/B1ms+f7LBlKO84FD9/jzIK8D1Ia0MMjU8irp2wVCbt6vbi7v5v8rYo
      T4gwKW0HR2cQr3+/uia+3yjxOLoQ6heTEMffYDnIv4uh3+Hs7tKtYwkqyq36SRIdIITnQ4m3k8KjHG8y
      6q5Q0jVtIVpqFIIMz0nGxamcilMZE6cSi9O7u+T3xX3y5erDbOIo8Tl3i4vlzTUV1ats2vLiz0WyvL+4
      J+Y6X2pz9UGQommqhjZq5ilD1A0fu7G5/ThG9zOFacggnnxWyXnPxZpqm95/hmwbympAR4cTk5LLTEqb
      2t021f8kKUxT5xAP5Zr9+Z7YZncze9SoOokQVlLoP3GAnTJEJWcsQO7zS/E0PtUdbU618Am2i/ojOwpd
      rU+Wz/tVVdBmnXypw9X16IerG05adrUAWf8Hl2xoAXJ3SQMXbYoBdneIVcWm23KbXwvxnZ4VRxVGI2dG
      RxrkkrMjpAccilS2zMAYpUEuL1gc/bQDL4AgiONV1bpDuU+b7yT6KHN4jV601lmSkrWpw4nJesWFKmmA
      u6nZ3E3tcA+cFHcA01ojUlmV7AIfkIN8ZrHvq136vnoQ3XXwRO6oA4nDMdJcsCl3+f11ygyyIbSZMuWE
      wahyaKdmCLVAsJU+lVoEHDUG6c/b5GJx8TG5vP8rSQnXwXtChDncNMzCDlqETOq9uUKEqZtzhFVBvhTh
      Uk6G9oQBZr/RKcsbsabc3jjFQRwpIyeODiFWteC9tBYGmMk2bXeEfQWIHnGQgrAH0xUGmIlcp23LfG0T
      gHi06Za01RPQImTKfSmeEGDqJSy0U94AKcDVe1ZVddLsOCWdKUbY3BA2tAC538jIDA9TbLM/6O2n99Vn
      wtImS2XTLq9u/1jcdZG66i7sIG2kxACoxzqviRncE+Nsep3lq3E6ZW2PL8W5bVNwuUqKcofjmyntWAyA
      etBWMAJanExsJThSlNst3alrWpMOR6A+1JaDI8W5D4wCBdKjDrwyHASgHvsq48aulqJcYkvHVuLUPONS
      8wyl6os6uEmk06JkGZ/G5Zw0rh+KKQFO+qBDdHq0IUEvfZg3v8A0CKBLVP06Ubdy4wEP/5iSJlzKRMXo
      REwySxa0VOHlfT/f05s9UFun+9unvKT1YwwZyiOcUugrIeoVtQI8qTAa6xUHIcT8Rrr509XZxI9irVLQ
      h1SKd79QiKYOJOpczwBqGcQjpx1DBvGosTyqIBo9RkwdRMy+kMsZS+gxdYuYE4gnHU4kpm9HCnIZ0XOU
      oTzea4L5cPiNFe2j0GHmWyFpH90pIAo9okcZyvvr5hMTqZQolRorlhKikpPOSYXRWK8Ip5vupyVl5aKl
      wmjM+D5JMS4vLI9KjMrINo4WInOpOPFP2rpQR4cTmbFliHE2L8ZGLU7mhq+ptumL68ubjwvWqIkjRbnE
      frWtdKglq11jyCAeOS0YMohHjf9RBdHocW7qICKjXWMJPSarXWPqcCKx3HekIJcRPXC7xviB95pg/TT8
      xop2rF3zx+3nRT8zQJ3utZUYNWcyc4jImZW2hAiTMcLvahGyeKqrpmWBeynCpZbIlhBhfs82LKTSYUSx
      5xHFHiFyZ+xAAOJBrJVMHUKkzmtbQoRJnXW2hCizPdRJemh3SSPWeZ2LsmV6+KBpTynKjDaahVPmuvVL
      HfQeJtYZswx28M1eItjnhXh0YM8I5/+fgpgRutQVCZYQYH7++CnZqYIv2dOLIUOLkHMeFKwzPy++die7
      FIwiyNAiZM6bdjKEZ57KzH1jh4E5jaejsI0sBOjzN7ttYWgxMnHlgCVEmKx2BXCCovnT8bxCFvcoRtjU
      +XBLiDA5rZZBhxD1mlUWUgsRJqeV4p8BZ/7COTkJ0WMO9NOTYDnCZ5XyR6HN/PoxYu2SJwbZXe6WHPCg
      xKm08uZrYH3t8TdiWWPIUB6xZ2wrYWojiOWMJQSZmWpXNBXn4wclSKWWs1+xtcpfT8uNXxPbIrYSpFJL
      16/YKuXhB9YLIu9GLVMNGcgjlqdfkbXMw9/Jq3BMHUhkrYpxtTCZV7qh5RrpwDdb5vHY5W+g7OWEIhx6
      ept7f1IdA2mLPTZxhUiv8CiMkAPDjBGnfnzeflgkshuJpKBGlUP7fLl8f65q8L9JtJPKpS3+Pu9+pNGO
      Kp/WDzpm2Vnf2cvLTUVFAwjEh7ra1xIizIzWijB1CJFa61lChNmf/E1sUvrqEL2RaVKlok6KdCUKvo/N
      wR27B/fbzRmxwsQYE07dK0U6DYwJJ8Y6SIwx5SRlItOiJXbtQ5yA4+mO5JhgNCGIVz9qRFyK6KsROrEF
      ZOpwInGEyJEiXPlCuVLOzpXqyaEQ5pY0FmHSRae5SBuNwH2SbKezEtdjkIf4XV5t0v1WlLRLZiZJc11/
      vKDvjylnse4f1gOmbEsTMsNLv9jpUMRoU4sWcGeMe0P6gIPOkiqXRKcchzPPsT6sxFP9Ep49acI1pp6X
      s+p5+QL1vJxVz8sXqOflrHpeGvXzENqRX2aRCK4vEH0+br5/TCMHx83wfynjacfo1pWcbl2lUhKXfRoy
      lJd8/IOJVMoAdXnBxi4vcG5/qD8X3atx+h3/re/At16lUnCal4MOInIqG6RmoZz+b2hgEueuF1gO8fWI
      eoyBrQccMkEf9TF0OJE8Qu2JQba+qI5B1TKUx33VkxYndxsEBW0xB6QHHIbN2mTyoMOJvOAwxQCbNb6E
      jC2RrpM3RQiLUxcMOpTIKFGPQozJrAMMLUa+477tHfa2Z8wwPUPD9Iwbpmd4mJ5FhOlZMEzPuGF6FgrT
      tpA6n+lF3bQbLIIU2C1p0kfuugOMEXJirT9AEIAPozECtkPodyh6SoDaN/HJyF6G8ngFuaEFyPtctfvK
      bUyjxEcAPpwRT3i0Uw9XxqZlgBFy4qdlHwH4HIeEyPSjMMDkpRlLDdG7Mx27p+jpxRTj7D5muPBejdO7
      6ODCOzHAlsx6UqL1pOTWkxKvJ2VEPSmD9aTk1pMSryfli9STcmY92d2lQ5x/t4QQkzPagYx1dF10Vo4+
      KUHqT8YXe2sXuj+zQg8JOeI9ibYM4D2Qt7EaMpTHiw9Di5MbsdYbaLjwQT7Jj/oCk2E7sfZjIzuxOXuw
      4d3Xx78Sl0QaMp9H3yaI7eBm7otGd0Tz9kJju6DHvxNDzxJCTHoI4rup9fUb/TmDSVrkKamB4mp9ckY+
      nWJUOTR9rnIqZHJ2/j5Zr9b6ZqquliLBMchMryTf16o1k1NP350FnH4HfQvYC3zxgAn5rffJqjiItqpo
      m65xyly35P3L+CXvJxz35DNsEUTIp22S3T49hjrfzOYEHEX6xHZR2gB5u96zyUobJqtuX5l1R8DGeIyU
      CTcZkX0H/YSDyl9n51EeHWGGy5tolzeYy2/n/FjvtQhZl0DRZbgLmekVXYaHgKF3eIGyAOAEHLllwaAN
      kLmpYtCGyZFlgUeZcJMRySBcFhyf4JcFFmGGy5toF6gsWO9S9b/z10ldFc9nb16/Jbt4BMAlU28iMvEm
      rmAAKXPdooqGSSLwFk/xQfs0Gbanth+NfZIhvLZh8doG5gnC/Tu2DOaRiyi0pdL/UG1Y76dkAE9Vjpz4
      6GUIjxEfvQzmceKjl8E8TnzAbYj+B0589DKfN9ToVN4gQ3j0+BhkMI8RH4MM5jHiA6m9+x8Y8THIbN6q
      SL+L8xWxhTSqbBpj0zG421gX7sQUMkh8DjEmBwnAoW23GCQg5w0D9AYmcYLpqEOInAAbdCCR+Yr+G+oj
      SMpDQRp8PGpskp7F70fSVs+ku9IAbYBMWwfgSH1uP07He2NTGyDT39iQ4txq9S8uV0lt7i6VXXG2S5vs
      MW1IIeFqHXL9XXAbNK4WITOqAlcLkKOatTABcOl3E5F7064WINf602LwLgDweDp/+/bstygXH2H77NNG
      /bkYkm6SFtuqydsdKbYxxjynpKwYCx+mabA7c5ELIEf4rKUtvtqhZ6Qj9NXjrv4tTf/W03f9VSKk09gk
      FTdSRKU2mAC5MOPaE4NsVjy7WpvcrM+TX15Tmx6jyqcxUADnFxrDSXvUdOOnmW6kZNMdfjucm7du9LaY
      w2aTP1HRKMjzPD//hQhXCp9CK7ShMnqYj3uhEAihPN8376lhoBQe5S1tbLNXQJSEHpqDyqbpYTc9Btdt
      /9inpEziamHyUD7pxRxNxsFbANij/+34pDzU+tBdwXJDUJhvd5ExY6ckTDBc/rpfXH9cfOwONvu2vPh9
      QdsXAcuDfMJCDkgcZFPW6ILqkf7p6nZJOj7hJAAYCeGAJ0vksw6FIN3c7eoc4o+DaJ6N1py+g/ogSXAY
      4fh0V3Cvq0NJmN/3hA5TiuYhX+sNT1m+TtuqSdKNeipZp/O7/5OgSc+V2OirwF/A1CA5rg+ikYQ7mk3N
      SPp9cb24u/iSXF98XSxJ2dxXYtT5mdvVYURClvaEMJOy29LVIUTC6UeuDiFyoycQO/0GqUpfTn1NKEAC
      iJDPQ1ocIjw6OcLnJTI0jXGTWCCFdcvsWcxOiVDlKfBLbvzZiJAPP/5kIP6W3z7c3y14ydvU4mRGZBrS
      kfvH54+z78jSz9pKfSFDWmYUwCDxOG2TrlsiqNMYpK8Xl7MJ6llbyTl/1tVhxPnlpquDiIRzZy0RwiIs
      EXZ1AJGS5C0RwNJj3/PPt3BkAI+yfN4SASxCBjQ1AIl0Lqqtcmik5eijwqFcUUPpyg8h4tJzU+OQaAvO
      DYnDoeydOQkMxt1yqQ9JSOfn5JPCoYiSSukUDuV4CDxlqNATOkz+YDMid/jcIU5Q7LKr4vmNyqyqP9DS
      uIYQZO4PBQOoVCPtarn8ph5NPl4t75Pbm6vre1I5iciD/Pl5GBQH2YSyD1aP9M9/f1jc0TKWIXE5pKxl
      SECObmDoBmSh/tk2hEo3xHCdONnYV4aokZ8RRLm+EbNhKAD1IBcjmN51YM/yIHKEz3x/vBwcfu9/2TTV
      nro5GwWMHl8/zh64V49aOlrz5CSwGZTGyfF5m3DfqJb6pmr2FMxJZLNojZNRYVLezpe/tXTU8Hzrh+db
      Yni+9cLzLSc838Lh+ZYcnm/98Fzc/3HzkbIdeVR4lENJ53Qag/Tl4/Li3VtWOQ9pfTK/PMQJvgu3zML0
      gINxRVVX9uhrzMg2EATw4peRAYTvQzlSwNT4JNqWeFtl0j4vvp69Pv+F1uJyZBCP1PJyZBCPl18gNUSP
      yTM4A3Li5xuMALrE5Z0gBvSLyT8BiOP167v3jIR6UgE0ejI9qQAaO5G6YoAdmURhBOATlUAhAOQRnTxR
      CuQWmTgRxujUDf9f3lwv7+8uVId2max3Yv4V67A6QKeMFIDiAHt+4w+QBriEEQJIa5DVL59oQXBSuJTu
      RgmxbglTzJ4QZLYNYb2Kq3OJRTX/CoJRAVGSVV7RSVrl0ijReRQYjMX98vLidpEsbz9fXNIi05eiXEJa
      doUok/LhnhKmXiWrd11DirDoBtOHHPoTtPgOvR5z4EbiVSAOr7pcoYpeQjWE6TEHXiK5QtPIFTeJXIVS
      iIwMBzkZDpSeia/EqLReCqQ1yDf3V5cL9SgtrVkqiEZIAYYGIlFi3hSNrJsP/5WsV/KcsNfIkDgc2kSz
      IXE4expj7+pJF46OCpuS0b4kc79C/Uemk2qe6SV7ksJypCh39RyDHtQ2vVsTlKVtSoGeRB4rOZTZ/AEs
      S2SzClFu55/GNCocSklN6L3Cpqg/nK9XKwpmkPicoqRiitKnEHb0GRKfI8lvI523UVhqEA8Sn9M+tVSO
      ktgcSY5xCcS4wlIxg8TnEONqkBic28W1fkifFZcWxbgeWCbrqpyf18IYwE92S+boBoPOJ+r1t9WayutV
      AI22cMqRITxCHWDLYF5Dakn4SoCq4irfkomdCqDVB1UxqLYb47tHqc/lfDX8vXo85ClT9VdL5x2VPlVX
      Onn65pwwNAdIAe6+zffkL+9VGE3l2H/xiFqJUrN8s2FitdTn7lK5e3NORfYqnzYEcXJLBZ6EAFMv9+rS
      LRl6UmJUfSFKxcN2UoAr06I87MnMXgbz6l3K4SkZxGNly0EG8WSdrgWd18kg3hPzBbFSo9glmShES37H
      kxBmVl193Gw52KMWJHOK4UEG8nJVcTYtg9gLQSahS2urYNphr7rOYv4OfEgLkhvRNrl44ITnURrkUmZC
      EDnA70ZXD3nR5uWwV40eMgDDd9qz2nZ7pG3X/520ehqQAlyxz+hNnV7l08qK2Rw7CX1mXcn8KWmrpCWX
      /IbU5zaCFUGDzOdJsdbXOPIbuR4A9eAlLUsMsL+rIlnUpK0NkBYhc2qJkzDATPING6u0IXI9/ww3UAyz
      6bmtV4E0PZjFwGkZzOOk2+9Yav3OrB9PQpgpE0naDA9pQTKj5u1VGI10PBgghbn0JnCvAml1xUmPSoXR
      usRA2HcCq2H6Qe44WCUDeYQ9P7YKo3WXmm4O5ZqHPclh/i7fsN5X62BixcqbWgbySBs5XR1I/CmaigHU
      MoDXNutU1YJ7eoo/KUEqp0zvVCBNDwAwcFoG8op12jJ4WobwGA2EXgbySn6klKFYKXnRUmLxUhKuFXdk
      Pk8PG23J5XivAmh73crtmrtk5CgFuFVRPQpyK2iQ+bwH7hD6Az6GfvpJtRn6nTFs+Ingu/xkNbl/um3t
      +z8Wd+RDF2wVRKM0XEyRwapFCU+GzAajBNylP16UbTHIcX5/5hGbP8h9PvGQFEeG8khNO186cm8XX5OL
      5fVZd6TNXKIlQliU5WyeEGA+qhQiyMBOhdFYr3hS2tS/3r7+Lbm6/nRDDkhbGaJS39dX2/TVcyski2wr
      bar6z27ecZXOX2Xr6hxileyU1fzaxRLZLD0Fpc8gu7y6VaVbFzoUKiC3+dTY9+O8C9WPf9AOQ/WEEHN5
      cdsvjv48f7gUVsP05PbbB8L1p4AU5nKD4qgEqIvLiKAwxSCbGxAnJUC9/Xy5/JVM7FQI7T2L9h6jqcev
      /uwOrqNmKowBOfECFg9VfioIpoG7qLx2N5HX9O/dlgcu/CiG2dxQvgvlY10ZkYlahLCSi29/sXhaiDEv
      777wmEqIMe8W/81jKiHAJNbUcB19/Cu/njHFGDsqD3gE3IWbXm05zo8JokAdpH+PqodcAOoRE0ChOkn/
      zquXTsoA9T2b+j5EjaynEA7myA/4cKjHpZrJNHMXnXfvZuTdqHrMBeAeMbFwN1U+sOq1ozDAZNVvpjjE
      5tRzpjjE5tR3pthmk7v9QI+/77JzqjpbCVK5GQWQI3xG8nW1CJkdIHCt1v/IrdJ8NUxnBwdSk/U/kqsx
      Q4bx3vN471FeTMA6gBkeCWEVfxCCevGrYhQCejETTCC1xEREMA7u4sqTu6nyhFvl+mqEzg7tu2BpRa1m
      RxVGo1awthKlEqtWW4lSiZWqrQxRk+vF//DJWg3RiZ1UZEz99OeIuhvvpxq/x+W5iZ6q9RA7d4T6qtYT
      UQEVqtdjuqswAXeJCqZgPc/qsjrSEPc9n/s+yI0N+Bn1P/AYrw2AgIKesW2BWf1y49GIBDaRumIjajKO
      7uLLq7s55VVcWyHcP7eeiYqNu8lSkdd2gPvo9m+8NgTeS3d+Z7Ul8H668zurTTHRU7d+57UtXILhorL3
      2Xly+2Gh113MJlsqj0Y7AMESeSzKUh1D4nH0LLM+Nysts2QtmvnLUjC959AdA0akdhqP1B8EQrk+zRM6
      zOTr75/OSLBOYVPeqgj//PHTeUK5ZsITBpjJ8o+LMza4U7v0eiXO9VFBelMjaf8OIgf5oozim3Kb/2uy
      OpRZIXS5Q0qwlhBh6lScb/SVVILHNgGIR5M+xvu4ENeLWkT8CpQQv3YZnB7MRxVE0+Uvj3hUYlR+kEIE
      yCXOYYoelywggutCOd1pVLiU9rkWetcK5UAaX4lSuwWOTG6nxchDiSIyHvwkx/kPoqhqPn+QY3wdF1x4
      rw2TL8psEfcJPsd2dLpM5DIK0ocdCKuQEbnLH+o9GnUQuawhSdFYg8hlHU91PSVTzk0FM1Cub3/O6wu4
      BkCG582Xq8u/6YnHloE8QivFFIEsSrKzVC7tv79dfGF+rSVFudSvNoQok/z1ptKlss+8ReRBPjU00JNv
      gZ/JoYKffjv8/vXi9lYr6a9tKDEqJ6xNKcqlh4OhHKl3F9cfk2HHwVyeqXFI6i8ifSaBeonDIYwXHJ93
      CN2SdxKjUzgU4kFZpsYhZblMV6rDsama78mhlOlGqD7IZiMopxtPkxxXsaWFo3reJZQv9NohkOO5yYn3
      U9sqh9Y36css2Yt2V9HCw9ECZPksW7E/XtmkPy9ZH2TbnWxODKFpnOPfHVeiP5tkc1I5tLqav6P9JHAZ
      UhyyipH5TKHDpBxnfxJ4DH4akME0QLvr3JAYnMvZtz6pRy1d93KENqIhMTjm5ALlGAtPaDOPMwlUpKmz
      iP836e8GqTJ9z22SPjydE7iA2qInt8tlcntxd/GV1kICpCh3fhPDE6JMQkvAV9pUvT2y/r6WZ6q0UX99
      onBdrU1e5fNHxY/PO4RCX3JfbpNq/mF+rg4jljxgafO6qyZUyVqTvnRUQTRK3jZFNovY2zYkLmeTHoqW
      Wop6SptK7L8bEpuzKdItKeg7gcMgZnw/tztXOlJgjjTApSYyT+yy29fJumkT2moUQApwMzIugyj7+owO
      UiKQ9YPD+gGxBBkkAMomXbdVQw/4QQcQ8x/7mozTIoBFLISOGoBUkjklQKF/GPRVtZTc9D5KAe4PMu6H
      R1G5nzQx4MhAnj56StVc1CLJ1trkXCZVnf44kDLBSWSzIm63ReQIn3wZF6y26cRGmNfy0gFMr1VHFUbT
      5y8KHrKT+lxm/DjSIDcp0mYr6O8NIMI++nDKpo2x6QmTLiLSA/oOVjq2lSEqOxI8gu1Sq46Cbj3r/kK/
      GuTmYnGb7LcbUp0cwEz56R5QvN2RMuXWzepFevUM3KmsSsF10FqY3HcmXiCOQNC0Jz/kfIrrxryDHBSD
      bFbuxG977H7VR1mRcFrgMbrXZvQIHSnMZfTlHCnMPV1LSRtaRAm4S1vFebQV6NDHKSfYLSVI5QS6pQSp
      EUEOAVAPVoD7cpsv+T1aGerRSmZvTaK9NcnoYUmwhyV5/QaJ9Rso65yOz/uErrNErTksIcBs0kcyTmlc
      0k9Bo/x0akqV7Fr6sNOosmmHOmkEaWyzV9gU2i2BowKiRDSYQADowUkfjhTkEtPIqBpplDXD9gph/a/k
      U044s3JUOJQrwsrfk8Bh3DdpKTdVsyeBTiqH9q3OCGvwDYnFOT//hYBQT7tqcvieNB6JGMZHicchh8wo
      sllv31Egb9+5anrYHDUeiRo2g8TjcNKgpcOJH4pq/V1yub3ao9Pj8iSyWG/eU9K5etpVk+PypPFIxLg8
      SjwOOWxGkcV6e3ZOgKinXXVCyymDAqKQQ9nSgURiaJsykEcOdVvoMTlfDH8t40vBr+SUEZbOI7LCzAuv
      q9s/LpZ/JIQa66QwKF/+0FvCdUmRnJ2/X1qzcrPBIUjAq26EPkee1KgPQmZ40ZqiE5gZfo9pU+rhn7Iq
      ZZuWWdpkL/O9GJj5Ti8ULjg69F59z7nrlw/DFvwX8VkB56iYmAjtyBD1Qu324vPiPLm8/4u0IMCRgTzC
      RJGt8minjL+XWyLSlHrcuqnWQnesyFhDaVBJS4Ld1cD9v6nHstuqkXZ/9215n9zffF5cJ5dfrhbX990Q
      OKH4xQlBl5XY5qW+v/GQlvPvfZwEETyTSoVGslfRk25f7gUs6oy3aUQm9nVLiMoZqKCv+nuuysoXCHqH
      NMf1RT7XY4WdCeUVIg/yCeUXrA7S9VikbJrIHGlQYLer5fLb4i4m79uEoAs3Rgx5kK8TZIxBpw86MON8
      VAfpOmGLfYRBD5jhEV0G4rSgu06Pe9Gmeog9MsG5qEnfiNzkU2A3pe3/g5vSLQDskYl1lY2zrscg4Lgh
      KMxXPWZMHkqxbubfLTdNgl3FU62e3ouyTR7OOGYWYNpDNd32q1ifDjLH66Gqm028W4eB/bgJEU9/nK46
      pocdmIUsWrrWUsc9N2JHdZDOjkpTPzp8Wy7urm/ury5p12g5MpA3f3zKEoEsQlTZqpH21/nbt2ezT7nq
      n3bVOi3Vad7QKEeVR4sYGcAJhsvb17/9+SZZ/HWvjx/plx7pm6FneyB60EGfRRXjYOlBB8IOVVuF0ZK0
      yFPJY/ZalMwNhckQ6H9N5PcYuJKD/Ow8Z2CVCqRRyhNHBvK281sBtgqjUY5u9JUgNT/nEJUKpHFTEZ6C
      +ujnffdJC5JJS+VcHU5MNjUXqqQed7j5sW8MUkYJML3noDLZGSMZHGUQLzmNNYunVpR6gE3S8RAFdCPd
      POzqcGKyqqqCi+3EATY97Vlaj6zthnhuKfvuEbnH77ISo4A86TziGKmsrOjKPb4u9ej1w6ACabwcaChB
      Kjut2eIAmx64ltYj90uQi1xSsaPQY3YXoLdPROCgAmmcuuiks4nJxZffb+4SwjXVtgqkEXa82yqQRs2a
      hgzk6U1nDJ6Wgby8ZdDyFmQR+la2CqRJ3pdK7Eu74beMR1RCl3l/f3f14dv9QpWkh5IYiLYWJ5POywXF
      E+xk9ZxcX32MshgYM5xuPvxXtJNizHBqn9poJ8VAnchlhKlEqfSywpKi3H4PNGHIFdOHHarVv1R1GuPR
      E8Iuek9QjIfWow459/Vz/K3JpaKpRKmqUDqLidOTPuwQFacGwXG5XNzd6yPZ6UneUmJUYjQaOoxIjURT
      iDHJrWtH6nKvrj8xwvOogmjUcOw1EIkcfoPIZd19oZ+b6isxKvV7Rx1GJH+3IQSYqq/5OmnEQ/VdZGSu
      KYbZZ7r3Rh1z8MQwW//KwWodQKS2+QcNQMpEIfQWRsbrjVKISzrG2ZFBvAP9i/3Whv4rK/Mg+aarU1Vr
      SR+6TWaa4gBbiiZPCza9l2N83kgYpMccilS2tKXMmB5zKNVLxDiMesxBr+FM20PDNDjJYX5yt/jz5vPi
      Iwd+1CJkTrYedDiR023y5WE+tbPky8P8dZO3+ZqXrVxGwIneO/bUATpxHNHVIuRuVVXDAvdShBtXEEyW
      A5HFwGQpMOZi6rwPTEBciOuFIS1AZjTtwFbdPm3XOzKqUwE0TvMQbhkyOhNHFUYjzphZQoDZ9QYjsoCj
      xxwiMoGjxxzGRJwW24rnYjOmnchTaSgE9hoKLtLJzZgeceDmaxnM15SdKZYIYVEnOywhxKwY7WItAli0
      QwYcGcCj7bxxZA5v8df94np5dXO9pBa1lhKjRoxXI4wZTtQmGMJAnag9OkuJUsm9O1uKcrsLnDiNRhgR
      9CEPbPryIJ8xrAkBUA9uFgjlAGpbwVKiVBkfq3JOrMq4WJVTsSpjY1Viscobb8TGGr/c3Hz+dtsNbGU5
      rY9hS2Huum0KDlTrYCLljgJXhxCpYWnoYGK3pZYZnEctTCZf0wCKHXa39mtxfX/3d0S1hkHmeFErNgwy
      x4s6FYtBcC9qNWpLcS45nTpanMyq4gB92IFRHIIE3CVn0/MAlVrR2VKcKwX7daVog9yo2JSTsSmjY1MG
      Y7ObZinb5pmOP0mDXHYB5xImXVhFm0uYdGEVai4BcqFOax1FEOs4O8WLWFMN0unTW4YOJHLKcaQE78OZ
      PvjsiiE2r17AaoR+cQ1xuNlSIlRuxJ+kGLc7TJ6do13CpAsrR7sEzKVlzuZAgCkP9oe06JxO94huwdLB
      WoXRkqrIeESthKiclgLcRmC1DpB2QVWKIi8ZmXkQQkz6QPwoQ3mEy2h8ZYhKHeN3xRCb1c7yW1gqtS8u
      6Zu/TB1O1PsfWlXKSS76BIA9urJZ/4HDP4lRNn0VpKOFydS8Ncoc3u23D/oGaXLcGTqYSNy6Z8hQ3msm
      8DVO7I+f5nJ7dYhOPqA+gIB9clYw50goU9PVKIN5kpcKJJYKZFScSTzO7m5vlgtOIhuFOLNb20SesIMA
      AQ/iRL8tDXDb5iBbNrpTO3S975s3VmspMSoxRxg6jEjNFaYQYHZLMNO2bcjQkzJE5bSSIcCUB7WVDAGm
      PKjddwgAe3CXE/ryST55EQ6MAHz6K1gYV6zgBMBlGGBgpVhDC5HpQxOjDOIRByYGDUA6BT0r8iw1QGcV
      fEiZd2wlcGLf0GJk3npSXw7zzxKxT/OCwx6kMJeXWI/CAJNbuDr6CQdO0eroQw700TZfjvAjSlVbjvD5
      CT2YziNWTIIEzOXQjezTF29BAMSDs3rL0QJkRqMKbE9xmlJwK4o+fHNSYTTq4I0pRJmbmsncQPVS7LpG
      hDHtRF/XiEFgL27OlqGcLWPznJzOczIiz8lgniOvmDyKEBZ5xaQpBJiMVYmjzON1e0P4e9sgAO5B3m3i
      aBEyc4eaL8f45PbtSYcQGS3RUYgwY3ZrIYyQk94ouU716TAfqWvJA5yQY79P7fqwX4mG72dScDd2YoL3
      Rjm/8pqzEGLah96ohRDTPqxFkgHOhCOnMQ0QJlyo+6cAPeKQ814+x96Y3sI76RCiriVfIJP7mIBfdBZ3
      IY7X8up3etl7FAEs8sj1UQSz9hzWHmBRU8OgcUn3N3eL7o6OdSHSklgLemqUTo8RS4pyu/KevPEa0E84
      7NK8jLLQgAmPQ9Pos6HXxOXLOCbsR5/sgQCTHt27EJvHKCXsJtuqETFGHSDsoSoUPfFCPHsCg4S8zrp0
      Kfk+A2DCIy5ln02n7DOdFOM+Q+nDDoztyiAh5NJNFR7oS1AxSNArMlqmY2UsJ6IKTwsT9BNNU0XEUK+f
      dlBdvbrdxfr0lLDbE33FM0iYclGVdr+OL87qhEH98jLnpoS8zPHYJ7dUTCVKHe45Z5csJ33YIaaWlNO1
      ZPfIUBnoQ4XX32O8LFDIM6p8kZPlS7ecX2zSQ9FGeAyECRd+bj/pgw4x5ZacLLdkdEkiZ5Qk+hnSPe+Y
      PuhQH5q6kiLCYyAEXdp8H2Oh5ZP8RL1F/hTp0kPCXuQVQIA+6DBcC79eRbicGKjTSxRg02WXHiFmtlaO
      UpzL6nQNSpRaVNV3Vpd6FINsZm8a7UkbJ49yighTjvO5NelEX3M7nrDJfPez4Lt3O1iLYWyL42ADQA9e
      CwlrHXVTg9zQHsUY+1gvq6faneRZ2IyAE692D9fsMbVhuCaMqwWnasCYGiNcW8TWFNO1BOPcFlPoMP+8
      YJzgeBQBLGK/p5cAHGo+HjQuaXF39env5Pbi7uJrf2JpXRX5mjYfjEEmvM6SXUVMYDAi5KMHixtGFsQg
      IS96MnHVIfqWVUjBiCmfyPDaIiWX9VBe7lQ2joj/ARDyYDSKAH3IgZwNHXGIretHPlyrp+iMhZsIY9Ip
      Lq+fEJM+eR3pktczPJJUrqN9NGTSqytKcyEj3Y6YCb/YEkbOKWFkfAkj55Qw+iGdZl7A64SZ8uM0yTDI
      lBd5eAIkzHFhDFIEOJOO5IYnjHB82KvSAqvRup8a0S0tZBwZ4sshfvcxbLyp9unklUnw2rnuVk36+oVR
      BvLIFeAoc3jdGDKnZ2AKPabedZN+Jy41H2Ugb50yaOsUZNFrd0MHEsm1+CgDecTa+ihCWORa2RTCTD1V
      y4nfXggyuTu9pnZ5Db8zKiBLCVLpRbKhc4nEQ3f883bUX06TweRK0BUDbBYzwGJUn7bU4TJXKKMrkxk7
      +MDde9SVzf6K5q7koXekR5nDU/+V6XUQw3nJqfoX43oLlIK4cZZuOFqXTA0RICy6we300O4q1Wt+5qxj
      AQlhF1VMUTe1g4SwCyNOQQLkwlwDH1773t8DUrUXm5YTB0clQv0gNtTVabYU4jK29uA7U41fklXeyrbh
      ggc5xGcv/51a2R+xpza4n7b/cdipxM05th5yaFdSv0JabOn0UQuRD3nGyCVa5dM4g1PojuJ+6m0tazpO
      q3xaYhxJQmWaWoB8nK/Sk8hJ2oiUzPcIUy7Uw3whwAyPRJQP0T4aMuVFPkIYJMxxif+kIyXgdmzzx0ST
      wQCcOOuC8HWFUasJJ9YQcnZTwbuoInZPBXdNReyWCu6Sit0dNb0rir8bKrQLirv7Cd/1dDpkIBNZV88d
      ZLoVHLiDwHy6U0Dow8iAHnDg3gWzDd4Do3/lB00oRLjN1kCrld9oDbVZuxUfhSjJzEEHEVmNYLQNHNVE
      nWihRpyGMXUSRtQpGBMnYHBPv8BPvtCb2tiJdh9ItXt+st3j6XbfDfuk2b9ozJPM4eVSH9iQZ8M8ADEl
      eGqPfip/yON6jjZAJh+564on2OQDeCGA60GrQL11DKq8UMFOnlEZZSCPPKMyyhxet9Swa8Cum4Le4Pbl
      KD+CjXL5rwy/LXUZiL/yo04bKZJNU+2T1WGzIZZUntqldwuy+kF5GtgQukzy2T3QuT2sM3uQ83q4xyzj
      JyyzTv9BTv4ZxqsYg+2W0qEOs8fdEjUS1BQ6zP5mRk6NaSkRKqPGtKUQN+I0pemTlKJPUZpxghJ3dw6+
      JyfmnsnwHZOS2wuQeC9AsnsBMtALYJ5JhZ5HFXWqxMRpElHnXE2cccU93wo/24p8rhVwphXrPCvkLKsx
      d2UHYkPUlqJcen3naF2yEV3kxrMrDrHJzWdPPUUnN6BBgudS11Wj92mdxlCIHp7ecWD1tJB+1vHP1KaM
      oXOJXZeLXrEbOofIWP8ErnxinBkHnhd33MdB3Whn6HDisLtetirrbbl4C2J7PbzhrJ8bVR6Nt6rDEnpM
      xmj5qMJojBFzTxxiE0fNPXGIzRk5hwmoC3n03NWO5PQ8T65uFeBusVzORVoihJVcX7JwSmcQhTw7f79d
      72X+kKh/JN9nD48D0iA3EeU6eTqLwA8ExCUTaxZb6RCiWK86y1VRze9y4wTMRf2+l9vk6ReexUk+xX8f
      x3+P8L9nGxZY6Szi+dt33HToSoNcejpECIgLLR1aOoTITYcIAXPhpENIPsV/H8d/j/Bp6dDSWUR9s3PX
      aSL0OB2ZzVM+OnJVOyzTs/cP+m/pw9M5BY4xZjm9PXsJL0Xx3XSsxH4XxJjlxPgumGK77R6T9WqtH22e
      65biYCt9atu8OT/+2udFScUDCM9HRR7jzQeVRxvKDgbRUPpUHjFM6+a82+r4KdQcHAR5nv0+Oa6Rowbp
      xssw6IZ6ip6kRRvnoAlzXJJadUVVh2z+how5rEnnVTp/O0UAYfuUFb+kcLUQObK0QCGAF6PEMHUAkRsm
      eHhE5DdIjzgw8xyktxyGxsauTVeFeEc6PA9W4/Qo+BS7rornh/l9b0wPOQw/JbuqKecPy2N6y6HMj60a
      YqK0hRCTntBtocGU5ZleCj8MVSWFKLfzN3LDaoeeVUmarUjIXuJwdDOKsp/FEgEsUoo1RQCrEaSDfV0d
      QJTpAx2nRT6rynTckAaEAanD3QqV3tMi/ymybihaNVzmHxyOEzwXfY5jla+FKugKsW6rhujh6QGHTS6K
      LKlbOvukBKhDnuiLoE3VJK2KbMKY8iTI8cxlP12kHyN5mEKHuU8buVPF25jHSWRfjvCVu9iKhgUftA5Z
      NdO6QdGuV6l33ulAS36KpiKZ4BjMT1fIVSl4LoPYYcvIXCAnc4G+bJp6vL4nhJiyP7OcnHJcMcTulkMk
      qUq9lU4DDd3AJTguh3bNLNss5UhdqSpYuaV13VQP/UGS7WF2AxtWe/SyaiMdAILtIg/rtZAk7CAxOEIc
      kn2VqfyrVwroyGgoG6gxveGQV8MRXFJ1Qajn5MJqm67+VFaJ3FUHVQs0om2eKXRfbdP1+QKqxNGT0Toh
      Da+h/5RmGek7wiTbVf9ID6lR5dP0Ohv131TcIAN53CAH5Aa/TFK9TfGwStZVKVtSagS0NjnLkseqmb/P
      0dTYJCn7NaqtVGk/WT23ggQF5BZ/lW9V0y/L01KnFeo7A2qLvq7qZzJ0FFksVScX6nMIawoskc1SnTlO
      rFs6iyieapXDCKheYDGOsUQNMEtnE/Va331VtttqL5rnRO7ToqCQIb3lsE3bnWjeEpiDwqKol2/ScivI
      n24LbabsW6KqBCBTHanLbUSRtvmDKJ51W5qUggC1Rf9Xuq5WOQHYCyxGoWpgTuq2dDZRVeZJu1PZ3EgM
      dxQ0CEA8qNHlKC3qPi8K0ahEsspL0iAApA2QVXuyO0+ZjT8CHI8yV1kuecyz+eM0rs4mVll/SjgjfXha
      kEyNPUvnEVUx2SUZctHliz320JZ83WdDvg3KwRzZoe/pUQdqueRpUbL8/1o7v+Y2lWyLv99vct9i5fgk
      8+g4yhnd+NgeJKcm94XCErYoS0IBZCvz6YcGCfrP3g1rt6umpnKM1m9Bs3fTNLA7XRZpFWSgIxyfTbnO
      ntQiS8I2cvSMQ6CBh789bEIuuhzC8ZGOXR0tSZbkcadziIeLP8X7amgtcrsMGzqbQUhpLnrF0HU0UQ0q
      okjYFgzDddp9QLm7DyblsPnj2GxBQJ2IYcXLZI/MWJJijo0PRV3xADts/y2I7SW7euo6h7jMt4/JHyCu
      FdGszxLWZ4IliH5d5xDxSCXj1DxR6pmrAGroaQcpmSTCF5izxiFJoo+MvKOo8zgyvccxqPs4DvQfx6AO
      5DjQgxzfpQs5juxDjnVncBRa6FKDm9f9y675GE3dvuaPr1l+KOu71zq5VeHJCjEaZJnOu2ZWvR8JIU62
      1iDv8zfZyTCFFBPMbU3l0o6XKOl4SVGkB3qkj7RQs8myGRtb6nJPdwfNb1CwrjXJ6eqwTOugWELMXsXR
      1BTUfpNIsZ3c4pfZfwRtq8lM3umeCAbqOoJ4bu/mP2Cuoabost0l9rZcJlWFde1niclpdhihNAKLoR7K
      wsemyyxeJZ4ncrQOWT0QzJaCvTWlDlcCJEi/is/H5p2AYpcgQyBTSDDBwUsvYliCDtgV22x8fN+LaNZn
      CeszwcLH94bOIaJj3E7jkODIO2ts0lEcekc29gTzLvScizHygVuPUBv0g3QK+cDPHx+k01kHfi7rDX4s
      90Y8l2taV7VJ/7gTIbpqjZ6r913KclNvKVUNgHS5VF39U/Pq0WgXL8Xj9vS0Wr+DnYnx+BVl8g52BoVy
      e8r2ZfxYpMmL0MgCsB7Zbtl+jTv+fSSeQLl0hxmvt0l9TtfJ5PJP3IrE+P22m1WZhDg1gCGPl3Qb5lED
      /B7hTTamterfAN+K8YQBl8068KS0BM2lbD4sBMcmushh4Z+rOMKeuZxk8dX89iL+MlvE84VSjqUSUoI7
      u11M/5pGMPSkI4h3X/5ver2Aga1M462T+n+TZpnM3xcfP1zGSblFz5QX4vPK9+MrotLqITr0IacHMehT
      lukfF4FGDcPnVKbjx4C0eoge2F4dYtAnsL16huakkjpvvklZbtSccbpTATh6XMbpe4eVvJ9Y+fqJfuPf
      91LsWUlR7+5uple3OLPVEcTp7cPf0+hqMf0KQ3spwf1reltvu5n9//TrYvb3FIZbet5B2MqGmqDPri6F
      5E5JUbGrx4q9enRbbh9ubmCcEhEs7Eq04q5E/YbrxVScXbqYYN/Xf19cfbmZCvGWnnCYT//1ML29nsZX
      tz9hvC4m2QshdsEQF39eCFuiU1JUSeoy+br4eS9g1SKC9XA7+zGN5uLst/SUw+JadPAnHUn89lm6u52U
      4P6YzWfyPDDUFv1h8c9auPhZdz/f7uKr62ugXhQL4Dy+T3/OvsrojdTiHqr8vl2c5Pv476ZdpUn9cjWf
      XcfXd7d1c13V/QfUGo7YZF9Po8Xs2+y6vp7e393MrmdTiE7ILX50E3+dzRfx/R2655bU5H795z4pkm2J
      AM8amhQDn4PYOos4i+or0130E08OS2pz5/c3Vz8X038vMGYns3jzK1mwGkIPE25SW+xjjy9kTWld8uFx
      ky0FDXHWOURwRS1TxdEETaopWSrcmL3QZc5nf6G0WuJwBAl+Fpms6bVgrzqRzbr/fq880iotSgyoKx2q
      lMkTRamt63giGoW21kPGItGS2lxBCnYihoUfOpt//Sb0oLnsq7v46e3X6Vc1Nokf5ld/QSNJV23STzev
      8e0VNkLVdTxxLkVaI4PZfP5QK7ShAwJ21Sb9drqYX1/dT+P5/fera4RsKnnqTAqdmcz779fz8XP9vYKi
      oEHfq0gaFu6dyGV9QjmfCIbk4D7Rx/ZZ3kUScj8fb8TPnr6y2a6mJ3402a/unGC8KR/ki1rIRQz7CFrK
      IVAuov1n9liyj85ewRc76konu8xx1zjRBY65uslGNNx4JiBVfVkqTlBPbkpuTZj7kkh6zxfx93xRyD1f
      5L/niwLu+SLvPV8kvOeL2Hs+fYukGXSth4w3giZ1uPH9fB7fX0VXf89BrKYkqHBfFDH3vpH43jfy3PtG
      0nvfiL/3VfXvEZT6vUuIr27+uotQTquiaItFNPvysJjixLOSoj78G+c9/JsgqRlEEe4spJj1RRvn1SKK
      Fd3gqOiGJsHjKkPIMMGs0HUMEcsITUbwmpvK+ezuFkZ2Sh91LsfOCS56a9uJCBbeBWoyghdN/wXDag1N
      kkXiWcgwJZF40jFEQSS2MpL34+479sKBriOI4JTiWUOQflzhvUytIUiSc0C3v6DtjXZfx21JxHT8tx66
      xiA1a3bGpwcuT8n4l2UprUnOt/tDlTalrffJSi2hrgqgoW//DpMM1736EdgynUYjlYmgkXWRyWqbCij5
      bIh6VrqM//p2KmpSt8RYmiWjeavHjYRXy2jeU7pJt6oGi4TaiX3sdilbpCSaj+Fz2h42cota7GO3XwPK
      8a3e51D+KuT4Wuxjqw8bws7AmUC7qEoaqg6/6gQkHrqedhCeW/asqhcUkeUAKK2PXC3XcnQt5tkBzazJ
      PfzmfjnsEHSG47TLykqtRbjMV6n6gnOTFKqSGxqcHMbxK7PtftMsrRkf68tUXqyyXVKhZ56hcG6BfR9D
      8bsJs5xkcE7PRX7Yt0W8D8WrsBEtiN+rfA+vcsirqXpVySxaLUsu40T1cE+qk/stdDAYHqd8F9JWGoDz
      aMoyN9U/ZRa93u+A1HTi9H4HFRJ1tIedGBLl9S3j9Nch2QTYnQiGS/Kk/nWqM5nsYA9STzm0X7jj5FZH
      EeuGO9viWE1sstHbAl1jkB6z592h6RebDhLgWUqG2l65RNhWanADLnLeK9v57u7t9uobwtRkBq+92GA3
      R52GIKHxrqkImuiy7b1Wtxt36TMMrDUUqe6n1ZIH8TYpX3CmribowGIJuoYgwd2FLqN4h0ccdngkSO23
      3nUmwbxOyVBFcUOOu9QISU9JtRIAimcZg05wz8RDDK9mkfj6eJtxRryfXP4ZH7er0xe8cVm+HQDPYZjP
      ++PnP84/V/8M8yZgI70vLybNz+NVkTxVHz69yz7YUHJfTvdN1r4L/GnQWE+1r/Jj9wONfRBOVLDzE92A
      qd6NdkgCUF3xABu+KecQhg88G6trTFIzGla9i1p3DMEZQoLZXFYPO9X+RVqW6QqGOwTCRU1dSKa/WQDj
      AfesttTLRee1SP2QAxaHNMDvgWcphxjwaeaqgmwawhiX8IZjZ9bOd6LgeEuXkbzq3HH01/VSwKcwhJ9g
      /GQKTWZ7/gWtYggNpqpgmDdD6GYEDacyqaccpGSCeIod7HarF1Gs5tYJXSCKkVN80S2Yo2XJeLFSFkB5
      ZLvXD0EeFoD0KKG18xwhxTQr2uNoU085YLfAvYhiwc/kDB1FhDsKQ0cSoRvWXkSxBJ2jpWSoIaecqd7L
      /EAFtrzXYFGmbzsbWyZPpwlTxMjWmuR2FjY8yX0cj+O7NOU4or4X6jWHMntWa1gdkZG3qeOJ8VtWrdUV
      cdkupvqyy992cbIr39ICHIeDYHufXtMie/otOU5d6aMK7y+8GN2vfS76HzX50dWjT1bH8U4MYMgDqafN
      ExgX6KJh6hhiPQYNbx8bMsZL3E4OxeOmqqEGH5kOGeMVdGQGhXFrB/qqNqn0sAzCsEt7w/IOZh1orKe4
      LUnSoOs72A36rPKDWiw7rDV7yBivwMPSKIzbudz3JVT3zYMY9BEfkokY8PkUfjyfxhzPp/Dj+eQ9ntB+
      cEQfGN7/cX3fanJ5efEPwaNsW+gy8SlfW6gxX/ftn5s69fWmfPzQyZX23KcsOb2zczqc1RF5C5CR+/nl
      r0NSpCEWLcFyaR5LSfZfF3JM4L1RR9gzVdnG5+aBSp23Y3mGiGI1hSBxWiOjeEiOmSqKVpZl+hHHNTKK
      97pvdvzX6hfUf7KAYQ+gnLEHYfnU2yo4Cs4iioVHQS+jeHAUdCqKhkdBL6N4IVFAAYY90CigEaZP88QT
      DIKzhiDBIdCrCBoaAJ2IYMGnv1cRtICTT+kHHcBTTxN6l3XymjYfCcXFqgDWZLB1FlEAczgvqyd82GKq
      NNqboKK6IepZ2SSRVvUlpAQXrF9r6wgiVnPWkhE8rNKfJdN5S2l9aEJKcOGWXLItuZLv6cq3pythJWtX
      SVGxSta2jiBKYn7li/lVUCVrTs87CFuZqWTdbYcrWbtKiorG72oofpFK1oaIYKG9yorrVVbyStakmGAL
      K1lzesJBUsmaFJPshRC7YIhwJWtXSVElqcvkK1LJ2hARLGEla05POWCVrG0dSUQrWRNSgiuqZE2rLXpI
      JWsWwHlAlawJqckV15wmxSY7oOY0I7f4f8+RMgiahOTEZVWkyVaAOyktqqwiNiE1uWhFbF1Dk9BGpCti
      N1tkFbEJqc2FK2JbMosnqY7mCD1MuEn56mju5vEFOyitS0aro9k6hwiWxDFVHE3QpGRVMGsb3JhUVbDz
      JqBQjCZxOIIEdytiqz/DFbENkc2SVMR2lQ5VyuSJotSmK2LbW9Ao5CtiO1uxSGQrYrcbBSlIVMQ2/owf
      Opt/korYts4iiiti02qTLqmIbet44lyKtEYG8orYtNqkyypiu0qeOpNCZyYTq4jdKygKGvRURWzt71i4
      ExWxz3/+hHI+EQzJwX2ij02rOT3bPeUSMoEY9sEb1CV4XQKPZPAowo5gcO932Sr0CE6IYZ+wI2kJhIus
      WjkjH+SLWstXrZz7kaC1PNXK+9+I9p/ZY8k+OnsFD0SoUYhsCMKNP0SDD2bkIRttcmPNgI7H1+eIuxtP
      TyO5bWTuGSPp/XjE349HIffjkf9+PAq4H4+89+OR8H48Yu/HpdXKKa2HjDcCWa38tFFQrdxVElS4L4qY
      eYlIPC8ReeYlIum8RMTPSyDVys+/dwlYtXJTRdHQauWukqKOLy+uawgSWq3cEVJMoFq5IaJY0Q2Oim5o
      EjyuYqqVG5vArKCrlRtbsIwgq5UbG6rHUgSsdQQRrn/uKn3UuRw7J7joRAZR/7z7M96pkvXPuw1A/XNd
      Q5Nkse3WPzc2SWLbqX9ubBHEtl3/XNsA1T+3dQQRnEB26593fwXqn+sagiQ5B3T7C9qebHdJf+L0JUUq
      7qAsKc1VUSPknqQ0V8i0eLma1saHv4ZM55Xyd7dK37tbpfAtpZJ9S6kMeROo9L8JVMneWqq4t5ZehfPh
      r+x8+Kt0PvyVmw9/aT5Uuccq6xgijfUlL7Ldc/3Lepg9/1VUi7fRfQ+l9ZNvxteTYuQa/26f7tTmNCnz
      3bxSv/6aVMloA0bPOfxINofxVRsorZ+MtA0t7/mbtXpz5Vs8r6O7HiXFy2SzaUqDPh12o4skeSEDXqtc
      /X9SPAeZdZQBt+aDmOBD6yi8W/BhjTiipyJNpXil5cnZrgSqY9Nqnr5L36ToWspzi7ROzfRV3CZnvetQ
      D74epmG5QSC8PuIAohheJ3FOUAzOKfBwBo9Ekgu9kqPK8kDXcmRBDnRCjimNf1Nt0qOf94u7+MvDt2/T
      SJ4APGXITRScHozHb5Vu0ioV+7RyDx8NUUfsYeOBSsg9fDBcba2PfNjGWZWOf9GLJ3hcJKlBAnqP7eoy
      ftzky5c4Kbfxqh4Pqsom6eiPszl975CrVerxO0FL1vP2L8vyYqLaqkiqLN+VcbJcpvsK+SjOx3Cc1Id4
      z+MHq6bKoe0f0zjdLYvfe2y5CEZu8i/V9knz+F1VV8726/FnktObDp+auinpsW65XbJpqlYl1aGAWp5j
      UE5qY7pqAgu30MQ2e58UZRqv02SFtZCpNKmfm7OzSpuzg0ANocbcPlb5S7pTa5Vd1FmWjf8alZBy3OUm
      S3dVE694+coRKM63br7sNe1/XNaHn1YyY5rFOddpqfI+RRbN4wm8SxWvmwJuqtpZfbMttbIwnF9Wloe0
      eJfzSKI436LOBJmNUnJUlboyqlJy1MMuIItOYpo9kefnJPZy3y0/J0h+Tt4xPydQfk6C83MyIj8n75Of
      k7H5OXm//Jwg+TkR5+fEk58TcX5OPPk5CcnPiSc/92UlvX72Uo77PvnJozjfd8pPD4tzDspPh8C7hOYn
      jeH83ic/eRTnK8rPTslRRfnZKTmqND91scbON7/j6BdSX0yT9BxVc0Wd4ZfaoqmB/Hh4ekrV8436Vknd
      0o3e4WGS5ipZx7mg13EuuiWZTyslAJlFaU1y/c9EFU/at68qxlV9mGV9lFvEgoXQXk054yJ5k1ictSY5
      270mm2wF9juu0qTCxXkMkcUKad+BdnU2i0oyD5NM1+ZMSI0csck+FYaW0gk5ya/jKNTDRhg+/4kvPkz+
      iJ+Tap0WWElVWk3RVQllGfmspKi7+uRPinQlRBtyil9vm6gfCfmGnOKXy6Sq5I1uyEn+r0KKPiktqvqT
      WqWlvqQUwEXJlfbccpKJ3l+xdQRR8v4KKdbY6+SiPRSwipkjdJlSJENsJ7X7CW2krCALGOExCTaZDLmM
      L0PI6YcckFKHPGHIBSqC6EFYPus3USj1MovXeIiQhtKgNtVwRTFvKR1qYNxziGEfKGIYwrALGJksY9gJ
      jU4e4niJItQUOkxplDpag6xWtJTFqaV0qIFxyiGGfcAIYhma08upTFT8dTq/jmb3/Tta6oE59OLAGNaQ
      8y6tx7uHzSbM80wZdBu/mDELGPLY53voxQU/ZdDtUK4DnWrCkMureg0yzKZBmD7akBA9M5aU56LtY2t5
      Mtwmjthlt+9My94a8jEGnPL973CrM8TvJepkWAjrtUrTfbNLQptOzzsc9lL2Yc9Sn4AZT0LKc8EOyZKy
      3KyMy7yoUulOd3rWQXKBIOQ8H+94eiVLlVwECDnPF3RrmpTlqmVGAjseHcH75OPfvSOkLFfUKetal6wq
      pEqi5KxjiJIz2AkZpujoe6VLxd8UdpUcVZrYppql4yesE3LMOitlzFrIMgVh0Cs5qigQNKnBtd9cl1zC
      WQbn1L4bHO+rQubS6zkHMKrZ99/NbYKoJtQsHYpqU8gxsag2hR5mQPuSVz99O5YzlpKjojljS02u+0K9
      KG08GI+fJAhJgM8DC0Vb6yGDAWlr/WQ4LEmAzwMMTkfsYcMh6qoNel+yUR6iLINzEgQnoWbpUFiaQo4p
      CBtCzdKxgLGUHBUNFVtqcPVvpeWR4qHwboJoIfUeByhibCnPFUQNqfc4YJHjaHlymVZScJlWPBeNSldM
      sucPXxbRNChYbITfRxQymtpLF55WXe7lS0+CoTcc7u6nt0rUPoIXT1/6MMN+gklML2fQUdIHejk+R8mE
      Jofw+YATkITaS8f6Q0LtowOfXJLiATbatTAErwvUsbhiH1twPWIIXhes8yLUPjo4bUiofXS0Y6TkBr9Z
      fuZnU3NX2idyCK+PpGdiGZwT2FNYSo4qeaBAyTm+II8JNUuH8tcUckxB3hJqlo7lq6XkqMLHCAyBdcF6
      A0vJUdFewJYS3H89XN2ERZ9D8LoIolAX+9iieDHUPrqs7U05wY//vrq/DxmZ+jCDfvLemOH4HEW9sqH2
      0eW9s4vw+YjzxCF4XQR5oot9bHGv7RC8LpJsNNQ+elAvTlK8bpLe3FD76LKexZQb/EX0MF/Ei7vv01ul
      a/8hzvcRtGF3QdZ4OSMcoQziEMM+gmzyckY4YpnFMoad0MjkIazXu4TlyGgMDMLB2AsMhcEICDjxzvk2
      C8+D5bI4vc9B0PgMwesCZb0r9rHB5ifUPjqaa5Tc5avCyeIUYwi0Cxj4ho4mSiNG1zJkPEpOMpqHPRbW
      ZSxP3J5kL9JvFcTwWUcTRXHbCV1mV+M/MG45js9R1gvaep+D5Kyaah8dKxzI6X0O0px0CV4XPD8NsY8t
      zS2X4HUR5Jmp9tGxZ6Cu2McW5bMld/lhE8AcgvGRnN5OyDClgc/PjGmb8XAn58P6LYIL0lnHE+XtymeM
      ZOLOFDJMURgzM3XNtpu7u+8P94ExTEJYL2nMWXKej8ddr2Sp0kix5DxfEC2alOWKIkbXuuTm3ZDp7SL6
      GRg3LMjrKRvOOACvh+RMW3IvXzakcQBeD2meEQi/D55vptpLl+YdgfD7CPLPknv5gsGNqfbSRVlu612H
      wK8jWcaAk+DFIh7i9xL3XcNfR+o/k7xMROpZB/AxlC1ludgrPpaSp+J9BvMNoL5N2lf4vgE0fiDoI7hv
      APWN4IMYW8pyRb0C+22evvRcWJdAQngvSWjrWp4sebpKA3gPQQJpUg8XTyFNynMlQa5rebLw6SbL8DgJ
      EkrX8mRRShlilx12IRq6AsmGy9wYWfSVoKUkqAf4xt7+pr9d7VHQ+dM9vnTwzI+YBflK5qlgEoSc/5Be
      LPmrpKTnoHsMwWCZHCGLstXNUrCZnNYRBBMZR1gI2dGDBY4dM4JwISPl/Mc4PQpQtcqkYSHnRBsaIG5s
      vObZCm2YTuOSBKFi6AgiFDRnicsBm7rTuCQsu88SlwOfvE6ksTbrlSqxrO4WX9Lf+yQr1OIy469ujN5x
      eMqLMt6/nCqwZ8+ggS2n+UhdcFvHEF+Q1ZFcJU2t6thXy51JuGetQ1a14NXGuB7yA50LJXbY60rSticV
      RWuLD+O8VucQm2HxOsl2kuA1xSS7WZtOiO60JDkg6Ww5yd8kv1MxvReT7CZghOhOy5PXafa8rqTsVs3T
      JVlS+rOk2fx7n0qotczhVe2SiCDupGJoaxFtzdG25bMMWAsZ5r6QHXGt44nS/WylDLd6ETGrF463kfE2
      Lk92gWGvLW95FXL1tuUsH7/WakqaKrhudTqHeNyWIe1gy2m+YI87XU98nWTG1+xjibaOJ86lyDnPBG6L
      CKnG/Rgnah2LbPTUZq8wKZsKIWwqQ/24zHcloG9+bxCW+3yDEJrfm4RioxYJUSuEIJxe5dCAW6pe4VCK
      Zr17ENSKbNYKo5hneJVuqkT9GYB0GoOUHusB2QHAtAKDUd8Wl+u0rMAd0mUGL1vtAUz9a1O9e8oRef1z
      S7/OHrMqTna/od3QZAZPJeihTJ6RSO40BmmXbNNYZVtV1CP/CkkxW2pyyzhLLuNNViL9hqayaEvgPYlO
      YDDyZblXKwDXEYKcA13m8nZ5syoTyjvJDF7dYWXL38Jz4Yop9jbZ77PdswB8VhrUEkyL0smLEr42lc61
      Ka/HpoLFOW0dSQxa9m+IQzqGLfg3CCI9JUv9MXKSH7To3hCHdESW27NkJA8ZiloykgcusecqbSq++KWt
      I4nvEP9j1rzUfvke8T9qtUvtp/L496xzqf3gHeJ/zIqT2i/x+CfWmtQ24PFPrDJpbYjfskpNLOT5k1ql
      a5MUknVAISi5L6JcpNe6fN0naYkug2KIHNbjMk530FrzjtBhVsXHyXlju2xJCcIJgu1yWugeA59EDKuJ
      /CqPH8skLUVgg2C7iNqZaWM1l6l5YkxLTLHPbS9ia+KefZxcXl78A1/s1NY5xOdmfhvEtSKKpXq+puOL
      X5OiyrYpTnYQlM/+Yn+hQmU/wQ16rZf8MYD8kSR/VNuWSX1zIWhwXU3R2/50exg/E0Rp/eT4MSnTEHwD
      GOFRh9cx2EdBBrzKrXova1+ky3y7DzI0SKTr4VFgcHikWFUODVIcocOEl+C1dQ6xXKrFQw9LNFw6HUFs
      BgxNa+PhYak1+uWHf/z4qPqz9q2Dtq+s79OBYY6PYTqdloluxoqrdjikXg18TMbPUgxgLL9V9qwm3JrR
      V7J5zov6t1vIiiTQLqfFerNdVkksNLnF3yZFuU42wiNw1AxdTZmJwEpoMff1ua/iZrFn9TQlKZJtCbEp
      gOXRLDteHZsrTonRTSnBVabqelMdYW4vNbnqycMki7M9MuCwdA6xHSnUduv0CEJ1qcNtLrRq6jvdlRnw
      eISRu/x899TO0W6Tqv4tbGDrHYf6qJrBNHSlcKUOd5PnL2W8yV7SeLUr4aShCf/7P/8FHkTw+g2CBQA=
    EOF

    # PrivacyInfo.xcprivacy is not part of BoringSSL repo, inject it during pod installation
    base64 --decode $opts <<EOF | gunzip > PrivacyInfo.xcprivacy
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
