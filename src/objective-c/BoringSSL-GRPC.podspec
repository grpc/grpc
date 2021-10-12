

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
  version = '0.0.21'
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
    :commit => "340faef0ad19283e985ce7fff0dec73ba4022c8d",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nW73
      fVNsJdG0Y/tIck9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLso/50sbg5W1e7Xd78v7P3v7zbptn2Xbo5//3i8n32
      ++WHdfbbdrt9t8nWv71fpb+8u7hYX27+7d/+67/Orqr9a50/PjVn/3f9H2cX784v/3H2uaoei+xsVq7/
      U35Ffes+q3e5ELmM11RnB5H9Q0bbv/7jbFdt8q38/2m5+a+qPtvkoqnz1aHJzpqnXJyJatu8pHV2tpUf
      puWrcu0P9b4S2dlL3sgfUOv/Xx2as22WnUnkKasz9evrtJQJ8Y+zfV095xuZJM1T2sj/k52lq+o5U6b1
      6drLqsnXmbqKNu6+v97jR/t9ltZneXmWFoUi80wcf93yy/Rscfdp+T+T+fRstji7n9/9ObueXp/9n8lC
      /vv/nE1ur/WXJg/LL3fzs+vZ4upmMvu6OJvc3JxJaj65Xc6mC+X6n9nyy9l8+nkyl8idpKSvd99e3Txc
      z24/a3D29f5mJqP0grO7T8rxdTq/+iL/Mvk4u5ktv+nwn2bL2+li8Z/ScXZ7dzb9c3q7PFt8UR7jyj5O
      z25mk48307NP8l+T229Kt7ifXs0mN/+Q1z2fXi3/IRXH/5Jfurq7XUz/+SB18jtn15Ovk8/qQjR9/Kf+
      YV8my8WdjDuXP2/xcLNUP+PT/O7r2c3dQl352cNiKmNMlhNFyzSUl7z4h+Sm8gLn6ron8n9Xy9ndrfJJ
      QIZezifqOm6nn29mn6e3V1PF3mlgeTeX331YdMw/zibz2UIFvXtYKvpOOXURvru9nervtKmv0kNei76K
      6VwmxNeJFn+yc+M/dfn/eDeXTnn7JJPr6+R+Pv00++tsn4omE2fNS3Umi17Z5Ns8q4UsPLLwV2UmM6FR
      RUwW6p1Qf1CivFF3qypx1fZsl67r6iz7uU9LXQjl//JGnKX142EnfeJslUk404Hk3fuf//bvG3lnlxl4
      Of83/cfZ6j/Aj5KZ/Onz9gtBh/nFs/Ts3//9LFH/Z/VvPTW7S7aJrGXga+j/2P7hHz3wH5ZDZA3V0iG9
      53p5s0jWRS6TKtllsnrYjNX5pGNl6ECPyOrnrOboLNKxqrowWR22W1ncOG6AtyM8nycX/JT1acDO1KI+
      dkr7tGePSYlwOjzKMt3ku0y1bDSvQXrWJ9nCFRlTbMOem5UIyK+PybNwjqm6Ii/zJk+L4y9JNoeu5qUG
      wlV93Ol8nhRVukmUQfVuZFdsbCCI7c1399Nb9YG6BkqV6XK98X76NamzLt5CdhdUmzjSCrGAeZVXUXaH
      tyO81LIV5eo9GHJHXD4o6GOoP17N7mXPJdlkYl3ne0qRhGnQruqH9CDr+TLfMPQmjvpXqrfCcysU9a7z
      vezfR1x5L0BjbPLHTDQRMXoBGoPtDji//0zKdJcxxR0dtLOvuoVR9y79mcgqW/DKu2PAo+RlbJTegEaJ
      yIJg+u/rbUQGdHTAXjXVuiqSiAgnAxql3q5j0ueIo/7ntDhw5ZrFzVHlJlRmcpGksl1jmDsSs66Kav29
      q+94dtMARhGN7BGm9YabqRbvRLj7ep+km02yrnb7OtNTMcTu4IAGiLetswz4piBHxERATFk+3tHTzyJh
      65v8EMSDRMw3rAD5BvFxkwVKleVfqhy8S9ZPqazF11ndkMw+DvrP4/znQ379iZUjafHICAR6kIjtMPVq
      wgpzhGF39rOp07gk8xxwJNH+TE6ADvW966dM1o/7On9Ws+zfs1eq3RMAMdr+qvxtj3V12JMj2DjgL7K0
      NlJPkCO4AiyGm0/MSJ4Gi7erNhkvhCIxa6XHVcxr72DfnZXpqsiSai32qlHcF3KgTw0BOdBIIn8ss64W
      UFMXEtjtBTMkLENjN4VQ+VeWGbm7iUn8WNviIJ6Oty75h9k0YJftO9kpGd+kG3GVcvk2X8tagGp1eSyC
      ul94bkWGrLyb2eWRCPu0TncstyYxa1vjMmpsBwf97Y0gGvV8hq43aMSuq3TBUrco4j021UmRi4altwxw
      FPmn9FDI4WIqxIusM1acQJ5kZKzkILJ6kzbpmwQ92eDo2c+EG6pDUW+ZvcgmfZP9ZMpPPBYhsqUGJXCs
      vNxWyTotilW6/s6JYwngGPJGLarHqCiOAo6jJqH03cu9gSwBHkNPtbCmJDAJEktmXXwsV4LEYvTWjhxs
      ZPbUDBT2/jjk6nHz06HZVC+sJLENcBT9rCN9os4MeTRs73o2sjzLIQg77X0LHI34tBFAEW8hZC0jv7P+
      3t6irMz2LXA0WXzz7WtULeIognE22b55igii+WAEbrYbuO/XTyu7bxTVOmXdg6DEj1VmctTR7PbJfEGe
      nDBZyPxCF774njrbVc8Zd/LBpn27+iBJ12uZ01S1gQa9yWNVbSLkmg9HqLMye6yanDH4QTRIvLaa2h6K
      ghWnxzH/KnnK6Z0lk8XMlRznrnmZ3LFhMz+bTcFAjNiMBjxIRD0Y0dkl8r95wWxFII7+4oodo8UDftVX
      j/C3eMDfVTIRIU4GJAr7pgjcEWpxbsaztijiLQ+7FfFxmY0iXhFfIsWYEiniSqQYKpEirkSKoRIpokuk
      GFEiu14lr/wcYcjdvOsWTyb7qmI0MzaPRGDN5YnAXF772XHyRvDUJxzxH/u+7Lkx2AJGO2en0XkgjeRn
      h/qZU+uc0KCXNW3g8kiEbP3EGiBZMOJmzdH2JGIV+WNaPPIuuGPDZn5ymwIkRtwzDkCBxHmLu+p85F2V
      yGFr9ZIcyu9l9aIeGO+7mR1OJuEyLHZktDF+kRWqg8lpeVwDHKV96s7Sd2jAy83/wXzXn0dOf2AeJKKe
      Nk7LDeepuidAYrSPxpm1gIkj/qjnKWLE8xTjOzEFyzIgUQ51rb6k+j7cMLYCiyOL4a4rI7wohgCOEf0E
      Sox7AiXe9AmUID6BMr/f3XL7tHkSMXFNDxKxErqWlXWgnhjmpa0rgWNlaV286udk3ZoATjMLWJBovKd5
      IvQ0T324TQuRqfUaddckZpuke6FVtyicgENO+Eoe6yyVWERa2gY4StTzPjH8vE/EP+8TY573idjnfWL4
      eZ94i+d9YtzzvuPXRCbbzG2dPqrXTLmxLAkSK/bZohj3bFEwny0K9Nmi/kTEFS+TH46QpPVjbBTlgCOV
      6ulXm4pR/V/IMxRRJOnmWS1eEtkmOqwjg2Pr5XF1JvZVKViFwhIgMXhPnkXoybP6UG1KcGgytbQiKwU3
      hG9BovXLUjkL71ELEk18P/VEI24sQIPH614UjY3naJB43aYVnBgtCnt/HPJ1RPYYOOqPWO0gRqx2EFGr
      HcTAaof280aNBqtS9vTEU3rx4dek2prjHsGLOmTFrqbrR8u+raxHDruMF921wNGOVXG/QpRZz4IiLGbs
      6hIxcnWJ+b1cveRTNrJai4nWW8LR1I2/ecq4a1sCKiQutMaa3fHEbXj0vHxUL4lUtRxJ7PROQoIbGlAh
      cetmrxr3bV5kvGimAInR1Pk6ejrIt8DRumVG6sW9iGrbt2DR2KUzWBrtuemYMSNsQqOqzl7b3qpXvLgd
      Y1A0NmZMdwG3haM3aXMQsb/2JBkTi9dIuI5gpH7FXVw0yzMyoniTeCIY7aAmYWT9ExHqqEDiyDp788TS
      azJkjSvmtgKPk635169Y3FyLlCuWaNAbnTSmA4lUH3jNkAZhJ39SPTSb3vVC36BjAJuCUVlrZMXgGtmD
      GuBvqd6WAmzyHr5vR8F/0B9m2fSQPZksbs/jQmjFYBzVn4qMoxRwnPliEpdglmBEDHay+ZYx0biJ51vg
      aBGvEzr4oJ+dcq5jOFL7SJebdrBpOOpbxMMjqaFfu0Fk85o85fQZd1Bix5pefUn+mH5bqHfZKXqTQ4zU
      12AtEHE+pSLZHPZFl1VVuc0fiUtohlxI5F1ai6e0UBM79Wv3bcGKC5qQqMRXDUwOMdKbLwe1vd1GZYna
      LPf0GLF/bEqJM6CC4xpPaNfpXg0POSF9CxyNWqRNDjNWu2T12tAmMHwatrfvUZM3+QHwgJ83tYYoAnHY
      D2dwSyDaPotIMwUPuM02QEQFskxDUdu56Lh4rSMQ6W2mI0cqA9fRjsXZMVsc9XNWfQB40M96lxtz4JFo
      LahN4tad2ue6pi7Sgw14lNPWcoylAiEPHrGb4inybabXq1G7ZkOuUORdxo+0y8Jm4lwwgOP+yMwJ5onq
      yEVWbo4Cj8OvUnoatueifVTH7cOYPByB2Jk0MNinV4fzqo4ODXpjehWOAo0TU4eLoTpcvFHtJEbXTv3T
      H26cUAkVETWQCNZAIq4GEkM1kJBjiWKTrNTbceVjkamRMSsQ4IEjNhW/V39kw+ZkW9URmQ1o4Hj0AaNN
      2lb6C+nQe+gRezUG92mM2KMxuD+j2igw3bdTDeqhviywDWWn95DDj8TakzGwH6P6SM1Sda+CHFb/ytaN
      UCVI9sJpDzoGVE7cQn1JbVje7W5PiuTCA+6kqCIDaAMURY/Su4cKqokuGnoc3wFFal73GTutDHjAzUwr
      12BHaVfyPOWkxDlBrkuteyr0gnPmDpyIwomjFnK12zeS3D3m+GL2DB3YL5R+lcD1xewHOrAXKG9fTmxP
      TvZ+nIG9OBkbbYD7a6wPTfNUV4fHJ73HbpHRnsQAuO3fyGL7qM55S9Z1pqf+00L1VEg9dVTixKr0wS9y
      2PSd9CNMzjHKbgPjtTgDs33t3O5ppfu6+dkvblZjS0qQIRcUWc8qt50YWg4AOOpX79aoPgG56sccTqT1
      E+8nGJxjjNzTdng/2zfby5awj230HrYj9q/N6lr22JmHwHiw4/65r2q9eEm10Tt5+9fyticFAA12FOpT
      FP/pyenwSrWsSx9EQPH5tGtv3pkvbNPKvE8DdvMBsOoWCXIEzwBF4TXU4d139afqxtYrFCvZJ61zWpsN
      G5Ao7OetsAGIYryadNo6ip7joAWIxn6KNfT0ircjMrYbcv+0J3bcGjZhUblPx8Y8Feu/03VyuhMO2pVl
      zHCgCovrrmZjxvQ0QLzu/aM6+3GQTZZswIh7+KASMFbMyxaIAorzJs8XSc8VH/XWLvSdGk3OMybdQh2i
      8Ij5PubaLgcFvO2LC6tX+iFKAI76GTmIv1PB3A0d3Qk9bhf0oR3Qjc9rOS6qdkx5CwPuboMN+mIQnw7Y
      +yNj2CF6BR6nP/qYGeUkAGM8Z8Ruu8lhRupxRTbpW4/7bjCemwC47/dGhtQIngCIoYYjZK+CABf9SR66
      CsP4IPnrw7vfk8Xybj7VayrzzU9mCMAERmWt+Qiv9ei23N+JRBz2aoBGVxuw796S75YtcJ/If+TiKaO7
      Os43svcTGTg7QH/8TG5XJOJ7ToPQpMjI95gF+272HiQD5w1EnzUw4pyB6DMGRpwvwDlbAD5XgHmmAHqe
      gF6RdBzG0DesBPCAn9lldHkkAve2tmDMfSiK2CRyHEgkvRdCI7tXQk9w6SGzYMUDTUhUNTxJm0Od9YM8
      VkzAA0UsN2rWjtdHtGnAzjpWySYBq/F6A9lrsGEzeYkfKPBj8PfPGDopRG+9vcorqlMxgIm1A0forJHT
      Z0LNKZTrjCU+woCb3iWpoT6JyNbqrul3ldeTV7xOVMgFRW5nj61dCughAQkUq53fYY08LRh1q1dbGfe+
      TWN2ztiqJ0NWPbfOV2sc8rPGyOg8knhKazWLxZvusGnUzthz2achO6/2w+s9oLFLNvljRu8C46ZxUVX3
      nFWAAq5xkVl3BOIBInJ3XnkM77pirIhPH7NEfKetWAZwwM9+OOvTsP1Q5j/ok6Q9CVqNnTNOD4EYISDN
      UDxOCfYNfpSITaMHz8uKOSsrfE5WxBlZwfOxjA/piwQ9GHRz2hx03PzC6F2+gL3LF3pf7QXqq73IKitj
      dyht2rardzdin4NiDjtSXjLfnrVAz2ls+kuUGqRnlWNzqk4hjkckG1lbkDwt4nmUnDXd4LKeue3REZUt
      5LuAZlZt+rIX1EQImOyoqu9w2G+Iczw9ZduKfFWn9Ss5+03OMaoj/frHY9SRDoAD/nbtU7u8TZD1Fm3b
      d+ljvj7Nf5w27mtI5QWVuLHazQPUwpZ2SQstiEu7drU9s/yCWpRDHe57sO3mnseIn8VIfJ/Ne4+tPOzs
      wTipVPi0bd9nGalLo77vGnRhoEk04njqaq3OptITj/tKNLwluwENHE9W0efv9SOpY3Gmv6405PIiP+eb
      rL1Eagvqwba73QxXlvHTr062Rf741FCf2wRFQEw901Vkz1lBjtKjgLft8PDEBmuba2KlUXv1BPMgSPTc
      R+MDzh0F4K5fL4oyclPN9QpaDFDhxhHuQ/V/Ed9GQBR2nG4r3349IyWCB7tutfW/jFy0rwTR1DbrmtU6
      4/zvrN3AJS/yJqdNTcAGLEpEbqMSN1Zbz9UZ9dUNm3StnDMCsfMBI84GDJ4LqD+kPr44QYAr6iS0MWcL
      6u+8cK74Bbric1YenSN5xDmbED2XMOZMwvB5hPpT6L0jcghIAsTqu8G8X+LwQATyemzs5EPuqYf4iYdR
      px0OnHQYecrh4AmH8acbjjnZUPBW6Apsha4+B7A9j1zNi1Kv12IBM+8MxOD5h+pDep2WQDUa5xA69GTD
      qFMAB04AjDj9L3jyX9ypf0Mn/unPuyPQWYXLggE39+y9gXP34s9qG3NOm/5O+2qdqrPbo8jIQVwBFGNb
      1etMT8Lp2TORPjLiABIgFn29K7pjjSCv4RTAGk71t6h+cTPUI45Y0TlwOpz6+F+b7+fnyUtVf0/r6lCS
      08Pl/Qjs9ZgD58FFnwU34hy46DPgRpz/Fn3224hz3zhnvsHnvcWc9RY+5y32jLfh8930N5oDWdocfA/7
      lcaBE9OYp6WhJ6XFn5I25oS0+NPRxpyM9ganoo06Ee0NTkMbdRIa8xQ09AS00/Fl5ta+9HcSAxokHi+7
      0ZPWTh/GLD1GJUgstW+4GkCv1WvTm2xf5SUv1SARGJO5DmzoBDn+6XGhk+Paz/ppYU5r4vJQhLc8l45z
      Jp2gr6MV0DpawVvxKLAVj/Hnuo05001/5ynbGH1S+gNXVALF4pV/vOS/zWvSlBPh3ug0uNEnwUWdAjdw
      Alx7bhtjJI2MoONOkhtzitzbnL029tw14yCqJ/UwmLriFOLRCDErH8XYlY8ieuWjGLHyMfIMsMHzv3hn
      f2HnfkWe+TV43hf3rC/8nC/mGV/o+V6xZ3sNn+vFOtMLOc+Ld5YXdo7X25zhNfb8rpizu8Lndgn6KlMB
      rTJltdFw+0xuWYBWRf2JseebyeFG8iafHmy7m6rRh95w11tBvB2Bf5Za6By1yDPUBs9Pizw7bfDctKgz
      0wbOS4s/K23MOWnxZ6SNOR8t4my04LlosWeiDZ+HFnsq2fCJZNGnkY04iUytVUmesqKouh3bulVRxDCg
      w47EmFcGZ5JfUloiqO87BrWMjqRQgOV4vnh/HMKTp5481jOzlIirm/9jKS22Ny9vFrwf74G2ky6DLKwf
      7IG2U52LlqwO260skAwzgFv+5/PknJ2iPuy7eVLMxk1hH3bdFzGpcBFOhQumFLNFpMJFOBUi0iCYAhwh
      bIr47cgv31zkiXGKxVing6E+ynoaAO29+cWGc50Ohvoo1wmgvVe2+lfzb/fLu+Tjw6dP07keBLeHPG4P
      5XpsjAHNUDy1h/AbxDtpAvE2WbbXF8YOdTIEoqhXIcpDUbCDHAWhGIcdX3/YBcz7g3hiqxUccIvxb5hA
      bMBM2vwSpi37Yr68l9+/W06vluq+kf/5aXYz5eTtkGpcXFJ+ByyjohHLQEhjx1NrL2f3X051xG5PvfMx
      BRZHrTduMl6AlkXNhz1Te9hjTvmnDU+qSMzKKbQ+jdppRdMCMSe1ANokZqVWEi5qefWWkbeTr1N2UUYM
      wSiMthlThOJw2mRMgcThtMUAjdiJN5INYk7C4QgeiDgJL8q6HG6k3uw+jLj31Z6fCkcYc9NueRtEnHqF
      c8yNaQqwGITtxjzQd8bdfkN3Hrdw4OWCVvsfEd/DLVp4qRJP+ZacMxryXdSWo4d61+TqSg7Ckuvp4mo+
      u19SD61H8KB//CYQIBx0E2oumDbs00Vy9XVyNdrXfd82rFfrJCvX9ev4QywdzPFtV+cXlyylRTrWpuZa
      LdK2bjKyrkNsT7ZecS7NwBwfwwV5KnZeVIG8EHqzdf0B5U0lAPW9XUCO10Bt76F8qdM9VdlTmC3Zp5vN
      +KVIIGy7OdcJX2XENeJXuLg9Tya33yj1Y484no+zZbJYqu+3xzySjC6Mu0lNBcDi5kf9WmDDlXc47uer
      Q1ZK8+OjAe9hRzseGhXgMQjTYAAa9MbkpIBz8us9uwhaKOqlXrEBok5y8TBJ13p3dzOd3JKv84Q5vunt
      w9fpfLKcXtOT1GFx8yOxjNko7s3Z2lA6ULPLRnGv4KeCCKVCUyUfb7lmDTvuT8xC9gktZZ+ntzLezex/
      p9fLmRxuppt/kcwAPxCB3vyBhoEo5FsGEgzEIGaCjw/4qcUd4Aci7GvCMiDcMBCFensB/HAE4jLKAQ0c
      j9vC+XjQzytXWGtnf8wsU2irN5t84KaKjaJeYmqYIOqkpoJFutbb5fSzes6029OcPYcYCY+OXA4x0vPI
      ABEntQthcIgx5wlzzEfO7Z5DjIL5mwX6m1XVc5BV6a+/cMUdjvjpXRGLdKy3Dzc39MJ0oiAbMdM7BjJR
      s/sIOa67j/89vVqq/acIi4l9EraS087gYCMx/U4UbKOmYY+5vqvltJ+8IFaRLhxyUytLFw656bnl0iE7
      NedsNmQm56IDh9zUKtCFHfe9/Pty8vFmyk1ySDAQg5jwPj7gpyY/wGMRItInmDLsNAmkBj8dgilAebUT
      QB3vYvrPh+nt1ZQz4euwmJlrBYxL3mUukStsi1ubNulmQ7M6cMi9LrK0JNbTkCAUg9oddWHYTW250Dbr
      +AFhRYvLwUbKVmIuhxh5ObXB8odcZeE1ef9Q4R37h59g1H06YnmXiu/MEJYDjlRk5eP4d2V9ErZSK120
      zek+oE8VmWDAmYw/Jxliw+Zku4+RSxz2C14tI7D6RW0kzBS+Q43J6jW5nV0zvR2N22PvDjHq7nC/laRi
      /RbRlAeOKAe8D8tPl5wgHYp4qR0Wg8ON3Bv9yDrm5a/n3OraRlEvsddigqiTmgYW6VqZz1iW6DMW1oMV
      5GkK8xEK+txEf7DJt1u6TlGQjV5wkOctnIcs8JMV1uMU5BkK88EJ+rSE9YgEeS4S8zAk/AREfyqrt8es
      zGp9+MFG7SNFj+A73Ejf7qfk/vYRglz08nikIBt1fHGEIBe5RHYQ5BKc6xLwdam91Vmyc8f2cDv7czpf
      8J+cQYKBGMQKw8cH/NRMA3g3wvKK1UQYHGKkNxQWiVl3e72JXNLw1Ccc8dNLiQEizpx3rTl2jeRS0HOI
      kd6kWCRipVYLBocbOc2Lj3v+T5fsasJmcTO5GBgkbqUXBhN1vH/OFrOIeXAfD/qJCeLCQTc1WTzasdOO
      ADcQx9P2Pxo5/FFbeZJ8Nop5n9/zpM/vPWOTVCvKKWEO5vjyJtslm4ucZDtCiIuyz4AHYk7itI3BgUZ6
      wTE40HjgXOABvDp1oAMnS1oOMZLrDRNEnPnFhqWUHGKk1hAGBxl5Pxr7xayfi/xWtcEG6z7pQMzJuU9a
      DjKysgPJi31K7HmeKMimNhOm2xSF2ZJ185NnVCRkPZS839xykJG2D6jLOcbdqtvZkfzsySIxa8nXloC3
      bb5kev9Nu6MNzjHKXvIub/LnjF5N2KjrPTRJVtHmpDsGMDFa+x5zfE36eEF90aNjAJMYfxy2ybimbLcv
      9B6F1EywSMP6sPwigeW3ZHb76S7pXiIl2VHDUBRC2iL8UARKjYwJoBh/TL/Nrpmp1LO4mZMyRxK3slLj
      hPbej5PF7Cq5uruVQ43J7HZJKy8wHbKPTw2IDZkJKQLChnt2l6T7vT7aKS8yymbwAGp7T6cYrZu6oFgt
      0HEWWVon2yIdf5ymg0G+dtNRptWAHbfaTEUfqay/QjLbqOOlJqefivIverioj0ohbtiKCpAY7Znjj4e0
      Tssmy1hhHAcQiXhEuMvZxk11PFeR4usp25ZVW4pGft3m1a4zpMfIFuS4CsJOKifAcdS0XHTqye4vSVoU
      VItibJNea0NYCmQyvmn8VvM9AVj2ZMvet+Rl3lA9ivFNOzUJwUijIwcb9+M7hg7m+9QOMrK8jl8S5IG+
      k1mnOyjmVQeJjt+KGmJ9M/WUApfzjNQf7vzap+zn5rAjFeYOsT0qg0pSWW4J19KQW74jY5tUMdRHR5W0
      FDI519g8kavFEwS4KB08gwFMepMq0sssAIp5idlhgYhzIzsSdfXK0nYsYqbeEBaIOOUgnOdUIOKsCUfe
      eSDiJG1Y75O+taL3SAzM9hELu1fOVSOwyqtkn+Y1UXTifCOjA2hgvo/Wt2gJwEI4I8JkANOe7Nn7FlUn
      rg5bqqrDfJ+o1t8zcqK3lGv7SfT8dA2H3SqryfejgYE+dUfJNoSh7Ejbyhj4gGOefUUqEPLrDq+WI5AK
      Qks4lqYmNytHxjERBzp7b5xDrdz9Op1adPwy055lKspzqkZDgIszy2OBrlPQblcNOI4X3lW9INckOHW3
      gGtuQay3hVdrC3KdLYAaW536saNJJOA66LWrAOtW3YcrCGc+WxDgkkmvT5OklgEPRtxqILAn7MUKwoib
      7YWd1JG6AGczBHk2QwCzGfpv1BH0CQJce7Jo71uoMyMCnBkR3YQEsfdiYLAvq7ZqnH+oS462p317SVhK
      YDK+6TQPQS4hPRmwEmdGRHBmpP9U7LN1nhY8dQdjbvIAyUF9L2c2R6CzOaehWHcKFOkROSpwYjxVh2KT
      yBERJ6VdGHSTi1yPIT7igxWTA430gmBwrrHNSfkZTXjCHF9J72MfGdvUZIJRsfeUbTuoQ5lJV9UStuWZ
      On/27M+dPXOS6BlOoxfGwOoFHFmRixRQltpbl/jI5ARBLk6X2yYN683kj+nFx4sPv462nQjIknzKS0L1
      43CgcUbpNNgY6HvYbyhzqi5oOG+Tjzez2+v2Pf/yOSP0Jn0U9pJuLYeDjXn5nBY5KQlAGrUzkyEPpAJl
      ntHGLN/V8q8kG3+ASE94FmK2HBHPQ3g5rSc8Cy15OsKziCatqVejGcv0eXp79VGvAyGoeghwCVIanRjL
      9PXudqkvmLLo0eVgI7EoWBxspGWniaE+VcmIhvICKCrAY2yrOtlVm0NxENwohgKOQysMJob6kkLNk2yY
      2o627OlKJLlIXqqaYjUo27YhWTYeTb6QDrE9Yn2xKikWDViOVV7SHC1gO+RfcpJDA4CDePSAywHGfUq3
      7VPPtF6tWNfWc65xk61pKgm4jifCGo8j4DqKjPXDTpjr2+1zmkkClkOvAyQo9Pd9A2V7fpMBTMTmpIds
      F2Hxx639Hn77b2qdcURsD62x9drYdXUoVQX7kvyd1ZVKMEHSebRll2WcVhu1gO3InymC/Nmlqel8RGzP
      gZLb1ltt8t9Z+ZSW62yT7PKiUI8/U13J1flO9vSbVz15QNCP0dnxfxzSgtVBcUjb+pOSJvLbFk28C737
      b1tXO9mRKZvHapfVrySVRVrWxzWlqMhv2/TxrVWVF1lCqs491jE3Sb1dv/9w8Wv3hfMP738l6SGBF+Mw
      frPlnvAsxDvuiFge2bbR6o4WsBykhyG37nOQW9VXlHUasUfcQ66rzB5T9coUTXakXFtF6rS2gOcoiRcj
      Adexr14uaBJFeBb6HWNQsG2bylpLzcvytAbu+okFHBpzyL+pRpNmUYRlKTLaTaK/bxtIpz2eAMBxTpac
      W5ZdWosn2dqQVnTYmOMT36k9mhNjm6oNcYzYEZAl+XHIx78T63KekdYKdwRkudBtIt3VcpCRKQz7WN0Y
      WIDHIN7fHuuZ9dSroF5yR2G2ZFWoxeAbnvVIo/ZqwzVXQMkn1zM9hLjOWbJzzMa6Ly0WMUeIEe/uUBB1
      koAsvA60D3tuYqfgiHge8aMmaiQBWRq6xi934rCiag4ryMIqEifOMzKqK7+W2ue0rkQL2A5auXTLpCxS
      1F/SIZaHNrnvzumXpUweCq++7xuod0AP2S51JiatC3NEQA81gS3ON1KO+zQZy0QbhLgjkH2qWhzV+UsO
      pdqLhNQeArRt587RBGZjSLvaHb/vGygLBnvE9ojssKmSOiU9sTUozKb+z2PGc7asZSZeoHdlrEsKXEv7
      Z9qw0uJsI7VnVPu9oprcI6qB3hDxGNye8CyMqQ4T83y0eSkBzEsJ+ryUgOalaD0StzdC7Il4vRBaD8Tt
      fageBDUNOsTyNFXiHM1KMPow6O7OWmOIO9K1srq6FmcZD7QJgYM7G3CgPUA6uE+QDrSicHDLwnNaHDJi
      23tiLBNxGsuZwzp9ZXso101elckToQYCacgusmJLa8N91PA+fEq+Tr92W7yMVlqUbyM9EjEY3/RYVy9U
      k2JgU3vGEMfXkr6V0kXvEd+jXpiqn8mJ1mG2b5ftKE/5ToRtEU1NtLSEZynWaUPUKATwEJ4Q94jnKek/
      q4R+V1lkJdVTmO91Xn38qKdDKdPEJgObklVVFRydBhEn6fBSn0Ss1boh7zeNCrAY+aZ9TtoQ3hTGDUiU
      Az+BDkgKkYakFuS7xD5dZ1SXhnzX4fxXqkkioKc740oO6eRHP8cPdwMKME6RMcwF9NsvyHksEdAT/dt9
      BRDn/QXZ+/4C9DDSUEGAi36fHKD7Q/6RcU0KAlyXZNElZInO1MvhPFW9TnK9oCHbRTyv0UBsD+VN1uP3
      HUNOfCHLglyXWKf1Jlk/5cWG5jNA2yn/Ix+/y0BPQBbKxtM25dgoO7ydAMDRNkJqgmD8/nUgbLspC1aO
      3/cNCfku6inbRuirdV+3eWL/3EBsD2WIefy+aVh0XbWsViP6TVaPl3ko5M2bbt/mp1RQZtBwAxBF9ajk
      JdB6ZD5rm9WeXWleim4F5yulOoFo175/pXbJTMq20erMhVdnLvRKs7R8JY4dbA43JlmR7Qi7uWE8HEGV
      wNgorgOIxEkZOFXooyoHRJzc3z/4u5N8ty/ydU4fXOEOLBJt4OOSiPXA1x4QL/nmPUG+q0hFQ+o0Wpjv
      q/Zqxo+4YgyEB9ysYuwbhqLwBvZDpqGovEIDOfxIpFHvCQE9/EECqgDjFBnDXGSA64KcqM6o9/TH6N8e
      HvV2X6KMek8I6GGkoTvqXVCXoxsI6GFckzvq7f5MrsCguitm1IsZgChlkxeyY18LcnNpoLaXNkZZeGOU
      hVrIfFxscWrTskdapxxzeJH0y/ROJ5sYCFKE4vB+ji+wY5DGYgt3LLZod1BSr/NQLCfIdu2z7Ht7qU1K
      Sk0LtJ3ie76nqNT3HUMz/qnX8fuugfL0picMy3S+nH2aXU2W0/u7m9nVbEo7SQPjwxEIdyRIh+2Ep3UI
      bvi/Tq7I2wRYEOAiJbAJAS7KjzUYx0Tao6UnHAtlX5YT4DjmlE0oe8Kx0HZ0MRDDc3f7KflzcvNAOinW
      phyb3scgE7T8d0HEWVTdvp4s8Yl27O16wyIntMc2ZvjmN8n1bLFM7u/I5/VALG4mFEKPxK2UQuCjpvfb
      /fIu+fjw6dN0Lr9xd0NMChAP+kmXDtGYPS2K8cemASjmJc2ceSRm5SdzKIX1XLRsWnnmI43ZKb0oF8Sc
      7OIQKAl6qxb1+JydEqYBi0LbnQ5iPfPXh+X0L/JjMoBFzKThhwsiTrXBDGn7RZgO2WlP6mAc8R/KuOs3
      +HAE/m8wBV4M2VH8Jlt46gNDCEbdjFJjoqj3oDs5yUr9PMEMYDm8SIvlZDm7iiyosGRELE6WI5ZwNH4h
      xjSj4kX/vmDJXn6ZTyfXs+tkfahryiMLGMf9etvs7mBAbhDTEY5UHnZZna9jAnWKcJx9pSZC6pg4ncKL
      s16tzy8u1X4z9euemi82jLmzMsLdwb57u1Ifn3PtDo75L+P8g9cfZUfdT6n8X3Lxjqo9cr6x7YmovrU+
      Wp7eiwYMfpSmjkgTCx5wq38SZvlxhRdnW9Xf5Q3RqIOm88eyqrNkl26ek5d8n1Wl/lRtPKhW0VPmXzly
      /9rowxRwfKIPWeQVAxP1vI/rnUrglNzy9SDm5NVvNjzgZpUpSIHF4d0XNjzgjvkN4fui+xKra2uxmFmP
      d79nrzz3kcbssgkdv/0agGJeylMDF/Sd6pCP17Yf1h7Jx+0LBUzBqN3Zem8R1lUF47YXGh/U8oARedWe
      QWJW8ummCA76ddPQbayWVyUjhGMAo+jUo+wWD7GoWa0JjMhiVwHGaZ70KVbyu4SHFjDu+59StRKXPv7u
      Qc+p1kimYkcUdpRvazuA5H7jifOMuloVr4Ly3jqA+l59ENc2VwfA5mmRrA6U5doBhxepyFd1Wr9y8s1E
      Pe+OM8O9g+e22z9zLtEgfWu2I7yZa0GeS9VOvJrTIH3rYZdw5ptOnGesYkZlVXhUVpVrasWoEM+zr4rX
      8/fvPvD6Ug6N2xmlyWJx84H2CBWkfbscCwlZVayqn6xLd3DPX28YdVgLIS61Z0+T74vsknK6WEDhx8k4
      lUxHAbZtu82xHKwkKrjeEpL0QsKQCI+Zl2tuFIl6XjUjpt5riuk3gg4w0tv0yQWhTy7erk8uKH1y8UZ9
      cjG6Ty7YfXIR6JPrwwA3MVdv0KA9skcrxvRoRVyPVgz1aHkdO6xP1/1dz2CJLGNqTzjqz7dJ+pzmRboq
      MmYMU+HFaQpxLtsTao1+xAzfcp5czz9+pp0cYFOA7bi/Nll4BAEnqcU1IcCl3nsj5L6NGb6n9EqNSYhT
      WhbV266ni+Mk3fuxLpOxTdl69Z7ayXQ5z8gUIr5NdqEewbCkDuuZ30eY3wfMJT1/joxtKpnXV6LXpupS
      wuSkgYCe5FCunzLKUUIg7Lsr2aHZp3XekC+1Jw3rl0RHGu3qvu8bkv1hRUpAh7ON1W5/kN0noq+nMJua
      WXki5AkEo27aaTYgbLkpT4O6r1v86ZwGWjKaGOyTpSjdZU1WC8LGgqjAidG8Sx5JTgX4DupvbhHfs6da
      9oDjB/kXSQTw1Pkz54cdOcBIvmlNzPf9oJp+uA519Mdvv5//nly8++WSZrNQy3vceL8vdwSzD1tuwpLK
      9ts2Tdw110AsT7vsmvX7XNTyCvq9JKB7SdDvAwHdB3pYpd8lo5k6yHYRzt7uvm7xtCWpJ8B06FQXlDOb
      TMYwzebTq+Xd/NtiOaeeFAuxuHn8MMIncSvlJvJR07u4v5l8W07/WhLTwOZgI+W3mxRsI/1mC7N83asG
      ye3k65T6mz0WN5N+u0PiVloauCjoZSYB+utZPxz5zbyfi/1SPQe3pzzOBWHDvZgkixmx9jAY39S1nVRZ
      h/k+SgL2iO/RbR7VpCHb1Q5h1Mu9aXOoSUYHtb2bKkbt055dfUJUKsTzPGd1vn0lmlrIccnG8foLSaQJ
      20ItuX6pZQ2aHA4x8oZNqMGNQho4nQjAQv7lXn/v+Nc92bOHLD/ov8vuN57+Sh1AuSDkJA6hHA4w/iC7
      fngW6iMXBwN95AVQEGubIwZmII3YZe4xbmkAR/yHVZGv2foTbduJbZ3XzrGHhAALmnmp6sGgm5WiLmub
      BaNuE2DdJhi1kgBrJcG7UwV2p1Kbdb9NJw2Ku+/bBuKw+ETYFnrHAuhVMIbXJtS7ple8WWmXw43JNt8L
      rlbDlpvRk7cp2FYRT1KCWMisWjG6U1GYLal5vqRGjYJpBH8xcWTkgbDzJ+WtcQ+EnIRWyIIgF2nU5WCQ
      T7BKjUBKTVNxy/aRdK3EcZYFAS5alehgro9+YdBVqb8lL3nzlJRqKaReLFZk6Xezfee8TsWz+1f3d0aN
      +LdX0jjJ7qd58vlTd+qq7FE9jT+3zyc9a5mLZn9x8QvP7NCI/cOvMfYTDdr/jrL/jdnndw/3CWGBtMkA
      JkInwmQAE61RNiDA1Q7i2/mBqiZbbRzzVzVhH2oAhb3t5mrbIn3kqHsasa+rbbpmpskJxtyH+jlTJZAn
      P9JBO2VeF8ER/yZ75JTAHkW87GKClpL2tiZsXO+TgFXNRaxeY5LZMyBR+OXEogG7TjHSk2MABbwi6r4U
      A/el+pxfWVk0Yte7KKjXhtTx3uqQNdk92LEigSYr6h/Tb908O23s5oCIkzTKtDnPKDM8l0VJj8FEtq7H
      b7OHCvwYpPaxIzwLsW08Ip6HM40PoEEvJ9s9HoigmuS6IidnD8JOxnwdgiN+8pwdTEN2fR9S72WPBc1Z
      udbVlWCYTyxspk3s+SRmJU/EI7jnz0VS7dMfB+oteOI8o8zPC8LLUzbl2Y5T5qymGxagMfi3S/C5Qfcd
      0rTKkYAs7J4MyIMRyEMzG/Sc7TQ9+6JdHPHTH3wgOOZnl4/AE5DuG9xemMeCZm5dKoJ1qYioS0WwLhXs
      ulQE6lLdm2Q0sycONPJLhUPDdm4Ta8MD7iTdqg9lXsuhQl6mpDnRcT7vCmgPjSzIcn2dLr/cXbcbYuRZ
      sUma1z2lggF5K0K7fIpwpLbJACb9Fhi13+uikJc083ViIBNh/3MLAlybVUFWSQYyHei/zx1x0FcMWhDg
      0jNTMbdPSDM6HnHKYUgFxM3VsLghx2gxyCeSVL0JrjY9aOilzcZhvxzC604DR35kAfPuQC/RkgFMtD4h
      sDb09Ndq3Vzo+Quy70QCVv33i/VqRbaeSNQq4zKtkgSs4m3uQzH2PhRvdx8Kyn3Y9sl2+zoTItu8SWxc
      h8RvKv6N6/BWhK6Ln28uSsIpBB4IOkUjP9swnC1oOfU5coe8aPKulqCUMx823NcXHz6c/676UPs0Hz9h
      amOo7zidN/6dRVTgxyA9XzYY30R8/mpRpm12P5kvv5Ffk/BAxDn+PQEHQ3yU1sDhDOPt59kt8ff2iOdR
      hbV9wE2cE4Bx0D+Psc9xtz7n5HinZeWj/EgQI0AKLw4l306EZ6mzR1nVqDNQi0LXyEXWULMQdHiRRFye
      iqE8FTF5KrA8nc+TxeTPqd7hnFi+fdT2qi2DsrquatqMg0eGrFu+dmt72zGg/pjiNDDIJ15lwdlxtSZt
      29ufQTvazuVwY1JynUlpW/UuyO1HguI0Ocd4KNfsn+/BtlvP61Oz6gQhrqRQf+IINRmykm8sAPf9Zfaz
      /5be2JEawjfYUeQf2Vnoso5ZtSwfZ3ecMueygFn9B9dssIB5Prm9ZqtNGHDrXVoqtt3Gbb8+3JF8y/QU
      ZiPfNA4a9JJvG4gHIuhTq3mJ0aNBLy9ZHH44Ai+BIIkTq9qrQeourb+T7D3m+Gq1tESHJBVrk8ONyXrF
      lUo04N3u2d7t3vEeOCXuAJa1OktFVbIrZgB3/bvqWbXqhC3ZXA40dlv3ccUm7vpFo46eYJgN0HaKlJMG
      PeXYZGtLvZ2OjGH68z6ZTCfX+mTTlHAekwciTuLZcBCLmEkjFhdEnKoLM/4MBABFvJS9Az0w4GyX9m/y
      OltT9rwf8iARKeNyh0OM1T7jXbQCA87kMW2eCCtpER6JIDLCW0cuGHAmYp02DfOyTQESo0kfSS83ASxi
      puyQ7IGAUz3ypu1RBKCAV72lJSv++olT05kw4uamsMEC5vbVHWZ6mLDt/qheuFpWfxCWQliUbbua3X+Z
      znWm6sMNaa8OYQI0xjrfE29wD8bd9DbLp3E7ZS2Aj+Lepi64Xomi3m6vT0qfEBOgMWgrngAWNxN7CQ6K
      evWj/v2eNl7CFWgcas/BQXHvM6NCgXg0Aq8OBwVojF214eauQlEvsadjk7g133Ct+Qa1qk2huUVEs6hZ
      xJdxMaaMqy/F1AAnPhghujzakmAstRUtv8I0DGCUqPZ1oG3l5gOe/jE1TbiWicrRgZxk1ixorcK79/37
      nt7tgfo6+m+f8jItCPto+SRknVEbrBOF2ViX2IGQ84F0No/L2cbrbC1z/GMqsl9/oRhNDjSqu5QhVBjk
      0zlG92kM8lFzuacgGz1HTA4ybm7I9YIFek7Vg+XcMA4KehmJecRQH+8ywbum+4yVST3oOPPHTNB+tCYg
      C71s9xjq++vuE1MpSdRKzRWLhKzkonOiMBvrEuFyoz9aUFaxWRRmY+b3CcW8vLQ8kpiVcds4LGTmWnHj
      n7Q1gg6HG5m5ZcC4m5djPYubuelr0rZ9WrLadQODfOTUNTDIR03RnoJs9FQ0OcjIaNct0HNy23UHBb2M
      xITbdeMD3mWC9XP3GSuTsHb9y/0f03bemfow0SYxa8505pCR88zTAhEnY/7YZRFz9nNf1Q1L3KKIlzpL
      aoGI8/tmy1JKDjFyn96AAiQGcebP5BAj9RmnBSJO6hNIC0SdjX4bdJ3v86xsmHrLEYwksnJDm8oABSNi
      tE+31UsWrI30aFrkeqhPSC0QcP5x/YlTGbYY5Jt+Zfk0Bvq+setBg8XMxGdoFog4WXUgsHuO+RH1HEoQ
      RtzUJ0MWiDi/ZzuWUnKIkVOf+nt1mJ9w9gdAeCwCfY8AGEf8rLrgCNrOr9cRT9w9GHQz7uKvgfVbx8+I
      d7CBoT5i39gmYas+g5oj1SDo7A6YZkg7ErRSa6+v2Fq4r7wVa1+x9WrdB7sNw7bbwK7qmfNbFQb6iHXU
      V2RVW/d38vNYkwONrOejLgubeTUGWleQtgqxMc/HrtMC9RknFeHUU6/TtXucMJQ27LmJzwpbwrMwUg5M
      M0ae+vl5/3GaCNJZwzbl2P64WlxeyFbxG8l2olzb9NuF/pBmO1K+jbUeywIR54bWDpscYqS2GxaIONvd
      CIndJ58O2WuRJlWa7ZMiXWUFP47twSPqL+4et+fEhgxzDETSlxQZqXMMRGKsVMEcQ5GESERaNMT1sSFP
      IOLp3LaYZDQlSCxi38HkcCNxJO6giFe80X0jRt83eu+4dbsPoFoFyg1nSUbEkgPnfgOT6KCWLRBdJYms
      tdTXSZtKD3jGRZRjzuzn/i1itqaBqDE1oRhVE4o3qAnFqJpQvEFNKEbVhMKowbrUjvxllokQ9Q2yz9eN
      jx/TDOC6EfHfKvBwxOj2Rwy3P6kQxMUVBob6kuvFhOlUKO5tt5zkqlsat8/5Vz0Hr3qViozTEHccZOQ0
      C0gbQNmb0mBgE2enXxiH/Gp+LSaAzQMRNhl9ZGlwuJE8C+bBoFsdBMCwKgz1cS/1xOJmvRw9oz2qg3gg
      QvdqENnccbiRlxwmDLhZY2VknEw6rs+EEBfh5GeXQ42MGvUIYk5mG2CwmHnOvdo5drXnzDQ9R9P0nJum
      53iankek6XkwTc+5aXoeStOmEOo+U0uyaPurBi1wtKROX7jPCzFHKBLruSGiAOIwOiNgP4R+RoVHAta2
      M05Wthjq41XkBguYd7ns95WPMZ0SXwHE4cwNwfNCamIntiwDjlAkfln2FUCc49QK2X4EA05embFoyK53
      42mPNqbLDRh3tznDlbc0btfZwZVrGHALbqsm8FZNRLRqItiqCW6rJvBWTbxJqyZGtmp6t2fiEzkLhJyc
      WQRkDkEPqFn334kErX8zfrH3NFP/mZV6SMoRz9ywMcD3TH4Jw8BQHy8/DBY319laLajlyjt80B/1C0yH
      HYn1NhHyHhHnDSL43aHjX4nLmQzM99EX+WPvHzHf6kHf5+G9yYO9w9P/nZh6Fgg56SmIvwuktiNu96BJ
      0iJPSd0Jl/XNG/K7lT3l2NTueGkmkvOLy2S9WifiKdWtFEmOSUbGSvLdXvY9curObKOEoWtY75JVccia
      qqK9cIRbxkZLLt8mXnIZitjUydMu1ely8eFXfkTbE4j4uN6xo0g2bJZDjnKjN7uKidFbBqKJiMLY8QMR
      ZEk9v4iKoQ0joryPjvIei/L7BT/XWxYxqyPro2skVzIyVnSNFBKGruEN7ljAE4jIzbuODZsj71jPMhBN
      RGRW+I49foN/x1qGEVHeR0eB7tj1Uyr/d/Eu2VfF6/n7dx/IUTwDEGUjryTbZO/jbl/QMjZa1A08aASu
      ojwUBf+3WjRg/xmfcT8Hc+7Uj6K5Txjia2qWr6lhX0bYudvGYB+5AkR7K+0H1ZZ1fRIDfLKB5ORHiyE+
      Rn60GOzj5EeLwT5OfsD9iPYDTn60mO/rWnWqr8MQHz0/Ogz2MfKjw2AfIz+QvkH7ASM/Osz2rYr0e3ax
      IvaSesq2MV6BA999U00HsYR0iO8h5mSHAB7aPncdAnreM0TvYRMnmY4cYuQkWMeBRuYl+leoju1WTTxF
      dmRsk3qK3M4NrV5Jx8IDbMBMew7toL63nXniXbHJBsz0KzZQ3Fut/sX1StT2PqVCV2dPab15SWtSSris
      Y95/z7gdGpdFzIymwGUBc1S3FjYAUZ6+b7aMEbXLAuaf7TmaMQF8hR1nl9byz0VXrJK0eKzqvHki5QTm
      gCMxlyAAOOJnLTzwace+IW3PKb/u8h9o/AeP1yM4okQztmkvf2kWld+wAYrCzGsPBt2sfHZZ21yvL5Jf
      3lEb5p7ybQwV4PmF5nDKHrXc+GVGzx1s9VZl3Z4161q9XnDYbvOfVDUq8mJeXPxClEvCt9CqTaiWlH97
      f0m9Fkl4lg+0+b2WgCwJ/Vd1lG1TU09qHkovkt+lpMLqsrC5qyfUQ/R6w9FbAjhG+9nxm+KwV1uVZaxo
      iAqLq48BY7z5BRuMKH8tp7fX02u9bcvDYvKZeMIujAf9hAfoEBx0U1YygnRv/zS7X5B2Vz8BgCMhbLVh
      QY5LHwO3rg4l4fQlD+ydn6e30/nkJlGniS9IGe+TmHV8drscZiRksgfCTspbSi6HGAk7ILgcYuRmTyB3
      2hcLKnWE2C1hUBtQhOI8p8UhIobGET+vkKFljFvEAiVML09lOTWJWMUp8Utu/tmKUBx+/olA/i0ePi7n
      U17xNlncTC8cPYlbGUXEQHvvlz+uR+/grr5rk2q71LTcUAQd4nmaOl03RJFmDNPXydVog/yuTXJ2cXM5
      yEjYwc2CEBdhwZ7LAUZKsbcgwEVZfGpBgItQvE0GMJH2GbMpx0ZazNkTjmVGTaWZn0LEhZsm45hoyzUN
      xPFQVp6fAMMxXyzUC8Hp+DvvRDiWrKRaNOFYjpuKUiZePNBx8qfuENzxcyeMQNh1V8Xre3mzPmfj99X2
      QNC5OxQMoaR622yxeJBfTa5ni2Vyfze7XZLqNQQP+sffwyAcdBPqPpju7V+vR0/nyK9aHK26OwG2g1LZ
      Hb9vG5Z1WoptVe8omhNku2iVXU+Ylg/j8Q8WR03PD356fiCm5wcvPT9w0vMDnJ4fyOn5wU/P6fLL3TXl
      5aCe8CyHku7RTG/Sw4Wru9vFcj6RN9MiWT9l4w8igemAnVJLgXDAPb6gAGjAS6idINYwy08+0ZLgRLgW
      vQsd7XB3DwSdTU2Y8XQ511hU4w9k6AnIkqzyim5SlGujZOcRMBzT5eJqcj9NFvd/yE4dKTN9FPUSyrIL
      ok7KD/dI2DpLVr/+ojqlhGlbjA9FaN995UdoeSwCNxNngTyc6btC9i4J3VKMxyLwCskMLSMzbhGZhUqI
      iEwHMZgOlNeUfRKz0l65hVjDfLecXU3lV2llzaIgG6EEGAxkouS8CfWuu4//naxX4oKwpspAHA9tUspA
      HM+O5ti5PGmb/56wLRvaL9m4v0L+x0YV1XyjVmUIistBUe/qNUbd0bZdP0OgnBBuQbaLdphzTziWklo4
      W8K2yD9crFcriqZDfE9RUjVF6VsIqw0NxPcI8tUI52qklprEHeJ7mp8N1SMR2yPIOS6AHJdaqqZDfA8x
      rzrE8NxPb9WX1JvZaVH0y7REsq7K0YPBAY0fb3XIC7X/XbvjsaDGcXDfr6tvkVG9HYb4CPWujcG+mtR6
      +yRglWmdP5KNmgJs+4OsjPVJZGRlj/pezq+Gf+/jrsl3ZFdLYTZZhv/FMyoStW7y7ZapVajvfUrF0/sL
      qrKlfFuevr9Yp/vknio8gYBTPTDRG11WZGuP+t7iSQ7xiqwhZ/wJhJ2VrrnqR472yIJmToHvMNCXyypq
      /FMEDwSdhA67TcG2w04ODLKd4DiPLGius6bOs2dOeh7RoJfy3AfBAb+eO1JtlmyydtXmUNCbPMjhR9rJ
      clitqe6Wwmyk59IACniz3YbeqLSUbysrZsN3An2nHHZxErLDfJ9o6nUqMsoA0iNBKyMdWwq0qeaBoVMY
      6CvWacPwKQzx7V9Zvv0r6Cv5mVKGcqXkZUuJ5UtJOEzAwXxfUxXVy/j1pw5m+JZfpnPq8ksLglykxtKi
      IBuh4jIYyERpIE3IcO2zEh4kjRajBjxK+0okO0SH4/52BTzb3+G+/1lGJTyNcjDUp7oXTKdCe+/99Gsy
      Wdye66XZY40WhLgoj6Y8EHC+yBKSkYWawmysSzyRtvWvD+9+T2a3n+7ICWmTISv1en0as7OSA8Bt/+q1
      yQTrym3Stsr/TNbynlul45/Iu5xr/C57eNuKZmsZx1QlT/Kix7dKFmS71JMu9e7M1exe1sM6oSlWALf9
      +1p2bCm7u1qQ7aKWeb+k67y+/kLbL9oDIedict++WvnH+CERTMP25P7hI2HrZQCFvdykOJKAdXoVkRQm
      DLq5CXEiAas6MfQ3slFTiO2SZbvEbPLrsz/1y1vUGxRzQJF4CYunKr8UBMvAPOpemw/ca+pzvS6VKz/C
      sJubyvPQfazaSLJRQYgrmTz8xfIpEHNezW94Tglizvn0nzynBAEnsf8A9xyOf+W3MyaMuaPuAc+AR+GW
      VxvH/TFJFGiD1OdR7ZArQGPEJFCoTVKf89qlExmwXrKtlyFrZDuFeLCI/IQPp3pcqRksM/Poe3c+4t6N
      asdcAR4jJhfmQ/UDq107ggEnq30z4ZCb086ZcMjNae9M2HaTJzuAeY52UM5p6mwStHJvFABH/Izi67KI
      mZ0gcKvWfsht0nwatrOTA2nJ2g/JzZiBYb5Lnu8S9cUkrCMYEYNyCHpQgsbiN8WoBIzFLDCB0hKTEcE8
      mMfVJ/Oh+oTb5Po0Ymen9jxYW1Gb2Z7CbNQG1iZRK7FptUnUSmxUbTJkTW6n/8M3KxqyEwepyKz56c8R
      bTc+TjU+j7vnBkaq1pfYd0dorGp9IyqhQu16zHAVNuBRopIp2M6zhqwOGvJe8r2XQW9swo9o/4Gv8foA
      iCgYM7YvMGpcbnw1ooANlK7YjBrMo3l8fTUfU1/F9RXC43PrO1G5MR+sFXl9B3iMbn/G60Pgo3Tnc1Zf
      Ah+nO5+z+hQDI3Xrc17fwjUYUeTtfX6R3H+cqtUmo80W5dlor3BZkOeiLHUyEM+jnlh/l3VmWm6SdVaP
      X4yD8V4EvbkJ0aoZz9SdlUnYQtQDbecHmVV/XH+6SCibV3lgwJksvkzO2WJNu/b9KrtQrymrBe6k1bUI
      DvqzMspv4rb/t2R1KDdFpmoMUlGzQMSpyl++zdfyfuG5TYEbg3rD/Qbcb7/p24X+048UZFO1Gc94JDEr
      PzkhAxQlLsKQXZ3vHhfBNbhRKG9794RrUSt7klyQXlD1SdRKOmkVYjFzd5dnG578hOP+56yo9nx/h2N+
      lRdcecuGzZNyM437Cb7HjugMQMh1FMSHI9CaA58O2wnrpBHc9XctHc3aQa6rK7A0Vwe5ruN+cqebgHOO
      wQiVG7fdae4NogZEXkzVP1Rv0xMjHDHQJ3g+YfvubmZX3+i3jo2BPsKNYkKgi3JbWJRr++fD5Ib5ay0U
      9VJ/tQGiTvKvN0nXyt4BDMGDfmpqoPuAAR+TUwXfC6z7/Ovk/l6R9Ms2SMzKSWsTRb3ciw1dKz1tDdKw
      zu/+ksk+nS/b5kmfOrCY3d3SEiNoGRONkEQBx5hIlIQLSdxYXSrTk80AESc1cU4Y4iMnQc/1xvnk9jrp
      3iAaazMZxyT/kqWvJFGLOB7CTNjx+45Bv2JCcmgCsrSH+6gzTdT+gepoMMLwaUDjxCNu4GEyjil7pKWg
      /L5rKNNVkSXbqv6eHEqRbrNkddhuM8pWiYMiJ+Y2l1+kHDJgU46tHViXm2SXNU8VLT0c1jHr19xVWJLz
      RDm2fTX+uMsT4DpEdthUjGJvgo5TZBkt0RTgOfh5IIJ5IJq0OdB+a4sYnqvR+ybLr1qcvjjCWMZADI/5
      wIqyY5oH2s7j0ymq0uQs4/8m5+8uflEbOqhzHZL0+ecFwQvQlj25XyyS+8l88pXWUwZQ1Du+9fVA1Elo
      gX3StqoXjfff1+JcDm8zwjF0EGubV/n4Jy3H7zuGIi/VeV7J+PecHcz26e2SZT24J11XT0E2yp1oQraL
      OIdjIK5nmx6KhlrneaRtJc4KGYjt2RbpIynpNeA4iLepf2+aJygQDrkA0ICXWsg82HU375J13SS09UgA
      Cng3ZN0Gsuz253SRhEDXD47rB+TKyKIMsGzTdVPV9ITvOMCY/9jtyToFAS5iJXRkAFNJ9pSAhf7DoF+1
      F4Jb3nsU8P4g6354Fnn300ZjNgb6ZNucyJaLWiXZrG3ORVLt0x8H0k1wgmxXxMnTCI74yQfMwLRtJ3aZ
      vH6SSmB6q9pTtq07qFT3oPQCjuRuMr1Pdo9bUr0X0AzFU33C+HBHy1A0/bQvMlbrGBXp4g0iXeCRyqrM
      uBEUC5vbruEblAZQNByTn0e+ZWS0izeJ5uUU88x0EAbdrBoKPwFLf0o5QPMEeA592YzRhIPCXsY4wEFh
      r+7z1tWOOImEGvAoTRUXo6lCERrq2Ucg7Ljb8sLJUosErZwMtUjQGpGdkACNwcpMH7f9gj/SEqGRlmCO
      IgQ6ihCMnr8Ae/6C158VWH+Wsmbs+H3foDvx1DbQAgFnnb6QdZJxTX9nNMvfTpt/2FPOJOsJ20I7M6Un
      IEtEtxAUgDE4OeqgoJeYqz3V2yirmO01y+pftMP3esKxUI7fOwGOg3wAn005NtoRfAZieS4ufiEo5Ldd
      mpy+J8YzEdP4iHgecsr0kO368CtF8uFXl6anzZHxTNS06RDPwymDFocbPxbV+rvgelvas9Pz8gRZrveX
      lHIuv+3S5Lw8MZ6JmJdHxPOQ06aHLNeH8wuCRH7bpRPandIRkIWcyhYHGompbWKgj5zqNug5Ob8Y/rWM
      Xwr+Sk4dYXGekZVmXnrN7r9MFl8SQot1IgzL/eSP6cXpMPvRKhsDfYSJTJvybKdnTjvxSFSaqOdVe7lm
      qrtG1hqkYSUt7XJXdbX/pm6XbVO9bTl/WCyT5d0f09vk6mY2vV3qST3CKAw3BKOssse8THIhDmm5ziKC
      2aIRMetsk+32lJNvR6iCceXfc/H0Fj/WMY2J+iY/13OFIxNqCAQP+gk1BkwH7WoWQNR15D1gWOBo6iT6
      6TzmbrMNwSjcHDHwoF8VyJgAmg9GYOZ5TwftqmBnu4gArWBEDMrQPigJxlKlb5c1qZrKiixermowbsS9
      41vgaJJt/4Nbri0BHKM9Vfo0m31MAk40RAXHzX7uszrfZWWTPJ9zolmC4Riyk7JbxcbRkjGxnqt9vY2P
      pjVwPG6RwEuCuZSJYzZ5OAKzcrNqtYfFdN4erUxKAgcDfePHRxYEugg/1aYM2/LTpVomMnpHiRPgOPYH
      okMBveOviw8fzkfvHNN+26VVmdineU2zHCnP1j0N0s+auuqGaAYMRpQP737/871670dtQtA+/qccG4vx
      YAS1v0tMBIsHIxDejbEpzJakRZ4KnrNlUXORj98QAEBRLzd1B1O2/TQR32PkEgf9xLd7fBK0bi5yhlFS
      oI1SCzsY6JMVGEMnKcxG2bzNJ0FrfsExSgq0ccsmXi7bQsX73ScWNJOWu7gcbky2e65UoqD3Wa9ZLBna
      jvSs3Yl8ssUQ2Zoy04DxXgRZIZwzCtcRg3zqFaZyk9bqTZomK9W0mKDrIQsYTabdIWP4NYcbk1VVFVyt
      hgfcCfkO9PhABPo9Y7EB82H9lNZst6Y9u64AGNX6ifOMfaFhVSAu7vlVXU1v1ToKtPHucIOErQ3lXVgP
      BJ3s+8OGA256hlmsZ24XVDJ6ej3oObtU5xRbEwW8TbJufpKVmgJtnNb+xPlGXTBYP7snbWsyufl8N6e8
      AGlTkI1ylK5NgbbNgWPbHGAbNfEMDPRR9hNyMNDHyQgsHwjzEjYF2gTvlwrsl+pJ2A3PKEHXuVzOZx8f
      llPZMh1KYiLaLG4m7ZsKwgPuZPWa3M6uo0J0jhGR7j7+d3Qk6RgRqfnZREeSDjQSuY4wSdRKryssFPW2
      b0ISJt4xPhyhWv1LtnYxMVpDOArlEFmMRyPk3MvP8asm14omiVplpXQek6cnPhwhKk8NgxNF7380efiL
      XuQtErMSs9HgMCM1E00Qc5JHKw7qeme3nxjpeaQgGzUdWwYykdOvg1zX/Ia+46dPYlbq7+05zEj+3QYI
      OL9Ol1/urnm/3mBxM+d6exTwppvNu6TOnqvv2YZsNmHYfa7G79RZLQ+G3epTjlZxgLF9RVEc8iZbkbUm
      DLmJI6COAUybrMjUq3mMn96jkDffbulGCYEuytbODgb5DvTU8/tx6q+sGxO5I3VvRfZD1UbcZKcJB9wi
      q/O0YNtbHPPz5oQhHotQpKKhLfDFeCxCKS8iJkLPYxHU22Rpc6iZAU447E/m0z/v/phec+RHFjFzqoiO
      w42cAamPh/3UYaiPh/3rOm/yNe+2ch2BSPR5B48O2Ikz3i6LmPUaxZolblHEG1cRDNYDersO+mjLoxF7
      XCUzWMf0dQT1qS1sQKIQV9NDLGBmdMnB3vgubdZPZJWmABunmwz3jxmDwCOF2YjPuy0QcOpRfMQN5vBY
      hIibwOGxCH0hTovHihfFdgxHIj+yRiVwLObmfgEFEqetfkm74WI8EoFfx4qBOlZE1E4iWDtRNjWwIMRF
      fRxogZCzYowdFAS4aNsTOBjgo21U4GCO77SLOvnJokVi1oinJYhjRCRqNxVxoJGoo16LRK3kETC2r7/z
      oT74itOxhhXBOORKyMeDfsakOiRAY3BvgdAdQO3xIOcaOJ+J+FwVY3JVxOWqGMpVEZurAstV3mw3NtPN
      mpNG5qNv7u7+eLhXtQx5xbbLomb5t8espveRQQMapeubMCbDEAcaSRzohcSjYfu6qVnXrjjYSDlRwOUQ
      I7UcGxxsfEqF7PblNcd6ZGEz5QhQl4ON1Puux2CfeDo0m+ql5EiPrGPWq4int8v5bEruSTksZv4W0ZnC
      JGNiUbtTmGRMLOryE0yCx6J23mwU95LvUIfFzayOFcCHIzAaYdCAR8nZ9tA9Qa0bbBT3iox9uSJrgt6o
      3BSDuSmic1MEc3N2u5zObyc3rAw1YMitHwKXTf1KN5/QoJddebqGwSisatM1DEZhVZiuAYpCfTB+hCDX
      8fk2L2NNGrTTH2obHGjktBFI69CmM/2RkwtDbl6bg7U27WJF4kMmi0Ss3Iw/oZhXb9HPvqNdw2AU1h3t
      GrAoDfMZLiQYisH+IQ36JFd/RY0L6GJFYbakKjY8oyIhK6fRgtsqVs8D6XNUZVbkJeNm7kDISR/89xjq
      Ixzx45MhK/XZmwtDblYfzu+9ydI+vWrfjVZv0zWyTqJN2kACOIauSdUfOP4TjLrpa8AdFjbnm5/cORrQ
      AEeps6bOs+csMhSgGYhHfwIOGuAo7VMeRgcB4J0I9+qce3If4URBNmqdd4RcV3uE7e3dNaea8mjX/vCR
      98t7DjYSN0EwMNT3rt3enqnt6JCdfLhGQAHHyVmJkiNpQi5hJwz2CV6eCSzPRFSeCTzP5vd3iyl1VxiT
      Q4yM3UpcFjGT36g0wYCTvlbCo0N2EacXYb9+pLHh6ls6bI+6/pMgEIPeFnl0wB6ROMGUaeqD4F+1phE7
      vQo5cY5R7QrFey5pkZiVWBMbHGak1sYmCDj1qyNp09Rk6YkMWTnjZ0gwFIM6foYEQzGoE3uQAI7Bfb3A
      xwf95GWzsAKI077WwziWDDcAUbqpR1aJNVjITJ+07DHIR2zhOwYwnZKelXkWDdhZFR9S50W8BeLjsP88
      yXZpXnDcHQp7eUXqCAac3CrQ4QcicCpAhw9FoHdAfBzxR9R9No745WCJUxn1KOLlv4kAGrAo7XwIvQMO
      CZAYnPXEDguYGV0fsNfD6fDAfR36vMaJwmzUyVcTRJ3bPdO5hVoPwb8HROgeELGlUwyXThFROkWwdJJX
      ux8hxEVe7W6CgJOxorzHPJ9+95H/jjkkwGOQ36Z0WMTMfJvbxzE/ub924hAjo2fVg4gz5m1kxBGKpDYs
      WKdq27dr6ttMAU8oYrvq9PawW2U1P55pwaOxCxP87q/zKa/jBymG49C7f5BiOA5rgXvAMxCR0+0EDANR
      qO8HAzwSIeddfI5dMb0vdOIQo2ol3+Am9zWBeNG3uCtxYi1mn+l17xECXORZ9SMEu3Yc1w5wEUtXiwAe
      aqnqGNe0vJtP9VlsnOcbHo3a6TlroahXtxvkDUoAfiDCU5qXUSGUYCDGoa7VyShr4msUuGZcPMaWCEFT
      OCr9kR8kGIyhU4DYuUctA9GqIl+/Jg2/hLuacDzRVHVUJC0Ix5DNr3qQQ9wxC5OEYp3H3lvnw/fWeXQZ
      Px9RtmN/yPDv6O/tqArP0gTjZXVdRaRayw9HkMO8ffMUG6e1hKP9pL8zABqGosiGtl2tGhfqpBmIt5dV
      R950VUhUSMuERiW/mmajqJfcpzFJ1Lo/1PtKqN3an2T3k3vhjgWNppemyMZXMOOc+HCEmHZUDLej+qVm
      fi1zxMP+iPpSDNaXxsYiETE6w0AUfu114oMRYuphMVgPi+iaUYyoGdV3tkX6GHFftHwwQneXRsToDMEo
      Tb6LCaHwsJ+8BgfggxHaKedkvYqIcnKgkbr+nzpfZ/2dGclyoJH+zuqKGUChoFfNbDPrwCOKe1mDvI5E
      rUVVfWcN4XsYdDNH7+jI3dhrnVMdmDju57aQA6PMdsgh85Z55R0ccPP6DicWM3PX+0MCNIb6bczCbeK4
      X682ighw5Aci6OHeJipIqxiI00+/RsXqNXg89vyeQaP2dmsjbq50dNDOHsLbAjRGW/3F3NmWYjAO+y43
      DWgUxpNoFx5w8/oOj4P9hqJKVVvUlmZOEtkCMAZvnImNMfVmidzWpocxd0ydKobqVBFZp4rBOlXE16li
      TJ0q3qZOFWPrVBFVp4qBOtXcFnOfNk+CGcNyBCLxRrDh0WvMiC882hNRLY4YaHFEbIsjhlscEd/iiDEt
      johuccSIFidu5D006o4ZEYdHwyKmpRThljJ2lD08wmbsh2qCjnM5f1iQT1PvKdDGqR8tErSSn+z3GOqj
      L4Z0WMzMeI/NYVEzfZ2Nw6Jmeq3tsKiZfh87LGimvll2ojAba+bYox37nxPG+SxHCHARH2X8Ce0Wpf5I
      7Q13jGuazmefviX3k/nka3tuEuNxFCYZjNWkK+JekYhjINJ58lQRCzCsCMVRlV/NuAkxSSgWvUC6dMhO
      rqo9eshOr7hhxWCcfZbVbxDrqBmIx6jcYcVQHHrXH1YMxYkszVjLYn2J84AXEoRiMKbYAT4UgVwdO3DI
      rWYb+HJFD9kZL/ohjsFIcTXxSTEYJ99HRsn3I2IkqVhHx1GSwVhxtdhJMRhHN915JiJjHTUD8WJrMjGm
      JhPxNZkYU5OpL6my+QaxTpqheJwBPCYZikV+gA4aBqOQBxuwIhRHdxpZA11c48RjvwEWePNLf1Rn+jU+
      xia3Pg75deKx9Sbt28lvAcHvqend/+nd1B4DfeRmtsccn17jxD+51cdBP2MmyQQ9pwqXfidOe/QY6Fun
      DNs6BV30PorBgUZyX6THQB+xz3GEEBe5b2GCsJP+LCfwBCduF5KhHUi6zxnNm0WCVnoTY3CukbhVtL9L
      tPzLaXE3uYl1YcDNcgIu5lvB6NvAjF1gwB1gqG8T+28R6xqCPqnSY45P/tfGON0llf9inBKDWpBonGVC
      DuuaqSkCpIWeP0kPzVMlx+ivnMdzoCEcRVYn1Pl70BCOwshT0ABFYb53Hn7fvJ03q5rJtuHkwZFErB+z
      LfUdJxuFvO2eGMkqb0TDuGQLh/zsF2SH3n2P2J8puDdT+2G3lwe3nNs8FKFZCXUJafFIt/csZD7kG0aZ
      VpRv40xcobtT6Q+qtdjTdYrybYmx+SnVabKA+bhCRC8TSussJfs9w1AU6nFZkGBEjCQrn6PjKMlQLPI5
      ZaBhTJT4n3S0BKIde9Ix2WQ4gEict03wt++i3rkbeNOOs98IvM9IxP4iwX1FIvYTCe4jErt/yPC+Ifz9
      QkL7hHD3B8H3BTltWLfJNrqdO4j0MePIHQUWR+/7SJ/6BXggAvcc7cfgGdrqU37ShFKE28kM9DH5XcxQ
      D1OvsSyykuzsOMhI3wEO3QHxMWYPl8fw3i1xOysO7aoYtaPiwG6K3J0U8V0U1bYv7EK7C5TaHb/Y7vBy
      u1PTM0m6+RfNecIcnzfDQJ7VAg1wFJWfXP+RDZjJxzC58ICbfCgTJHBj0BpSb62DrDfyDf15SI+BPvLz
      kB5zfPrljuMbDfSOt4+j/gg36uVfMny11KUi/uoQNdyUKU3fZNUEHec+rUWWbOtql6wO2y2xFvRo197u
      k6On0WliA4SdRfacFceZpE3GsTuKUBz1OaPvizjgSPpzYzcjTiTXMRiJvuwTcQxF+nFIi3yby2Y4Llrv
      gSOqPZnoM9guHHDrq9A5yo7QK4bisJbloJahaAfZiL9RSEsViNveGuw7y3W4kchVJVhHcvahRvag5h79
      h5/6x9rRGtnNups3Zzyis0jH2q090YucSVITdJztyjZOz90iESuj526jkLcfNqXFY0WX23w4wnNaHLKY
      EFrgx2DNBuI7zoiIOQ4RnOMQ3NkIgc9GCPZshAjMRjB3j0d3jo/a/3Vg39eoHekHdqPn7kSP70JP3oEe
      2H2etfM8sut8f3dtDsSBsI2iXnp757Cu2cgu8uDdhUNu8vDdo4fs5AE8aPCi7PdVrXY8Os3lEmN4vBOB
      NeODzPcc/0ztyhica6yS48EINGPPuUa9kJTeVTA4x8hYLwmulGS8ewy+cXx8T5i6WZXB4cZud03RyJv5
      kau3JHastOGdZ2dyuJHxvA3Aw37iczcAD/uJZ9gBuOdnnshmk56VcyKXgaE+XiYGz+JyPqdnYfAcLvNz
      8kDUg23383vO+vee8my81ZgW6DkZz817CrMxioEHh9zEQuDBITfnGTpsQKOQC5rL9ub0Ik8+T2+n88lN
      cjv5Oh1rdTnbOLuX8Hy6WFB0JwhxJbdXLJ3kDOMqT5pMtvardJMcyhe1lrXJdrIjldaj2+egJBzrpa7K
      R9lBeMwFYXA5bAKirotqJUdhSX3+jhzHYIPm8wjzedB8EWG+CJrfR5jfB82/RJh/CZo/RJg/hMyXfPFl
      yPs73/t7yJv+5IvTnyHzas83r/ZBc8Q1r4LXvI4wr4PmTc43b/KgOeKaN8FrFhHXLELX/HO341ehCg67
      z2Pc5wPuqAs/H7ryuEsfuvaLKPvFgP19lP39gP2XKPsvA/YPUfYPYXtUsg+kelSiD6R5VJIPpHhUgg+k
      968x7l/D7t9i3L+F3Zcx7suw+/cYN9SD0ANt2W1ud0va5HW2bo6rZ8mxQjIgtt5xIi6irwDiNHW6U8+2
      y4zs71HA24046qw51CVZbdG4XTTp+ElNEA65qz1fXZm9u0ycX1w+rncif07kP5Lvo9c6AGjQm2TlOvl5
      HqHvDEiUTbZmuSWHGLP1SodcFdX4JVu4AYsiP9+Jx+TnL7wQJ3zIfxnnv0T83zdbllhylvHiw6/ccuii
      QS+9HCIGJAqtHFocYuSWQ8SAReGUQwgf8l/G+S8RP60cWpxlTNZNrdsnwioEB7N9Ty/JerVWP6B+3TcU
      pU361qZ+f3H8tM1bQdUDCi+OLJmMK+8oz9aVRYbRIH0rz4jY2j212kQhFgOfBu3HJOfZDdq2lxW/tLks
      ZI4scagEiMUodSYHGLlpgqdHRDmBeCQCs6xAvBWhqwCf9B5ev5IOR4Rp3B4lH3LLjv7r8/gnVBgPReg+
      Sp6quiQ830B4K0KZJ/JLjGJug5CTXtBt0HCK8jzZVEm6Gb1/l4E4HtWEU1ajWxDgIpUpEwJcdUY6ntjl
      AKNIn+k6BTmux0yWnLTI/842evFRUyXjD3XHDV4UdXxIla8zWWUUclw+/txGjAcibPOs2CT7hu4+kY41
      F+1iPco22h7oOptsl6yr3Ur+hV5gPdqx19lWP7RWN7CeddGjc8o5gAMaLJ5qCqoy40XpYMctIkuNGCw1
      zeu+W4CdpDLHKpljGS0GaHCiHJo1896yyN66yrJDsqs2srpR63HVBdSUTcgw3oiQV918nZAdKOpZqzBt
      27ebRDxVh0LPdY1fTQCgtlftzifLq1rsqZKtuwD1p3SzIf2CsMmOqj6kp1FP+Ta1jl3+N1XXYYavTFK1
      XdBhJauNUjSkcgKwtnmzSV6qevx+QyZjmdbV/pWs6iHLtZFdI85vtTjLmP3cy3wnqFrAcmzzRsgbjvwj
      Lc42qrdBd1XZPFa7jHALeWTImohdWhR8d8tbER7T5imrPxCcHWFZZJLUafmYkRPUBm2nUDuZ6YaDbHVQ
      11tnRdrkz1nxqnoGpHIJ0Jb9X+m6WuUEYQtYjmK9Y90zFmcbMyGS5iktzcIwp6hBARKDml0OaVl3eVHo
      JTCyk0UaBkBswCx7CqTz+FCBE6PM5S2XvOSb8Ru/u5xtrDbtGcuM8uGxoJmaexbnGWXlm6xS2a25YF8y
      pADjqKJJriJ92HN3PbN37e3OD4N6sIjsJPN4NAK1/vNY1CyydZ01UQFMhRenEE/5Vh0ozUwjj0ciRAYI
      +HeHIqZxxxReHG5/02NBM6e+OHGe8XD+K/taLdYxy1utfEfyacK2yMRm1ZAm5xnVBEL6C1HXQrDrkuO6
      BFyMXDA5z6jSlChTCOhhdFxd1POSb8Aj45k4JcQvHZUsM6V+IVl1O6vVc14dhOx1ygzbV0L2OAgRBl12
      5FLPc7DGMx5rmffVCy3XWsBy1GrczxtvuKjv7doc/R2q2GRtc7Y5rDOZNGuSs6cwmxpA7YuUqz3hjl/k
      fzPS1sBsX9fSkoUmBxiP6a3/QfZaNGTnXS5wtWKdNg2t1B8R26MnTsnXZWKOr2GPUDzWM4tGjofWjKu1
      Uc/LEQKmH/Xlz0TPEJcppdK3QddJb817CHZdclyXgIvemlucZ6S2lifGM5Fz9Mi4pp/sLP2J5imjhwv3
      bq02kZx6AG3ZD9xJgQM+I3DgDhwO+KjhhTx9++LN31bqDX0h1H6De3UkVbHVj8RGOxG+j7C+yJPJ4vY8
      +ThbJoulEoyVAyjgnd0up5+nc7K04wDj3cf/nl4tycIWM3yrlR6qqBnOcvTKSZvybYe1uEhWGVXXYYCv
      2b5nCTsONF4ybJe2ST2+Vn9NCHssu5xp1Oe3kfPCpHwbOS8sDPCR88LmQOMlw2bmxVMq/3ehtwB8PX//
      7kNS7Qk5AtIhu8jGtzcwbdjVspxKr9FZF2pcmJVq4cLoGhPj+wgbdfNfXakXzK+ni6v57H45u7sd64dp
      x86rOzehurP/8Os9V3skIevd3c10ckt3thxgnN4+fJ3OJ8vpNVnao4C327xg9r/T6+Vs/L4HGI9HYKay
      RQP22eQD03wiISutRd2gLerpk9uHmxuyTkGAi9Y6b7DWuf/gajll310mDLjv5d+Xk4839JJ1IkNW5kU7
      PBBhMf3nw/T2appMbr+R9SYMupdM7RIxLn89Z6bEiYSsnAoBqQWW3+4ZLgkBrofb2Z/T+YJdpzg8FGF5
      xfrxHQcaP11yL/eEAt4/Z4sZ/z6waMf+sPwiweU3Wal9uusaaVIASIDF+GP6bXbNs2vU8R6a6r49kOmP
      8WvffdK2fpwsZlfJ1d2tTK6JrD9IqeHBtvtqOl/OPs2uZCt9f3czu5pNSXYAd/zzm+R6tlgm93fUK3dQ
      23v9ZZ/W6U5QhEcGNiWEpXEu5xhnc9ne3c2/0W8OB3W9i/ubybfl9K8lzXnCPF+XuERdR2E20kZWAOp4
      FxPeLWWBASc541045B6/tTfE+ubDqsjXjIQ4cp6ReNahTWE2RpIaJGolJ2YP+s7F7DPVJhHPw6iGjpDt
      ml4xruoEua57FSFrCCc2uJxnZN2EJocbqeXFZQNmWplxUNfLuFlOEOKi/3T0Tuk/ov5o7D6ZXs/uJ/Pl
      N2qFbnKO8a/l9PZ6eq16T8nDYvKZ5vVo287ZSXGD7qTofrLgKp2+y2yxeJAEs/31adt+O10urib302Rx
      /8fkimK2Sdw640pnjvNuOZMdyOknku8I2a675ZfpnJrtJ8h23f9xtRi/91VPQBbq7d1ToI12Y58g3/Ub
      1fMb4OD8uN/g33bJbwwAPOynJ+JloFXQn6uJnT91raTGnGS9jQ/6WSnkK4bjMFLKM0BRWNePXDHnGr2r
      UmPXb+SsO1GQ7Z8Pkxue8Ug61vndX9/0gLtNWd0WLoiPPFAJFKu9Grq+5RwjueME9Zp4XSasv8TqLCE9
      JV7vGOsbR1SGoXqQXQUGaj/OgBQZjc65I/05PtKfx4z05+GR/jxipD8PjvTnzJH+HB3pm59wksFkA2Z6
      Ihio503uF4tEDiQmXxdErUECVnJdNEdmPObsGY95YMZjzp3xmOMzHg8L2dPVXWeKsKdsm9rTnuJR3/cN
      yeTm892c6mkpzLbg6RaQb7mczz4+LKd05ZGErA9/0X0PfwEm3YpzdEcQcspeAd0nIcg1v6Gr5jewidyv
      tkDESbxnTQ4x0u5XAwN8rA6eTYasC74WuluoY+8ThLiS6e1y/o1lbFHAS6/4DQzwEU7OMhnYxCvhRxBx
      ckp4xyFGRglvMdD3590ftIVFJgcYidPnRwYw/Tmh116SAUycPIDTn5H2VrqLNNF7wOyy8S9JWJDt0gd8
      J3v6kwaA7c3ZOvn8qXuROd2MXjDoYLBvsyo4PonBvm1WZLvuCPXXZvyxyyFHKNLuUPBDSDjkFj9qvlvC
      IXdTxabP0QBHeayrwz6Rf87Hn0SJ8aEIlJ0bYDpk15tLHerxu7AFFHAcdQXJvs7U65KcICYPR2CWULRs
      qqW/atcEplSzIXOzfuKrJYy7I5LZwAN+PXKO+wmmw4skb4ZGnaW5rjaZepOvSGu1Hw31JsY0XjyR7/aF
      Pmw2+Zmsq6re5GXaUHMesWDRImtwxBKOxqwNQQcWKaJGBAzhKI/MeguWhGMxamCPD0cQb/FrxNCv0XuD
      MH9Jy6JmkaSqplY517wyI1iOQKSqjEkrQ4DF0Nsf6l3ZeCF6PhyBX656PhxBFQl518ZlDKgKxhVJ9uOQ
      FhHhOoMVJd2q/+p2/UpLcgyQhyK0b33TzS0HGWXCHcPStQZsu6nDKpOxTKv8sTzo+l1X9ASfQyLWtgVm
      aVvU8kY01sEWWnV9Dk2WvNxOPlGcBmb52kaTNpw8MYCJWt4NCrCxuh/BPkf7YZk9koWSgUyynlZb9Sa7
      VHynO00asJNvchODfIcVXXZYASbVzdLln+w7kYiVldtgr0/1nMwbSe0aTNWjjsFI5PoEl9ixdD+qzF4o
      6iNjmZ5S8aRSTvczkv37y1+Snzu132/64fwiEeLlkGzqdNu8+40QarwUvJZuHORy/OsIC61rYE4CoGP/
      UyMuL6NtJglWHx5wkwe8mMKKs/+evVLb7xNjm3QPTVfLh1KlVZ0JkVHaHcQARNE7d1HvPxcNeqlzLyA/
      FIGWn7AgHINe2jHFQBw9nxIVRhvGRIlPOHT25zjKILbKJgb6muMN2Nf+guGHNEA8Ritrg7azzX9Gqlig
      5VS7rVW6e6R7R+RbGeStCF1O0zq+PQS5dCeWejwAgkN+VmfYY1EzfTNAVADFyMvnd1ExHAEYQ5BO3/BA
      yGnvwEpX2zwUgTYY6SHI1e79R9e1HGQk39YWBxpJg5AeglyMqswhEWtMliO7YyJfUAWbX2ugKjtuOy8m
      0m03dUUJ5LK2uZ0Pi7/JQ55AxDdJynFG8yrUk3ohR7HJS948qXZmnSXbqk6+l9VLmaSleMlq0qZlBKV5
      He1TpL8vPvyapM8/L057QRJGSqgCiUPd6ReEETepKrQ5xCj7QXFXbAoCMdSehVExjgIkRtsBI3VXIHrI
      Th6nBiTBWJvqQDjnCxUgMY5l+AMrwIkesP8WZcfur6iSBJSizcWH/9/aGfS4iYNh+L7/ZG8dZkfdXld7
      qVRppaTqFRFwEpQEKCZppr9+bScBPvsz4f2Y22jgeRwc7IANr99evggG4n0wdOKDAz44OG2g2c4N2phe
      aK6PQJzLRaThNodxPrsSJ66zFGfTWqtXXOcwz2c+bwfX3APiXHjNDRjng2uupzgbXnMDRn1u9A6suAfD
      mOBqGyjGhlZaDzEuuMoGarCVSbYgW5CnPbssW49BGS+YIudzjBFLfvMwxocl43jY2JdLUxoZlPHCNZlH
      a7JYdEYVT86oQl4PxVQ9FMK0ypDkrFhapc8xRkmLKqZaVLEorTLGx0sQ1nIkrbLfDqdVhiRnRVtHMdU6
      0LRKAjEutM8qYn1WIU+rZGHGDadVhuSUVfiho2mV/R6StEoWZt3fhdrvESOcVhmSnFXSIUR6ASStkkCM
      S5hWGeO5ErC0Sp9jjWhaJYMyXlFaJU979iVplVFBrAworZJBqVecK8nC1L0gVzKCe35ZriSDUi+aKzlm
      eBPy/pfPeUZZriSD+l44V9LDAh+Ya0WpmA16x5RBPa8kbSIAJ5zwFx9Pmwg3z38VkGNDM5o24XOBEXzZ
      llIxm6BK2ZQFbxtcmVzKwmMT8ArqCAk8gm4ozJW0/4ZzJQnku/BcSZ8LjKJGyOdK+lvQ8yWeKxlsxc6Z
      aK7kbaOgsTC5kuTf+KFHW4okV9LnPKMgV9LnPKM4V5KnqV2SK+lzceNaqvSuXeS5kjxN7bJcyZCMW79K
      pV89J5orSSDqgnMlCURdWK7kQHAWtHlzuZKj/2MNm8mVfPz7M+r5zDgkB/eZP7ZRcuPXaltLzIzieTl4
      hYaGyVIWHsnTo1h2BE8/fVUWS4/grnhezrIjuRmYUmSZnxH8qV9UW1OZn7GdBLU1kfk57CP6/JFPLPmM
      waeCMz8pxdnQzM+Q9KxLMz8nJVxZWOanz3lG+KKWu6KVXc7GrmVFF7KRq1jZnUvsvmVB1z7Vq4s79Im+
      XDJYEBkpWElHYVbxUZjVklGY1fQozGrBKMxqchRmJRyFWUVHYaSZnxw7YcYrgc38vG8UZH6GJGOF+6JV
      ZDRqJR6NWk2MRq2ko1Gr+GgUnvlJKWpDMj8f+4cGLPOTUjHbWqZbcz408zMkOev8kM4xw5jQzM8A5JxA
      5ieBONfqG65afeNN8HV1JPOTbALbLJ/5SbZg7ZXN/CQbuo0WCQ3HGEWXjLEU0XDbWq7l2h860sKkiJJ/
      YymiDMp48Z8SNkW03wCkiI4Z3iRrM2GKKNkkaTNBiijZImgzforoaAOUIupzjBGcLAlTRPv/AimiY4Yx
      Sb4Dvv4Fdc/Wu6SfCvqoVok7Pg/lvfasEXrvKO8VOj1fbSeG8It+go19Wv4UpJ56CjLYmIIPq0UETBnw
      M4U6+kyhXvLcnp5+bq+TPWPYxZ4xvMif371MPb97Ec5dXaJzVxfp3NUlNnd1+Kduy2pn9jY3M+ufbff9
      1+y+jmOnzd9UtURu8JH/v0ZVdrPKdF2tO7v3v1mXzS4gwsdK+JEdz/PfAubYaTNSNzw++I/qoo7uPbmq
      Lma/Akcp32b+lOh6bPCdir/SzbHOD2lh6tu+mqhmJy9w7Nj8dt+a6ZPIzvNDCfVtoUr0d8PDBl9zyPVL
      kpadarOurCudZnmumi4DXl2ccgQl2dfidvNPNUoFtmajUlXl7XuDxThGcOr/7M5F+8KyKtyXgdgD2Hc3
      WatVulcZcH6EJLX+7Y6oUO6IECkBR87TpqsPqkrVtXkxZ6ZpS7OtIRrz5sdSVZ37jvEAkBmqWLnmhLJn
      rLK7y4obG+KldOneveZu32w3nby0KE8TK6/U+qzaD6lNVhUrtzXno6wYS8astgHJrJaMWc/VgnP5DvPu
      RN5KknTS+2GtJEFaSbK4lSQzWknyMa0kmdtKko9rJQnSShJxK0kmWkkibiXJRCtJlrSShGkltbn2eE/z
      LN+r21UZ9JPK0jF7q5RMbMCIU6tOpDRc3JiesqZBTvYIH5TgLh0F1dBzvBGI5PSwwGcvyV0OMO4co7xX
      cOQ9xxtPSOBeABLne7r6iayVMUIGj41/s/3cwTQ0l1u0OW+3yt5Dmgtae+E9u9k+N41Klawi1PKrCLXD
      SkC3LELg94Vjqdn8mdk4BPBamEF5b3ObzE87U33a1N5JUkIg4ctykUlt9ktSxIONmX8rmfW3okY4J4VA
      xPU7ffmU/JXusm6v2jeX2ARIGZqz27wjmflBctbKfIdJa274ZGqCc36zLbE7Cf0E5/w6z7pOXukEZ/0/
      W6n6Tg5WnZSiUWOfY4ySUWMWHrn32Yt40ImFidsGIy2wczjx2zzpBX4OH/nNv5VqoJU+xoxnOqr5axH0
      AONIm66FPRairnODSM4NobfA9fd9d8oDF0L33QlfVhpYqqYHqEOnum47hRxIzxATcKl429un0+p8PGIK
      h1DP/BUBbnsTuqmR88Hs7dPod/pAWI+5VxOoDEVt5/kLTd13Jzxwb3Xb26fd3cD2XOWYpseob19uoc9j
      96eGGmozdnfCX+yMCiBw+xMDkhF8333gO/sVu3vs+et9jJnBdHn8KOJzmwxKvZK5TZ+LG9dS5TruBBob
      g468r2lmr5zL2T3qQFDLsUMMx47Qm7yuNMC7/YkhN7e2iMHtTw3t0ebXFsDyQ5QKbEDvPhCBpXUzo6Do
      BvmuArPQb9hclJjrLfNvQNIzxKSuXXo4A5obQBzmt0Pvle7ADzTGiK8sGkBj9qZ0ta0R3Ozu8ftyY1MS
      q3foY4ww4rMN9KyzHXIm9wwxVdnJLpBQ6a7N7CJugNBHqVenZfaWHkuN9BsjyrPlwLVlDxBHnevGzjab
      MwT5DsZY6KtqN7aE+u4Y8TV5CWjM3pS+D/eKvskQ5tz3AWSB+EESqwYblQ5alYZ/2XTwy1Y37VYwGedz
      rHHRNNwzD1uiZAIugrP+RVNhzzxsicgkmIexPmT6y8NYHzjxFZIja5Mpneab/PGcyWypDwbOrn1N+qdX
      3OiKBuWMwS8FHD8nkO8S1UDk6O3d270YqF1wMOd+1IrIPYIH91UYUn6NZpTft+wUEppPIM5l265ruugy
      ExMKrpzmpXmxK1E0CV7AwE6aXxeYX1nzq1v3z06/Cip8THP22+ocNsUbdw/stBla1C0qeFKGPmXHI7rw
      2nMTW+r8lXYIxLm6GvrpC8DACU+KXaNrB9y36Bxc/8jnRsa3T19+vLqnFN340a2H0e455Nn2CQctKS3K
      nb2Fc/OR2XFXt2W3PyHl8Aa+lItqy+079ERoBPf8TWsX5nBzl1qnWE5bVOCV4Sa3u6vrhTRmpyjjtYXa
      Pqi7wt4BpV47MpSUadkgP0IeFxhvvx6muL26gtIxGnhvT86oa6cqXQLDVxE88Jsy4QW7GDTwHuv6oM0t
      9EGlhbmftnfpoJ4xBKXcbv6BLptif/7xP+zmWSIhogQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
