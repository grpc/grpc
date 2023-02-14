

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
    :commit => "6195bf8242156c9a2fa75702eee058f91b86a88b",
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
      H4sICAAAAAAC/2JvcmluZ3NzbF9wcmVmaXhfc3ltYm9scy5oAKydXXPbuJZo3+dXuO683Kk6NRM7nbT7
      vim20tG0Y/tIck9nXliURNk8oUiFoOy4f/0FQErEx94g94arTs10LK21KQDEF0Hgv/7r7DErszptss3Z
      6vX0j2RV1Xn5KESR7Otsm/9MnrJ0k9X/KZ7OqvLsk/50sbg5W1e7Xd78v7OP5799WG0vL365OP/wcf1b
      erFNf/3w67uLLMvefbjc/na+uvyYXl6u/u3f/uu/zq6q/WudPz41Z/93/R9nF+/OL/9x9ntVPRbZ2axc
      /6f8ivrWfVbvciFyGa+pzg4i+4eMtn/9x9mu2uRb+f/TcvNfVX22yUVT56tDk501T7k4E9W2eUnr7Gwr
      P0zLV+XaH+p9JbKzl7yRP6DW/786NGfbLDuTyFNWZ+rX12kpE+IfZ/u6es43Mkmap7SR/yc7S1fVc6ZM
      69O1l1WTrzN1FW3cfX+9x4/2+yytz/LyLC0KReaZOP665Zfp2eLu8/J/JvPp2Wxxdj+/+3N2Pb0++z+T
      hfz3/zmb3F7rL00ell/u5mfXs8XVzWT2dXE2ubk5k9R8crucTRfK9T+z5Zez+fT3yVwid5KSvt59e3Xz
      cD27/V2Ds6/3NzMZpRec3X1Wjq/T+dUX+ZfJp9nNbPlNh/88W95OF4v/lI6z27uz6Z/T2+XZ4ovyGFf2
      aXp2M5t8upmefZb/mtx+U7rF/fRqNrn5h7zu+fRq+Q+pOP6X/NLV3e1i+s8HqZPfObuefJ38ri5E08d/
      6h/2ZbJc3Mm4c/nzFg83S/UzPs/vvp7d3C3UlZ89LKYyxmQ5UbRMQ3nJi39IbiovcK6ueyL/d7Wc3d0q
      nwRk6OV8oq7jdvr7zez36e3VVLF3GljezeV3HxYd84+zyXy2UEHvHpaKvlNOXYTvbm+n+jtt6qv0kNei
      r2I6lwnxdaLFn+3c+E9d/j/dzaVT3j7J5Po6uZ9PP8/+OtunosnEWfNSncmiVzb5Ns9qIQuPLPxVmclM
      aFQRk4V6J9QflChv1N2qSly1Pdul67o6y37u01IXQvm/vBFnaf142EmfOFtlEs50IHn3/ue//ftG3tll
      Bl7O/03/cbb6D/CjZCZ/+rz9QtBhfvEsPfv3fz9L1P+RdcCJmt0l20TWMvA19H9s//CPHvgPyyGyhmrp
      kN5zvbxZJOsil0mV7DJZPWzG6nzSsTJ0oEdk9XNWc3QW6VhVXZisDtutLG4cN8DbEZ7Pkwt+yvo0YGdq
      UR87pX3as8ekRDgdHmWZbvJdplo2mtcgPeuTbOGKjCm2Yc/NSgTk18fkWTjHVF2Rl3mTp8XxlySbQ1fz
      UgPhqj7udD5PiirdJMqgejeyKzY2EMT25rv76a36QF0Dpcp0ud54P/2a1FkXbyG7C6pNHGmFWMC8yqso
      u8PbEV5q2Ypy9R4MuSMuHxT0MdQfr2b3sueSbDKxrvM9pUjCNGhX9UN6kPV8mW8YehNH/SvVW+G5FYp6
      1/le9u8jrrwXoDE2+WMmmogYvQCNwXYHnN9/JmW6y5jijg7a2Vfdwqh7l/5MZJUteOXdMeBR8jI2Sm9A
      o0RkQTD99/U2IgM6OmCvmmpdFUlEhJMBjVJv1zHpc8RR/3NaHLhyzeLmqHITKjO5SFLZrjHMHYlZV0W1
      /t7Vdzy7aQCjiEb2CNN6w81Ui3ci3H29T9LNJllXu32d6akYYndwQAPE29ZZBnxTkCNiIiCmLB/v6Oln
      kbD1TX4I4kEi5htWgHyD+LjJAqXK8i9VDt4l66dU1uLrrG5IZh8H/edx/vMhv/7EypG0eGQEAj1IxHaY
      ejVhhTnCsDv72dRpXJJ5DjiSaH8mJ0CH+t71Uybrx32dP6tZ9u/ZK9XuCYAYbX9V/rbHujrsyRFsHPAX
      WVobqSfIEVwBFsPNJ2YkT4PF21WbjBdCkZi10uMq5rV3sO/OynRVZEm1FnvVKO4LOdCnhoAcaCSRP5ZZ
      VwuoqQsJ7PaCGRKWobGbQqj8K8uM3N3EJH6sbXEQT8dbl/zDbBqwy/ad7JSMb9KNuEq5fJuvZS1Atbo8
      FkHdLzy3IkNW3s3s8kiEfVqnO5Zbk5i1rXEZNbaDg/72RhCNej5D1xs0YtdVumCpWxTxHpvqpMhFw9Jb
      BjiK/FN6KORwMRXiRdYZK04gTzIyVnIQWb1Jm/RNgp5scPTsZ8IN1aGot8xeZJO+yX4y5SceixDZUoMS
      OFZebqtknRbFKl1/58SxBHAMeaMW1WNUFEcBx1GTUPru5d5AlgCPoadaWFMSmASJJbMuPpYrQWIxemtH
      DjYye2oGCnt/HHL1uPnp0GyqF1aS2AY4in7WkT5RZ4Y8GrZ3PRtZnuUQhJ32vgWORnzaCKCItxCylpHf
      WX9vb1FWZvsWOJosvvn2NaoWcRTBOJts3zxFBNF8MAI32w3c9+unld03imqdsu5BUOLHKjM56mh2+2S+
      IE9OmCxkfqELX3xPne2q54w7+WDTvl19kKTrtcxpqtpAg97ksao2EXLNhyPUWZk9Vk3OGPwgGiReW01t
      D0XBitPjmH+VPOX0zpLJYuZKjnPXvEzu2LCZn82mYCBGbEYDHiSiHozo7BL537xgtiIQR39xxY7R4gG/
      6qtH+Fs84O8qmYgQJwMShX1TBO4ItTg341lbFPGWh92K+LjMRhGviC+RYkyJFHElUgyVSBFXIsVQiRTR
      JVKMKJFdr5JXfo4w5G7edYsnk31VMZoZm0cisObyRGAur/3sOHkjeOoTjviPfV/23BhsAaOds9PoPJBG
      8rND/cypdU5o0MuaNnB5JEK2fmINkCwYcbPmaHsSsYr8MS0eeRfcsWEzP7lNARIj7hkHoEDivMVddT7y
      rkrksLV6SQ7l97J6UQ+M993MDieTcBkWOzLaGL/ICtXB5LQ8rgGO0j51Z+k7NODl5v9gvuvPI6c/MA8S
      UU8bp+WG81TdEyAx2kfjzFrAxBF/1PMUMeJ5ivGdmIJlGZAoh7pWX1J9H24YW4HFkcVw15URXhRDAMeI
      fgIlxj2BEm/6BEoQn0CZ3+9uuX3aPImYuKYHiVgJXcvKOlBPDPPS1pXAsbK0Ll71c7JuTQCnmQUsSDTe
      0zwRepqnPtymhcjUeo26axKzTdK90KpbFE7AISd8JY91lkosIi1tAxwl6nmfGH7eJ+Kf94kxz/tE7PM+
      Mfy8T7zF8z4x7nnf8Wsik23mtk4f1Wum3FiWBIkV+2xRjHu2KJjPFgX6bFF/IuKKl8kPR0jS+jE2inLA
      kUr19KtNxaj+L+QZiiiSdPOsFi+JbBMd1pHBsfXyuDoT+6oUrEJhCZAYvCfPIvTkWX2oNiU4NJlaWpGV
      ghvCtyDR+mWpnIX3qAWJJr6feqIRNxagweN1L4rGxnM0SLxu0wpOjBaFvT8O+Toiewwc9UesdhAjVjuI
      qNUOYmC1Q/v5uqo3/RtKES0OosLiNmoUWpWyhyme0osPH5Nqa463BO8ShqzY1XT9d9mnlvXXYZfxorsW
      ONqxCehXpjLrd1CExYxd1SJGrmoxv5erl4vKRlanMdF6SziaqnA2Txl3TU1AhcSF1nazO7y4DY+el4/q
      5ZSqliOYnd7BSHBDAyokbt3s1U2+zYuMF80UIDGaOl9HT0P5Fjhat7xJvTAY0Vz4Fiwau3QGS6M9Jx4z
      VoVNaFTVyWzbefVqGbdDDorGxozppuC2cPQmbQ4i9teeJGNi8RoJ1xGM1K/0i4tmeUZGFG8STwSjHdTk
      j6x/IkIdFUgcWWdvnlh6TYasccXcVuBxsjX/+hWLm2uRcsUSDXqjk8Z0IJHqA68Z0iDs5E/mh2bxu17o
      G3QMYFMwKmttrhhcm3tQEwtbqrelAJu8h+/b0fcf9IdoNj1kTyaL2/O4EFoxGEf1pyLjKAUcZ76YxCWY
      JRgRg51svmVMNG7i+RY4WsRrjA4+6GennOsYjtQ+SuamHWwajvoW8fBIaujXbkzZvCZPOX2mH5TYsaZX
      X5I/pt8W6h16it7kECP19VsLRJxPqUg2h33RZVVVbvNH4tKdIRcSeZfW4ikt1MRO/dp9W7DigiYkKvEV
      B5NDjPTmy0Ftb7dBWqI26T09vuwf11LiDKjguMaT4XW6V8NDTkjfAkejFmmTw4zVLlm9NrQJDJ+G7e37
      2+TNhQA84OdNrSGKQBz2QyHcEoi2zyLSTMEDbrMNEFGBLNNQ1HYuOi5e6whEepvpyJHKwHW0Y3F2zBZH
      /ZzVJgAe9LPeIccceCRaC2qTuHWn9teuqYsDYQMeJeaBUciDR+ymeIp8m+l1ctSu2ZArFHmX8SPtsrCZ
      OBcM4Lg/MnOCeaI6cpGVm6PA4/CrlJ6G7bloH9Vx+zAmD0cgdiYNDPbpVem8qqNDg96YXoWjQOPE1OFi
      qA4Xb1Q7idG1U//0hxsnVEJFRA0kgjWQiKuBxFANJORYotgkK/VWXvlYZGpkzAoEeOCITcXv1R/ZsDnZ
      VnVEZgMaOB59wGiTtpX+Ijz0/nvEHpHB/SEj9oYM7gupNihM9+1Ug3qoLwtsQ9lhPuTwI7H2ggzsA6k+
      UrNU3Ssoh9W/snUjVAmSvXDag44BlRO3UF9SG6V3u+qTIrnwgDspqsgA2gBF0aP07qGCaqKLhh7Hd0CR
      mtd9xk4rAx5wM9PKNdhR2pU8TzkpcU6Q61Lrngq90J258yeicOKohVzttpEkd485vpi9Sgf2KaVfJXB9
      MfuQDuxBytsPFNsLlL0PaGAPUMYGH+C+HutD0zzV1eHxSe/tW2S0JzEAbvs3stg+qvPlknWd6an/tFA9
      FVJPHZU4sSp94IwcNn0n/QiTc4yy28B4Hc/AbF87t3taYb9ufvaLqtXYkhJkyAVF1rPKbSeGlgMAjvrV
      Oz2qT0Cu+jGHE2n9xPsJBucYI/fSHd5H98320CXsnxu9d+6IfXOzupY9dubhMx7suH/uq1ovXlJt9E7e
      /rW87UkBQIMdhfoUxX96cjo0Uy3r0gcgUHw+7dqbd+aL4rQy79OA3XwArLpFghzBM0BReA11eNdf/am6
      sfUKxUr2Seuc1mbDBiQK+3krbACiGK9Enbasouc4aAGisZ9iDT294u3EjO3C3D/tiR23hk1YVO7TsTFP
      xfrvdJ2c7mSFdmUZMxyowuK6q9mYMT0NEK9776nOfhxkkyUbMOLeQagEjBXzsgWigOK8yfNF0nPFR72l
      DH2HSJPzjEm3UIcoPGK+j7m2y0EBb/viwuqVfngTgKN+Rg7i71Qwd2FHd2CP2319aOd14/NajouqHVPe
      woC729iDvhjEpwP2/qgadohegcfpj1xmRjkJwBjPGbHbbnKYkXpMkk361uN+H4znJgDu+72RITWCJwBi
      qOEI2asgwEV/koeuwjA+SP768O63ZLG8m0/1msp885MZAjCBUVlrPsJrPbqt/nciEYe9GqDR1Qbsu7fk
      u2UL3CfyH7l4yuiujvON7H1MBs4s0B8/k9sVifie0yA0KTLyPWbBvpu998nAOQfRZxyMON8g+myDEeca
      cM40gM8zYJ5lgJ5joFckHYcx9I0yATzgZ3YZXR6JwL2tLRhzH4oiNokcBxJJ78HQyO6V0BNcesgsWPFA
      ExJVDU/S5lBn/SCPFRPwQBHLjZq14/URbRqws45zsknAarzeQPYabNhMXuIHCvwY/H07hk4o0Vt+r/KK
      6lQMYGLt/BE64+T0mVBzCuU6Y4mPMOCmd0lqqE8isrW6a/rd7PXkFa8TFXJBkdvZY2uXAnpIQALFaud3
      WCNPC0bd6tVWxr1v05idM7bqyZBVz63z1RqH/KwxMjqPJJ7SWs1i8aY7bBq1M/Z69mnIzqv98HoPaOyS
      Tf6Y0bvAuGlcVNU9ZxWggGtcZNYdgXiAiNydVx7Du64YK+LTxywR32krlgEc8LMfzvo0bD+U+Q/6JGlP
      glZj54zTQyBGCEgzFI9Tgn2DHyVis+rBc7pizugKn88VcTZX8Fwu40P6IkEPBt2cNgcdN78wepcvYO/y
      hd5Xe4H6ai+yysrYHUqbtu3q3Y3Y56CYw4/UjaSo8g6zfXnJfBvXAj2nsXkxUWqQnlWO9ak6hTgekWxk
      7UPytIjnUXLW9IXLeua2h0hUtpDvAppttYnMXlATIWCyo6q+yGG/Ic4Z9ZRtK/JVndav5Ow3Oceojibs
      H7dRR04ADvjbtVTtcjlB1lu0bd+lj/n6NJ9y2giwIZUXVOLGajcjUAtl2iUytCAu7drVNtPyC2qRD3X6
      wINtN/dcSfxMSeL7cd57ceVhZw/uSaXCp237PstIXST1fddAblfANkX23dfqjC09kbmvRMNbAhzQwPFk
      FX3+Xj/iOhZn+utPQy4v8nO+ydpLpLagHmy72019ZRk//epkW+SPTw31OVBQBMTUM2dF9pwV5Cg9Cnjb
      DhRPbLC2uSZWGrVXTzAPtETPrzQ+4NxRAO769SIrIzfV3LGgxQAVbhzhPqT/F/HtBkRhx+m2Bu7XR1Ii
      eLDrVkcYyMhF+4oRTW2zrlmtW87/ztoNYfIib3LaVAdswKJE5DYqcWO19VydUV8FsUnXyjnrEDvnMOKM
      w+D5hvpD6uOQEwS4ok50G3NGov7OC+eKX6ArPmfl0TmSR5wzFtHzFWPOVgyfq6g/hd5jIoeAJECsvhvM
      +yUOD0Qgr+/GTnDknt6In9wYdWrjwImNkac1Dp7UGH9K45gTGgVvxa/AVvzq8wzbc9XVPCv1ei0WMPPO
      cgye46g+pNdpCVSjcQ7TQ09ojDrNcOAkw4hTDIMnGMadXjh0cqH+vDvKnVW4LBhwc88QHDg/MP7MuTHn
      zenvtK/qqTq7PVKNHMQVQDG2Vb3O9CScnj0T6SMjDiABYtHXz6I74AjymlABrAlVf4vqFzdDPeKIFaID
      p9ypj/+1+X5+nrxU9fe0rg4lOT1c3o/AXt85cK5d9Jl2I86ziz7LbsQ5dtFn2I04v45zdh18bl3MmXXh
      8+piz6obPqdOf6M5kKXNwfewX5EcOPmNeeobeuJb/GlvY056iz/lbcwJb29wutuok93e4FS3USe6MU9z
      Q09yOx3DZm4VTH/HMaBB4vGyGz0x7vRhzFJmVILEUvuQqwH0Wr2Gvcn2VV7yUg0SgTGZ68qGTsLjn4IX
      OgGv/ayfFua0Ji4PRXjLc+44Z9wJ+rpcAa3LFbwVlAJbQRl/TtyYM+L0d56yjdEnpT9wRSVQLF75x0v+
      27x2TTlh7o1Olxt9slzUqXIDJ8q158AxRtLICDruZLoxp9K9zVluY89xMw62elIPg6krWCEejRCzklKM
      XUkpoldSihErKSPPFBs8T4x3lhh2jljkGWKD54dxzw7Dzw1jnhmGnhcWe1bY8DlhrDPCkPPBeGeDYeeC
      vc2ZYGPPA4s5Cyx8Dpigr1oV0KpVVhsNt8/klgVoVdSfGHvImRxuJG8a6sG2u6kafYgOd70VxNsR+Gez
      hc5lizyTbfA8tsiz2AbPYYs6g23g/LX4s9fGnLsWf+bamPPWIs5aC56zFnvG2vD5arGnnA2fcBZ9utmI
      k83UWpXkKSuKqtsBrlsVRQwDOuxIjHllcCb5JaUlgvq+Y1DL6EgKBViO54v3xyE8eerJYz0zS4m4uvk/
      ltJie/PyZsH78R5oO+kyyML6wR5oO9U5a8nqsN3KAskwA7jlfz5Pztkp6sO+myfFbNwU9mHXfRGTChfh
      VLhgSjFbRCpchFMhIg2CKcARwqaI34788s1FnhinYox1Ohjqo6ynAdDem19sONfpYKiPcp0A2ntlq381
      /3a/vEs+PXz+PJ3rQXB7aOT2UK7HxhjQDMVTexK/QbyTJhBvk2V7fWHsUCdDIIp6FaI8FAU7yFEQinHY
      8fWHXcC8P4gntlrBAbcY/4YJxAbMpM00YdqyL+bLe/n9u+X0aqnuG/mfn2c3U07eDqnGxSXld8AyKhqx
      DIQ0djy19nJ2/+VUR+z21DsfU2Bx1HrjJuMFaFnUfNgztYc95pR/2vCkisSsnELr06idVjQtEHNSC6BN
      YlZqJeGilldvQXk7+TplF2XEEIzCaJsxRSgOp03GFEgcTlsM0IideCPZIOYkHLbggYiT8KKsy+FG6s3u
      w4h7X+35qXCEMTftlrdBxKlXOMfcmKYAi0HYvswDfWfc7Td053ELB14uaLX/EfE93KKFlyrxlG/JOaMh
      30VtOXqod02uruQgLLmeLq7ms/vl8TD7sVYED/rHbwIBwkE3oeaCacM+XSRXXydXo33d923DerVOsnJd
      v44/FNPBHN92dX5xyVJapGNtaq7VIm3rJiPrOsT2ZOsV59IMzPExXJCnYudFFcgLoTdv1x9Q3lQCUN/b
      BeR4DdT2HsqXOt1TlT2F2ZJ9utmMX4oEwrabc53wVUZcI36Fi9vzZHL7jVI/9ojj+TRbJoul+n57bCTJ
      6MK4m9RUACxuftSvBTZceYfjfr46ZKU0Pz4a8B52tOOmUQEegzANBqBBb0xOCjgnv96zi6CFol7qFRsg
      6iQXD5N0rXd3N9PJLfk6T5jjm94+fJ3OJ8vpNT1JHRY3PxLLmI3i3pytDaUDNbtsFPcKfiqIUCo0VfLp
      lmvWsOP+zCxkn9FS9vv0Vsa7mf3v9Ho5k8PNdPMvkhngByLQmz/QMBCFfMtAgoEYxEzw8QE/tbgD/ECE
      fU1YBoQbBqJQby+AH45AXEY5oIHjcVs4Hw/6eeUKa+3sj5llCm31ZpMP3FSxUdRLTA0TRJ3UVLBI13q7
      nP6unjPt9jRnzyFGwqMjl0OM9DwyQMRJ7UIYHGLMecIc85Fzu+cQo2D+ZoH+ZlX1HGRV+vEXrrjDET+9
      K2KRjvX24eaGXphOFGQjZnrHQCZqdh8hx3X36b+nV0u1/xRhMbFPwlZy2hkcbCSm34mCbdQ07DHXd7Wc
      9pMXxCrShUNuamXpwiE3PbdcOmSn5pzNhszkXHTgkJtaBbqw476Xf19OPt1MuUkOCQZiEBPexwf81OQH
      eCxCRPoEU4adJoHU4KdDMAUor3YCqONdTP/5ML29mnImfB0WM3OtgHHJu8wlcoVtcWvTJt1saFYHDrnX
      RZaWxHoaEsAxqK0L2q4cPyCsOnE52EjZ7svlECMvNTdYGpKrFby27Sf+37F/+AlG3adjlXep+M4MYTng
      SEVWPo5/n9UnYSu1YkTbhe4D+nSOCQacyfizkSE2bE62+xi5xGE/tYeC9k36D94xhe9QY7J6TW5n10xv
      R+P22LtDjLo73G8lqVi/RTTlgSPKQenD8vMlJ0iHIl5qp8LgcCP3Rj+yjnn58ZxbXdso6iX2LEwQdVLT
      wCJdK/M5yBJ9DsJ6+IE88WA+5kCfbegPNvl2S9cpCrLRCw7yTITzIAR++sF65IE852A+3ECfaLAeYyDP
      LmIeWISfUuhPZfX2mJVZrQ8o2Ki9nugRfIcb6dv9lNzfPkKQi14ejxRko05JHyHIRS6RHQS5BOe6BHxd
      av9zluzcsT3czv6czhf8p1uQYCAGscLw8QE/NdMA3o2wvGI1EQaHGOkNhUVi1t1eb/SWNDz1CUf89FJi
      gIgz511rjl0juRT0HGKkNykWiVip1YLB4UZO8+Ljnv/zJbuasFncTC4GBolb6YXBRB3vn7PFLGKu2seD
      fmKCuHDQTU0Wj3bstGO/DcTxtP2PJkue35NkBucZm6RaUU7ccjDHlzfZLtlc5CTbEUJclHf2PRBzEqdX
      DA400jPY4EDjgXOBB/Dq1OEInCxpOcRIvr9NEHHmFxuWUnKIkXonGxxk5P1o7Bezfi7yW9VmFaz7pAMx
      J+c+aTnIyMoOJC/2KbGHeKIgm9qYl25TFGZL1s1PnlGRkPVQ8n5zy0FG2p6aLucYd6tul0TyMyKLxKwl
      X1sC3rb5kun9N+2ONjjHKHuzu7zJnzN6NWGjrvfQJFlFmzvuGMDEaO17zPE16eMF9aWJjgFMYvzR0ibj
      mrLdvtD7/VEzwSIN68PyiwSW35LZ7ee7pHshk2RHDUNRCGmL8EMRKDUyJoBi/DH9NrtmplLP4mZOyhxJ
      3MpKjRPaez9NFrOr5OruVg4JJrPbJa28wHTIPj41IDZkJqQICBvu2V2S7vf6mKS8yCgbqwOo7T2dCLRu
      6oJitUDHWWRpnWyLdPzRlA4G+doNPJlWA3bcamMSfTyx/grJbKOOl5qcfirKv+jhoj52hLj5KSpAYrTn
      dz8e0jotmyxjhXEcQCTicdsuZxs31fGMQoqvp2xbVm0pGvl1m1c7uJAe91qQ4yoIu5KcAMdR03LRqSe7
      vyRpUVAtirFNek0MYcmOyfim8du29wRg2ZMte9+Sl3lD9SjGN+3UJAQjjY4cbNyP7xg6mO9Tu7HI8jp+
      6Y4H+k5mne6gmFcdyjl+W2eI9c3UHf9dzjNSf7jza5+yn5vDjlSYO8T2qAwqSWW5JVxLQ275joxtUsVQ
      H8NU0lLI5Fxj80SuFk8Q4KJ08AwGMOkNn0gvhgAo5iVmhwUizo3sSNTVK0vbsYiZekNYIOKUg3CeU4GI
      syYcH+eBiJO0+btP+taK3iMxMNtHLOxeOVeNwCqvkn2a10TRifONjA6ggfk+Wt+iJQAL4bwFkwFMe7Jn
      71tUnbg6bKmqDvN9olp/z8iJ3lKu7SfR89M1HHarrCbfjwYG+tQdJdsQhrIjbStj4AOOefYVqUDIrzu8
      WjZAKggt4ViamtysHBnHRBzo7L1xDrVy9+t0atHxy0x7Lqgoz6kaDQEuziyPBbpOQbtdNeA4XnhX9YJc
      k+DU3QKuuQWx3hZerS3IdbYAamx1gsaOJpGA66DXrgKsW3UfriCcn2xBgEsmvT6ZkVoGPBhxq4HAnrCv
      KQgjbrYXdlJH6gKczRDk2QwBzGbov1FH0CcIcO3Jor1voc6MCHBmRHQTEsTei4HBvqzaqnH+oS452p72
      7SVhKYHJ+KbTPAS5hPRkwEqcGRHBmZH+U7HP1nla8NQdjLnJAyQH9b2c2RyBzuachmLdiUqkR+SowInx
      VB2KTSJHRJyUdmHQTS5yPYb4iA9WTA400guCwbnGNiflZzThCXN8Jb2PfWRsU5MJRsXeU7btoA44Jl1V
      S9iWZ+r82bM/d/bMSaJnOI1eGAOrF3BkRS5SQFlqb13iI5MTBLk4XW6bNKw3kz+mF58uPnwcbTsRkCX5
      nJeE6sfhQOOM0mmwMdD3sN9Q5lRd0HDeJp9uZrfX7fv45XNG6E36KOwl3VoOBxvz8jktclISgDRqZyZD
      HkgFyjyjjVm+q+VfSTb+MI6e8CzEbDkinofwEllPeBZa8nSEZxFNWlOvRjOW6ffp7dUnvQ6EoOohwCVI
      aXRiLNPXu9ulvmDKokeXg43EomBxsJGWnSaG+lQlIxrKi5qoAI+xrepkV20OxUFwoxgKOA6tMJgY6ksK
      NU+yYWo72rKnK5HkInmpaorVoGzbhmTZeDT5QjrE9oj1xaqkWDRgOVZ5SXO0gO2Qf8lJDg0ADuI2/i4H
      GPcp3bZPPdN6tWJdW8+5xk22pqkk4DqeCGs8joDrKDLWDzthrm+3z2kmCVgOvQ6QoNDf9w2Ure5NBjAR
      m5Mesl2ExR+39vvy7b+pdcYRsT20xtZrY9fVoVQV7Evyd1ZXKsEESefRll2WcVpt1AK2I3+mCPJnl6am
      8xGxPQdKbltvtcl/Z+VTWq6zTbLLi0I9/kx1JVfnO9nTb1715AFBP0Znx/9xSAtWB8UhbetPSprIb1s0
      8S707r9tXe1kR6ZsHqtdVr+SVBZpWR/XlKIiv23Tx7dWVV5kCak691jH3CT1dv3+w8XH7gvnH95/JOkh
      gRfjMH7j4p7wLMQ77ohYHtm20eqOFrAcpIcht+5zkFvVV5R1GrFH3EOuq8weU/XKFE12pFxbReq0toDn
      KIkXIwHXsa9eLmgSRXgW+h1jULBtm8paS83L8rQG7vqJBRwac8i/qUaTZlGEZSky2k2iv28bSCcnngDA
      cU6WnFuWXVqLJ9nakFZ02JjjE9+pPZoTY5uqDXGM2BGQJflxyMe/E+tynpHWCncEZLnQbSLd1XKQkSkM
      +1jdGFiAxyDe3x7rmfXUq6BeckdhtmRVqMXgG571SKP2asM1V0DJJ9czPYS4zlmyc8zGui8tFjFHiBHv
      7lAQdZKALLwOtA97bmKn4Ih4HvGjJmokAVkausYvd+KwomoOK8jCKhInzjMyqiu/ltrntK5EC9gOWrl0
      y6QsUtRf0iGWhza5787pl6VMHgqvvu8bqHdAD9kudb4krQtzREAPNYEtzjdSjs40GctEG4S4I5B9qloc
      1flLDqXai4TUHgK0befO0QRmY0i7zx2/7xsoCwZ7xPaI7LCpkjolPbE1KMym/s9jxnO2rGUmXqB3ZaxL
      ClxL+2fasNLibCO1Z1T7vaKa3COqgd4Q8UjZnvAsjKkOE/N8tHkpAcxLCfq8lIDmpWg9Erc3QuyJeL0Q
      Wg/E7X2oHgQ1DTrE8jRV4hxzSjD6MOjuzi1jiDvStbK6uhZnGQ+0CYGDOxtwoD1AOrhPkA60onBwy8Jz
      WhwyYtt7YiwTcRrLmcM6fWV7KNdNXpXJE6EGAmnILrJiS2vDfdTwPnxOvk6/dlu8jFZalG8jPRIxGN/0
      WFcvVJNiYFN7Xg/H15K+ldJF7xHfo16Yqp/JidZhtm+X7ShP+U6EbRFNTbS0hGcp1mlD1CgE8BCeEPeI
      5ynpP6uEfldZZCXVU5jvdV59+qSnQynTxCYDm5JVVRUcnQYRJ+kgUJ9ErNW6Ie8LjQqwGPmmfU7aEN4U
      xg1IlAM/gQ5ICpGGpBbku8Q+XWdUl4Z81+H8I9UkEdDTnUUlh3Tyo5/jh7sBBRinyBjmAvrtF+Q8lgjo
      if7tvgKI8/6C7H1/AXoYaaggwEW/Tw7Q/SH/yLgmBQGuS7LoErJEZ+rlcJ6qXie5XtCQ7SKefWggtofy
      Juvx+44hJ76QZUGuS6zTepOsn/JiQ/MZoO2U/5GP32WgJyALZeNpm3JslB3eTgDgaBshNUEwfv86ELbd
      lAUrx+/7hoR8F/WUbSP01bqv2zyxf24gtocyxDx+3zQsuq5aVqsR/Sarx8s8FPLmTbdv81MqKDNouAGI
      onpU8hJoPTKftc1qz640L0W3gvOVUp1AtGvfv1K7ZCZl22h15sKrMxd6pVlavhLHDjaHG5OsyHaE3dww
      Ho6gSmBsFNcBROKkDJwq9FGVAyJO7u8f/N1JvtsX+TqnD65wBxaJNvBxScR64GsPiJd8854g31WkoiF1
      Gi3M91V7NeNHXDEGwgNuVjH2DUNReAP7IdNQVF6hgRx+JNKo94SAHv4gAVWAcYqMYS4ywHVBTlRn1Hv6
      Y/RvD496uy9RRr0nBPQw0tAd9S6oy9ENBPQwrskd9XZ/JldgUN0VM+rFDECUsskL2bGvBbm5NFDbSxuj
      LLwxykItZD4utji1adkjrVOOObxI+mV6p5NNDAQpQnF4P8cX2DFIY7GFOxZbtDsoqdd5KJYTZLv2Wfa9
      vdQmJaWmBdpO8T3fU1Tq+46hGf/U6/h910B5etMThmU6X84+z64my+n93c3sajalnaSB8eEIhDsSpMN2
      wtM6BDf8XydX5G0CLAhwkRLYhAAX5ccajGMi7dHSE46Fsi/LCXAcc8omlD3hWGg7uhiI4bm7/Zz8Obl5
      IJ3oalOOTe9jkAla/rsg4iyqbl9PlvhEO/Z2vWGRE9pjGzN885vkerZYJvd35PN6IBY3EwqhR+JWSiHw
      UdP77X55l3x6+Px5OpffuLshJgWIB/2kS4dozJ4Wxfhj0wAU85JmzjwSs/KTOZTCei5aNq0885HG7JRe
      lAtiTnZxCJQEvVWLenzOTgnTMBhFNGmTr3Vuq351us0ig/pC7BpoO+RBrGf++rCc/kV+VAewiJk0BHJB
      xKk2uSFtAQnTITvtaSGMI/5DGXf9Bh+OwP8NpsCLITur32Qvg/rQEoJRN6PUmCjqPeiOVrJSP08wA1gO
      L9JiOVnOriILKiwZEYuT5YglHI1fiDHNqHjRvy9Yspdf5tPJ9ew6WR/qmvLYBMZxv966uzuckBvEdIQj
      lYddVufrmECdIhxnX6nJmDomTqfw4qxX6/OLS7XnTf26p+aLDWPurIxwd7Dv3q7Ux+dcu4Nj/ss4/+D1
      R9lR91Mq/5dcvKNqj5xvbHsiqn+vj7en9+QBgx+lqSPSxIIH3OqfhCcNuMKLs63q7/KGaNRh1/ljWdVZ
      sks3z8lLvs+qUn+qNj9UK/kpc8AcuX9t9KESOEbSBz3yioGJet7H9U4lcEpu+XoQc/LqNxsecLPKFKTA
      4vDuCxsecMf8hvB90X2J1bW1WMysx9zfs1ee+0hjdtmEjt8CDkAxL+XJhQv6TnXQyGvbD2uPBeT2hQKm
      YNTufL+3COuqgnHbC40PannAiLxqzyAxK/mEVQQH/bpp6DZ3y6uSEcIxgFF06lF2rIdY1KzWJUZksasA
      4zRP+iQt+V3CgxMY9/1PqVoNTB9/96DnVOs0U7EjCjvKt7UdQHK/8cR5Rl2tildBeXceQH2vPgxsm6tD
      aPO0SFYHypLxgMOLVOSrOq1fOflmop53x5ll38Hz6+2fOZdokL412xHeDrYgz6VqJ17NaZC+9bBLOPNN
      J84zVjGjsio8KqvKNbViVIjn2VfF6/n7dx94fSmHxu2M0mSxuPlAe4wL0r5djoWErCpW1U/WpTu45683
      jDqshRCX2jeoyfdFdkk54Syg8ONknEqmowDbtt1qWQ5WEhVcb0tJeiliSITHzMs1N4pEPa+aEVPvVsX0
      G0EHGOlt+uSC0CcXb9cnF5Q+uXijPrkY3ScX7D65CPTJ9YGEm5irN2jQHtmjFWN6tCKuRyuGerS8jh3W
      p+v+rmewRJYxtScc9efbJH1O8yJdFRkzhqnw4jSFOJftCbVGP2KGbzlPrueffqedXmBTgO24xzdZeAQB
      J6nFNSHApd69I+S+jRm+p/RKjUmIU1oW1duup4vjJN37sS6TsU3ZevWe2sl0Oc/IFCK+TXahHsGwpA7r
      md9HmN8HzCU9f46MbSqZ11ei16bqUsLkpIGAnuRQrp8yynFGIOy7K9mh2ad13pAvtScN65dERxrt6r7v
      G5L9YUVKQIezjdVuf5DdJ6KvpzCbmll5IuQJBKNu2ok6IGy5KU+Duq9b/OmsCFoymhjsk6Uo3WVNVgvC
      5oaowInRvEseSU4F+A7qb24R37OnWvaA4wf5F0kE8NT5M+eHHTnASL5pTcz3/aCafrgOdfzIr7+d/5Zc
      vPvlkmazUMt73Py/L3cEsw9bbsKyzvbbNk3cuddALE+79Jv1+1zU8gr6vSSge0nQ7wMB3Qd6WKXfZ6OZ
      Osh2Ec7/7r5u8bQlqSfAdOhUF5Rzo0zGMM3m06vl3fzbYjmnnlYLsbh5/DDCJ3Er5SbyUdO7uL+ZfFtO
      /1oS08DmYCPlt5sUbCP9ZguzfN3rDsnt5OuU+ps9FjeTfrtD4lZaGrgo6GUmAfrrWT8c+c28n4v9Uj0H
      t6c8zgVhw72YJIsZsfYwGN+k2niqSTG+qWuFqbIO832UrOgR36NbT6pJQ75LMFJLeKnVDqvUS89pc6hJ
      1+agtndTxah92rOrT4hKhXie56zOt69EUws5LtlgX38hiTRhW6h3k38nsQZyDocYeUM51OBGIQ3mTgRg
      If9yrw96/Oue7NlDlh/032X3ZU9/pQ7qXBByEod1DgcYf5BdPzwL9TGQg4E+8qIsiLXNEYNFkEbsMvcY
      tzSAI/7DqsjXbP2Jtu3EVtNrMdnDVIAFzbxU9WDQzUpRl7XNglG3CbBuE4xaSYC1kuDdqQK7U6nNut+m
      kwbq3fdtA3GofiJsC71jAfQqGEN+E+pd0yveTLnL4cZkm+8FV6thy80YXdgUbKuIJ0xBLGSmjF1sCrMl
      Nc+X1KhRMI3gLyaOsTwQdv6kvE3vgZCT0ApZEOQijd8cDPIJVqkRSKlpKm7ZPpKulTjOsiDARasSHcz1
      0S8Muir1t+Qlb56SUi3P1AvYiiz9brbvnFe8eHb/6v7OqBH/9koaJ9n9NE9+/9ydRit7VE/jzzP0Sc9a
      5qLZX1z8wjM7NGL/8DHGfqJB+99R9r8x+/zu4T4hLNo2GcBE6ESYDGCiNcoGBLjaQXw7P1DVZKuNY/6q
      JuzPDaCwt910blukjxx1TyP2dbVN18w0OcGY+1A/Z6oE8uRHOminzDUjOOLfZI+cEtijiJddTNBS0t7W
      hA39fRKwqrmI1WtMMnsGJAq/nFg0YNcpRnqaDaCAV0Tdl2LgvlSf8ysri0bsemcH9SqTOvZcHT4nuwc7
      ViTQZEX9Y/qtm2enjd0cEHGSRpk25xllhueyKLXbQGXrevz2g6jAj0FqHzvCsxDbxiPieTjT+AAa9HKy
      3eOBCKpJritycvYg7GTM1yE44ifP2cE0ZNf3IfVe9ljQnJVrXV0JhvnEwmbaxJ5PYlbyRDyCe/5cJNU+
      /XGg3oInzjPK/LwgvNBlU57tOGXOarphARqDf7sEnxt03yFNqxwJyMLuyYA8GIE8NLNBz9lO07Mv2sUR
      P/3BB4Jjfnb5CDwB6b7B7YV5LGjm1qUiWJeKiLpUBOtSwa5LRaAu1b1JRjN74kAjv1Q4NGznNrE2POBO
      0q36UOa1HCrkZUqaEx3n866A9tDIgizX1+nyy911u0lHnhWbpHndUyoYkLcitEu6CEeNmwxg0m+mUfu9
      Lgp5STNfJwYyEfaFtyDAtVkVZJVkINOB/vvcEQd9FaMFAS49MxVz+4Q0o+MRpxyGVEDcXA2LG3KMFoN8
      IknV2+lqI4aGXtpsHPbLIbzuNHDkRxYw7w70Ei0ZwETrEwLrVU9/rdbNhZ6/IPtOJGDVf79Yr1Zk64lE
      rTIu0ypJwCre5j4UY+9D8Xb3oaDch22fbLevMyGyzZvExnVI/Kbi37gOb0Xouvj55qIknM7ggaBTNPKz
      DcPZgpZTn693yIsm72oJSjnzYcN9ffHhw/lvqg+1T/PxE6Y2hvqO03nj36NEBX4M0vNlg/FNxOevFmXa
      ZveT+fIb+dUND0Sc499dcDDER2kNHM4w3v4+uyX+3h7xPKqwtg+4iXMCMA765zH2Oe7W578c77SsfJQf
      CWIESOHFoeTbifAsdfYoqxp1NmxR6Bq5yBpqFoIOL5KIy1MxlKciJk8FlqfzebKY/DnVu64Ty7eP2l61
      jVFW11VNm3HwyJB1y9dubW87BtQfU5wGBvnEqyw4O67WpG17+zNoR/65HG5MSq4zKW2r3pm5/UhQnCbn
      GA/lmv3zPdh263l9aladIMSVFOpPHKEmQ1byjQXgvr/Mfvbf0ptNUkP4BjuK/CM7C13WMauW5dPsjlPm
      XBYwq//gmg0WMM8nt9dstQkDbr1zTMW227jt14dekm+ZnsJs5JvGQYNe8m0D8UAEfZo3LzF6NOjlJYvD
      D0fgJRAkcWJVezVI3aX1d5K9xxxfrZaW6JCkYm1yuDFZr7hSiQa82z3bu9073gOnxB3AslZnqahKdsUM
      4K5/Vz1n+vi0jCbuOdDYbSfIFZu46xeNOg6DYTZA2ylSThr0lGOTrS31djoyhunP+2QynVzrE19TwhlR
      Hog4iefVQSxiJo1YXBBxqi7M+HMZABTxUvYz9MCAs13av8nrbE3Zh3/Ig0SkjMsdDjFW+4x30QoMOJPH
      tHkirKRFeCSCyAhvHblgwJmIddo0zMs2BUiMJn0kvdwEsIiZsmuzBwJO9cibtm8SgAJe9ZaWrPjrJ05N
      Z8KIm5vCBguY21d3mOlhwrb7k3rhaln9QVgKYVG27Wp2/2U615mqD1ykvTqECdAY63xPvME9GHfT2yyf
      xu2UtQA+inubuuB6JYp6u/1HKX1CTIDGoK14AljcTOwlOCjq1Y/693vaeAlXoHGoPQcHxb3PjAoF4tEI
      vDocFKAxdtWGm7sKRb3Eno5N4tZ8w7XmG9SqNqrmFhHNomYRX8bFmDKuvhRTA5z4YITo8mhLgrHU9rj8
      CtMwgFGi2teBtpWbD3j6x9Q04VomKkcHcpJZs6C1Cu/e9+97ercH6uvov33OS9o4xsBQH2FfLp+ErDNq
      A3iiMBvrEjsQcj6Qzh9yOdt4na1lCfqUiuzjLxSjyYFGddczhAqDfOSyY2CQj5rLPQXZ6DlicpBxc0Ou
      ZyzQc6oeMScRTxxuJJZvBwW9jOw5YqiPd5ngfdh9xsr2HnSc+WMmaD9aE5CFntE9hvr+uvvMVEoStVJz
      xSIhK7nonCjMxrpEuNzojxaUdXYWhdmY+X1CMS8vLY8kZmXcNg4LmblW3PgnbRWjw+FGZm4ZMO7m5VjP
      4mZu+pq0bZ/eXt1dT1mzJg6Keonjapt0rCWrX2NgkI9cFgwM8lHzv6cgGz3PTQ4yMvo1Fug5Wf0ak8ON
      xHrfQUEvI3vgfo3xAe8ywfap+4yV7Vi/5sv9H9P2yQD1ca9NYtac6cwhI+eptAUiTsYMv8si5uznvqob
      lrhFES+1RrZAxPl9s2UpJYcYuc/XQAESg9iGmBxipD6FtkDESX1GbIGos9Hv667zfZ6VDVNvOYKRRFZu
      aJNNoGBEjHb9gXoNhrXVIU2LXA/1GbYFAs4/rj9zKsMWg3zTryyfxkDfN3Y9aLCYmfiU0wIRJ6sOBPY3
      Mj+inl4Kwoib+uzOAhHn92zHUkoOMXLqU383FfMTzg4OCI9FoO/iAOOIn1UXHEHb+fU6Yk2EB4Nuxl38
      NbDC7vgZ8Q42MNRH7BvbJGzVJ5dzpBoEnd2x5AxpR4JWau31FVut+JW3pvArtqKw+2C3Ydh2G9hVPXN+
      q8JAH7GO+oqsO+z+Tn5ibnKgkfUE22VhM6/GQOsK0mYuNub52HVaoD7jpCKceuqFx3YXGobShj038Wlu
      S3gWRsqBacbIUz8/7z9NE0E6odqmHNsfV4vLC9kqfiPZTpRrm3670B/SbEfKt7FWzFkg4tzQ2mGTQ4zU
      dsMCEWe7XySx++TTIXst0qRKs31SpKus4MexPXhE/cXd4/ac2JBhjoFI+pIiI3WOgUiMtUSYYyiSEIlI
      i4a4gjnkCUQ8nawXk4ymBIlF7DuYHG4kjsQdFPGKN7pvxOj7Ru/ut253alTrdLnhLMmIWHLg3G8xEx3U
      sgWiqySRtZb6Omnb7wHPuIhyzJn93L9FzNY0EDWmJhSjakLxBjWhGFUTijeoCcWomlAYNViX2pG/zDIR
      or5B9vm68fFjmgFcNyL+WwUejhjd/ojh9icVgri4xMBQX3K9mDCdCsW97aagXHVL4/Y5/6rn4FWvUpFx
      GuKOg4ycZgFpAyi7hxoMbOLsxQzjkF/Nr8UEsHkgwiajjywNDjeSZ8E8GHSroxoYVoWhPu6lnljcrF8Y
      yGiP6iAeiNC9vEU2dxxu5CWHCQNu1lgZGSeTDlQ0IcRFOJvb5VAjo0Y9gpiT2QYYLGaec692jl3tOTNN
      z9E0Peem6TmepucRaXoeTNNzbpqeh9K0KYS6z9QiL9oOuEELHC2p0xfu80LMEYrEem6IKIA4jM4I2A+h
      nyLikYC17YyTlS2G+ngVucEC5l0u+33lY0ynxFcAcThzQ/C8kJrYiS3LgCMUiV+WfQUQ5zi1QrYfwYCT
      V2YsGrLr/ZLaw6fpcgPG3W3OcOUtjdt1dnDlGgbcgtuqCbxVExGtmgi2aoLbqgm8VRNv0qqJka2a3o+b
      +ETOAiEnZxYBmUPQA2rW/XciQevfjF/sPc3Uf2alHpJyxFNRbAzwPZNfQjEw1MfLD4PFzXW2VgtqufIO
      H/RH/QLTYUdivU2FvEfFeYMKfnfq+FficiYD8330Rf7Y+1fMt5rQ95l4bzJh7zD1fyemngVCTnoK4u9C
      qQ2j212CkrTIU1J3wmV984b8bmlPOTa1f2GaieT84jJZr9aJeEp1K0WSY5KRsZJ8t5d9j5y6d94oYega
      1rtkVRyypqporzDhlrHRksu3iZdchiI2dfK0S3W6XHz4yI9oewIRH9c7dhTJhs1yyFFu9HZkMTF6y0A0
      EVEYO34ggiyp5xdRMbRhRJT30VHeY1F+u+DnessiZlnS4mskVzIyVnSNFBKGruEN7ljAE4jIzbuODZsj
      71jPMhBNRGRW+I49foN/x1qGEVHeR0eB7tj1Uyr/d/Eu2VfF6/n7dx/IUTwDEGUjryTbZO/jbl/QMjZa
      1A08aASuojwUBf+3WjRg/xmfcT8Hc+7Uj6K5Txjia2qWr6lhX0bYW93GYB+5AkR7K+0H1ZZ1fRIDfLKB
      5ORHiyE+Rn60GOzj5EeLwT5OfsD9iPYDTn60mO/rWnWqr8MQHz0/Ogz2MfKjw2AfIz+QvkH7ASM/Osz2
      rYr0e3axIvaSesq2MV6BA999U00HsYR0iO8h5mSHAB7aToQdAnreM0TvYRMnmY4cYuQkWMeBRuYl+leo
      DlZXTTxFdmRsk3qK3M4NrV7LdEfKWJcNmGnPoR3U97YzT7wrNtmAmX7FBop7q9W/uF6J2t6nVOjq7Cmt
      Ny9pTUoJl3XM++8Zt0PjsoiZ0RS4LGCO6tbCBiDK0/fNljGidlnA/LM96TQmgK+w4+zSWv656IpVkhaP
      VZ03T6ScwBxwJOYSBABH/KyFBz7t2DekDU/l113+A43/4PF6BEeUaMY27eUvzaLyGzZAUZh57cGgm5XP
      Lmub6/VF8ss7asPcU76NoQI8v9AcTtmjlhu/zOi5g63eqqzbs2Zdq9cLDttt/pOqRkVezIuLX4hySfgW
      WrUJ1ZLyb+8vqdciCc/ygTa/1xKQJaH/qo6ybWrqSc1D6UXyu5RUWF0WNnf1hHqIXm84eksAx2g/O35T
      HPZqq7KMFQ1RYXH1QW2MN79ggxHlr+X09np6rbdteVhMfieegQzjQT/hAToEB92UlYwg3ds/z+4XpP3v
      TwDgSAhbbViQ49IH9a2rQ0k4H8sDe+fv09vpfHKTqPPeF6SM90nMOj67XQ4zEjLZA2En5S0ll0OMhB0Q
      XA4xcrMnkDvtiwWVOuTtljCoDShCcZ7T4hARQ+OIn1fI0DLGLWKBEqaXp7KcmkSs4pT4JTf/bEUoDj//
      RCD/Fg+flvMpr3ibLG6mF46exK2MImKgvffLH9ejd7BX37VJtV1qWm4ogg7xPE2drhuiSDOG6evkarRB
      ftcmObu4uRxkJOzgZkGIi7Bgz+UAI6XYWxDgoiw+tSDARSjeJgOYSPuM2ZRjIy3m7AnHMqOm0sxPIeLC
      TZNxTLTlmgbieCgrz0+A4ZgvFuqF4HT8nXciHEtWUi2acCzHTUUpEy8e6Dj5U3cI7vi5E0Yg7Lqr4vW9
      vFmfs/H7ansg6NwdCoZQUr1ttlg8yK8m17PFMrm/m90uSfUaggf94+9hEA66CXUfTPf2r9ejp3PkVy2O
      Vt2dANtBqeyO37cNyzotxbaqdxTNCbJdtMquJ0zLh/H4B4ujpucHPz0/ENPzg5eeHzjp+QFOzw/k9Pzg
      p+d0+eXumvJyUE94lkNJ92imN+nhwtXd7WI5n8ibaZGsn7LxR5vAdMBOqaVAOOAeX1AANOAl1E4Qa5jl
      J59pSXAiXIvehS5bN3lV0mQGCDqbmjDj6XKusajGH8jQE5AlWeUV3aQo10bJziNgOKbLxdXkfpos7v+Q
      nTpSZvoo6iWUZRdEnZQf7pGwdZasPv6iOqWEaVuMD0Vo333lR2h5LAI3E2eBPJzpu0L2LgndUozHIvAK
      yQwtIzNuEZmFSoiITAcxmA6U15R9ErPSXrmFWMN8t5xdTeVXaWXNoiAboQQYDGSi5LwJ9a67T/+drFfi
      grCmykAcD21SykAcz47m2Lk8aZv/nrAtG9ov2bi/Qv7HRhXVfKNWZQiKy0FR7+o1Rt3Rtl0/Q6Cc4W5B
      tot23HZPOJaSWjhbwrbIP1ysVyuKpkN8T1FSNUXpWwirDQ3E9wjy1QjnaqSWmsQd4nuanw3VIxHbI8g5
      LoAcl1qqpkN8DzGvOsTw3E9v1ZfUm9lpUfTLtESyrsrRg8EBjR9vdcgLtf9du+OxoMZxcN+vq2+RUb0d
      hvgI9a6Nwb6a1Hr7JGCVaZ0/ko2aAmz7g6yM9UlkZGWP+l7Or4Z/7+OuyXdkV0thNlmG/8UzKhK1bvLt
      lqlVqO99SsXT+wuqsqV8W56+v1in++SeKjyBgFM9MNEbXVZka4/63uJJDvGKrCFn/AmEnZWuuepHjvbI
      gmZOge8w0JfLKmr8UwQPBJ2EDrtNwbbDTg4Msp3gOI8saK6zps6zZ056HtGgl/LcB8EBv547Um2WbLJ2
      1eZQ0Js8yOFH2slyWK2p7pbCbKTn0gAKeLPdht6otJRvKytmw3cCfaccdnESssN8n2jqdSoyygDSI0Er
      Ix1bCrSp5oGhUxjoK9Zpw/ApDPHtX1m+/SvoK/mZUoZypeRlS4nlS0k4TMDBfF9TFdXL+PWnDmb4ll+m
      c+rySwuCXKTG0qIgG6HiMhjIRGkgTchw7bMSHiSNFqMGPEr7SiQ7RIfj/nYFPNvf4b7/WUYlPI1yMNSn
      uhdMp0J77/30azJZ3J7rpdljjRaEuCiPpjwQcL7IEpKRhZrCbKxLPJG29a8P735LZref78gJaZMhK/V6
      fRqzs5IDwG3/6rXJBOvKbdK2yv9M1vKeW6Xjn8i7nGv8Lnt424pmaxnHVCVP8qLHt0oWZLvUky717szV
      7F7WwzqhKVYAt/37WnZsKbu7WpDtopZ5v6TrvL7+Qtsv2gMh52Jy375a+cf4IRFMw/bk/uETYetlAIW9
      3KQ4koB1ehWRFCYMurkJcSIBqzox9FeyUVOI7ZJlu8Rs8uuzP/XLW9QbFHNAkXgJi6cqvxQEy8A86l6b
      D9xr6nO9LpUrP8Kwm5vK89B9rNpIslFBiCuZPPzF8ikQc17Nb3hOCWLO+fSfPKcEASex/wD3HI5/5bcz
      Joy5o+4Bz4BH4ZZXG8f9MUkUaIPU51HtkCtAY8QkUKhNUp/z2qUTGbBesq2XIWtkO4V4sIj8hA+nelyp
      GSwz8+h7dz7i3o1qx1wBHiMmF+ZD9QOrXTuCASerfTPhkJvTzplwyM1p70zYdpMnO4B5jnZQzmnqbBK0
      cm8UAEf8jOLrsoiZnSBwq9Z+yG3SfBq2s5MDacnaD8nNmIFhvkue7xL1xSSsIxgRg3IIelCCxuI3xagE
      jMUsMIHSEpMRwTyYx9Un86H6hNvk+jRiZ6f2PFhbUZvZnsJs1AbWJlErsWm1SdRKbFRtMmRNbqf/wzcr
      GrITB6nIrPnpzxFtNz5ONT6Pu+cGRqrWl9h3R2isan0jKqFC7XrMcBU24FGikinYzrOGrA4a8l7yvZdB
      b2zCj2j/ga/x+gCIKBgzti8walxufDWigA2UrtiMGsyjeXx9NR9TX8X1FcLjc+s7UbkxH6wVeX0HeIxu
      f8brQ+CjdOdzVl8CH6c7n7P6FAMjdetzXt/CNRhR5O19fpHcf5qq1SajzRbl2WivcFmQ56IsdTIQz6Oe
      WH+XdWZabpJ1Vo9fjIPxXgS9uQnRqhnP1J2VSdhC1ANt5weZVX9cf75IKJtXeWDAmSy+TM7ZYk279v0q
      u1CvKasF7qTVtQgO+rMyym/itv/XZHUoN0WmagxSUbNAxKnKX77N1/J+4blNARJDHRoeHceVuLGoN/ev
      wL39q7416cl8pCCbqjl5xiOJWflJChmgKHERhuxxxQIyuFEob5b3hGtRq4iSXJBehvVJ1Eo61RViMXNX
      o2QbnvyE4/7nrKj2fH+HY36VF1x5y4bNk3IzjfsJvseO6Ax2yHUUxIcj0Joenw7bCWuyEdz1d60qzdpB
      rqsrsDRXB7mu4951p5uAc2bCCJUbt93V7g2iBkReTNUXVW/uEyMcMdAneD5h++5uZlff6LeOjYE+wo1i
      QqCLcltYlGv758PkhvlrLRT1Un+1AaJO8q83SdfK3m0MwYN+amqge44BH5NTBd93rPv86+T+XpH0yzZI
      zMpJaxNFvdyLDV0rPW0N0rDO7/6SyT6dL9vmSZ9wsJjd3dISI2gZE42QRAHHmEiUhAtJ3FhdKtOTzQAR
      JzVxThjiIydBz/XG+eT2OuneVhprMxnHJP+Spa8kUYs4HsKs2/H7jkG/zkJyaAKytAcJqfNT1F6F6hgy
      wvBpQOPEI24WYjKOKXukpaD8vmso01WRJduq/p4cSpFus2R12G4zyraMgyIn5jaXX6QcaGBTjq0dWJeb
      ZJc1TxUtPRzWMetX6lVYkvNEObZ9Nf5ozRPgOkR22FSMYm+CjlNkGS3RFOA5+HkggnkgmrQ50H5rixie
      q9F7NMuvWpy+OMJYxkAMj/lwjLI7mwfazuOTMKrS5Czj/ybn7y5+UZtHqDMkkvT55wXBC9CWPblfLJL7
      yXzyldZTBlDUO7719UDUSWiBfdK2qpea99/X4lwObzPCkXcQa5tX+finOsfvO4YiL9XZYcn4d6odzPbp
      rZllPbgnXVdPQTbKnWhCtos4h2MgrmebHoqGWud5pG0lzgoZiO3ZFukjKek14DiIt6l/b5qnNRAO1ADQ
      gJdayDzYdTfvknXdJLS1TwAKeDdk3Qay7PbndJGEQNcPjusH5MrIogywbNN1U9X0hO84wJj/2O3JOgUB
      LmIldGQAU0n2lICF/sOgX7UXglveexTw/iDrfngWeffTRmM2Bvpk25zIlotaJdmsbc5FUu3THwfSTXCC
      bFfEKdcIjvjJh9nAtG0ndpm8fpJKYHqr2lO2rTsUVfeg9GKR5G4yvU92j1tSvRfQDMVTfcL4cEfLUDT9
      tC8yVusYFeniDSJd4JHKqsy4ERQLm9uu4RuUBlA0HJOfR75lZLSLN4nm5RTzfHYQBt2sGgo/bUt/Sjms
      8wR4Dn3ZjNGEg8JexjjAQWGv7vPW1Y44iYQa8ChNFRejqUIRGuo5SyDsuNvywslSiwStnAy1SNAakZ2Q
      AI3Bykwft/2CP9ISoZGWYI4iBDqKEIyevwB7/oLXnxVYf5ayZuz4fd+gO/HUNtACAWedvpB1knFNf2c0
      y99Om3/YU84/6wnbQjufpScgS0S3EBSAMTg56qCgl5irPdXbKCum7fXR6l+0g/56wrFQjvo7AY6DfNif
      TTk22nF/BmJ5Li5+ISjkt12anL4nxjMR0/iIeB5yyvSQ7frwkSL58NGl6WlzZDwTNW06xPNwyqDF4cZP
      RbX+Lrjelvbs9Lw8QZbr/SWlnMtvuzQ5L0+MZyLm5RHxPOS06SHL9eH8giCR33bphHandARkIaeyxYFG
      YmqbGOgjp7oNek7OL4Z/LeOXgr+SU0dYnGdkpZmXXrP7L5PFl4TQYp0Iw3I/+WN6oU+VpzywcjDQR5jI
      tCnPdnrmtBOPRKWJel61b2ymumtkrUEaVtLSLndVV/tv6tbcNtXblvOHxTJZ3v0xvU2ubmbT26We1COM
      wnBDMMoqe8zLJBfikJbrLCKYLRoRs8422W5POWV3hCoYV/49F09v8WMd05iob/JzPVc4MqGGQPCgn1Bj
      wHTQrmYBRF1H3gOGBY6mTr2fzmPuNtsQjMLNEQMP+lWBjAmg+WAEZp73dNCuCna2iwjQCkbEoAztg5Jg
      LFX6dlmTqqmsyOLlqgbjRtw7vgWOJtn2P7jl2hLAMdoTrE+z2cck4ERDVHDc7Oc+q/NdVjbJ8zknmiUY
      jiE7KbtVbBwtGRPrudrX2/hoWgPH4xYJvCSYS5k4ZpOHIzArN6tWe1hM5+0xzqQkcDDQN358ZEGgi/BT
      bcqwLT9fqmUio3evOAGOY38gOhTQO/66+PDhfPQuNe23XVqViX2a1zTLkfJs3dMg/aypq26IZsBgRPnw
      7rc/36v3ftQmBO3jf8oRtRgPRlB7ycREsHgwAuHdGJvCbEla5KngOVsWNRf5+A0BABT1clN3MGXbTxPx
      PUYucdBPfLvHJ0Hr5iJnGCUF2ii1sIOBPlmBMXSSwmyUjeJ8ErTmFxyjpEAbt2zi5bItVLzffWJBM2m5
      i8vhxmS750olCnqf9ZrFkqHtSM/anf4nWwyRrSkzDRjvRZAVwjmjcB0xyKdeYSo3aa3epGmyUk2LCboe
      soDRZNodMoZfc7gxWVVVwdVqeMCdkO9Ajw9EoN8zFhswH9ZPac12a9qz6wqAUa2fOM/YFxpWBeLinl/V
      1fRWraNAG+8ON0jY2lDehfVA0Mm+P2w44KZnmMV65nZBJaOn14Oes0t1TrE1UcDbJOvmJ1mpKdDGae1P
      nG/UBYP1s3vStiaTm9/v5pQXIG0KslGO7bUp0LY5cGybA2yjJp6BgT7KfkIOBvo4GYHlA2FewqZAm+D9
      UoH9Uj0Ju+EZJeg6l8v57NPDcipbpkNJTESbxc2kPVpBeMCdrF6T29l1VIjOMSLS3af/jo4kHSMiNT+b
      6EjSgUYi1xEmiVrpdYWFot72TUjCxDvGhyNUq3/J1i4mRmsIR6EcWIvxaISce/k5ftXkWtEkUauslM5j
      8vTEhyNE5alhcKLo/Y8mD3/Ri7xFYlZiNhocZqRmogliTvJoxUFd7+z2MyM9jxRko6Zjy0Amcvp1kOua
      39B3/PRJzEr9vT2HGcm/2wAB59fp8svdNe/XGyxu5lxvjwLedLN5l9TZc/U925DNJgy7z9X4nTqr5cGw
      W33K0SoOMLavKIpD3mQrstaEITdxBNQxgGmTFZl6NY/x03sU8ubbLd0oIdBF2drZwSDfgZ56fj9O/ZV1
      YyJ3pO6tyH6o2oib7DThgFtkdZ4WbHuLY37enDDEYxGKVDS0Bb4Yj0Uo5UXEROh5LIJ6myxtDjUzwAmH
      /cl8+ufdH9NrjvzIImZOFdFxuJEzIPXxsJ86DPXxsH9d502+5t1WriMQiT7v4NEBO3HG22URs16jWLPE
      LYp44yqCwXpAb9dBH215NGKPq2QG65i+jqA+tYUNSBTianqIBcyMLjnYG9+lzfqJrNIUYON0k+H+MWMQ
      eKQwG/F5twUCTj2Kj7jBHB6LEHETODwWoS/EafFY8aLYjuFI5EfWqASOxdzcL6BA4rTVL2k3XIxHIvDr
      WDFQx4qI2kkEayfKpgYWhLiojwMtEHJWjLGDggAXbXsCBwN8tI0KHMzxnXZRJz9ZtEjMGvG0BHGMiETt
      piIONBJ11GuRqJU8Asb29Xc+1AdfcTrWsCIYh1wJ+XjQz5hUhwRoDO4tELoDqD0e5FwD5zMRn6tiTK6K
      uFwVQ7kqYnNVYLnKm+3GZrpZc9LIfPTN3d0fD/eqliGv2HZZ1Cz/9pjV9D4yaECjdH0TxmQY4kAjiQO9
      kHg0bF83NevaFQcbKScKuBxipJZjg4ONT6mQ3b685liPLGymHDfqcrCRet/1GOwTT4dmU72UHOmRdcx6
      FfH0djmfTck9KYfFzN8iOlOYZEwsancKk4yJRV1+gknwWNTOm43iXvId6rC4mdWxAvhwBEYjDBrwKDnb
      HronqHWDjeJekbEvV2RN0BuVm2IwN0V0bopgbs5ul9P57eSGlaEGDLn1Q+CyqV/p5hMa9LIrT9cwGIVV
      bbqGwSisCtM1QFGoD8aPEOQ6Pt/mZaxJg3b6Q22DA42cNgJpHdp0pj9ycmHIzWtzsNamXaxIfMhkkYiV
      m/EnFPPqLfrZd7RrGIzCuqNdAxalYT7DhQRDMdg/pEGf5OqvqHEBXawozJZUxYZnVCRk5TRacFvF6nkg
      fY6qzIq8ZNzMHQg56YP/HkN9hCN+fDJkpT57c2HIzerD+b03WdqnV+270eptukbWSbRJG0gAx9A1qfoD
      x3+CUTd9DbjDwuZ885M7RwMa4Ch11tR59pxFhgI0A/HoT8BBAxylfcrD6CAAvBPhXp1zT+4jnCjIRq3z
      jpDrao+wvb275lRTHu3aHz7xfnnPwUbiJggGhvretdvbM7UdHbKTD9cIKOA4OStRciRNyCXshME+wcsz
      geWZiMozgefZ/P5uMaXuCmNyiJGxW4nLImbyG5UmGHDS10p4dMgu4vQi7NePNDZcfUuH7VHXfxIEYtDb
      Io8O2CMSJ5gyTX0Q/KvWNGKnVyEnzjGqXaF4zyUtErMSa2KDw4zU2tgEAad+dSRtmposPZEhK2f8DAmG
      YlDHz5BgKAZ1Yg8SwDG4rxf4+KCfvGwWVgBx2td6GMeS4QYgSjf1yCqxBguZ6ZOWPQb5iC18xwCmU9Kz
      Ms+iATur4kPqvIi3QHwc9p8n2S7NC467Q2Evr0gdwYCTWwU6/EAETgXo8KEI9A6IjyP+iLrPxhG/HCxx
      KqMeRbz8NxFAAxalnQ+hd8AhARKDs57YYQEzo+sD9no4HR64r0Of1zhRmI06+WqCqHO7Zzq3UOsh+PeA
      CN0DIrZ0iuHSKSJKpwiWTvJq9yOEuMir3U0QcDJWlPeY59PvPvLfMYcEeAzy25QOi5iZb3P7OOYn99dO
      HGJk9Kx6EHHGvI2MOEKR1IYF61Rt+3ZNfZsp4AlFbFed3h52q6zmxzMteDR2YYLf/XU+5XX8IMVwHHr3
      D1IMx2EtcA94BiJyup2AYSAK9f1ggEci5LyLz7ErpveFThxiVK3kG9zkviYQL/oWdyVOrMXsd3rde4QA
      F3lW/QjBrh3HtQNcxNLVIoCHWqo6xjUt7+ZTfRYb5/mGR6N2es5aKOrV7QZ5gxKAH4jwlOZlVAglGIhx
      qGt1Msqa+BoFrhkXj7ElQtAUjkp/5AcJBmPoFCB27lHLQLSqyNevScMv4a4mHE80VR0VSQvCMWTzqx7k
      EHfMwiShWOex99b58L11Hl3Gz0eU7dgfMvw7+ns7qsKzNMF4WV1XEanW8sMR5DBv3zzFxmkt4Wg/6e8M
      gIahKLKhbVerxoU6aQbi7WXVkTddFRIV0jKhUcmvptko6iX3aUwSte4P9b4Sarf2J9n95F64Y0Gj6aUp
      svEVzDgnPhwhph0Vw+2ofqmZX8sc8bA/or4Ug/WlsbFIRIzOMBCFX3ud+GCEmHpYDNbDIrpmFCNqRvWd
      bZE+RtwXLR+M0N2lETE6QzBKk+9iQig87CevwQH4YIR2yjlZryKinBxopK7/p87XWX9nRrIcaKS/s7pi
      BlAo6FUz28w68IjiXtYgryNRa1FV31lD+B4G3czROzpyN/Za51QHJo77uS3kwCizHXLIvGVeeQcH3Ly+
      w4nFzNz1/pAAjaF+G7Nwmzju16uNIgIc+YEIeri3iQrSKgbi9NOvUbF6DR6PPb9n0Ki93dqImysdHbSz
      h/C2AI3RVn8xd7alGIzDvstNAxqF8STahQfcvL7D42C/oahS1Ra1pZmTRLYAjMEbZ2JjTL1ZIre16WHM
      HVOniqE6VUTWqWKwThXxdaoYU6eKt6lTxdg6VUTVqWKgTjW3xdynzZNgxrAcgUi8EWx49Boz4guP9kRU
      iyMGWhwR2+KI4RZHxLc4YkyLI6JbHDGixYkbeQ+NumNGxOHRsIhpKUW4pYwdZQ+PsBn7oZqg41zOHxbk
      09R7CrRx6keLBK3kJ/s9hvroiyEdFjMz3mNzWNRMX2fjsKiZXms7LGqm38cOC5qpb5adKMzGmjn2aMf+
      54RxPssRAlzERxl/QrtFqT9Se8Md45qm89nnb8n9ZD752p6bxHgchUkGYzXpirhXJOIYiHSePFXEAgwr
      QnFU5VczbkJMEopFL5AuHbKTq2qPHrLTK25YMRhnn2X1G8Q6agbiMSp3WDEUh971hxVDcSJLM9ayWF/i
      POCFBKEYjCl2gA9FIFfHDhxyq9kGvlzRQ3bGi36IYzBSXE18UgzGyfeRUfL9iBhJKtbRcZRkMFZcLXZS
      DMbRTXeeichYR81AvNiaTIypyUR8TSbG1GTqS6psvkGsk2YoHmcAj0mGYpEfoIOGwSjkwQasCMXRnUbW
      QBfXOPHYb4AF3vzSH9WZfo2Pscmtj0N+nXhsvUn7dvJbQPB7anr3f3o3tcdAH7mZ7THHp9c48U9u9XHQ
      z5hJMkHPqcKl34nTHj0G+tYpw7ZOQRe9j2JwoJHcF+kx0EfscxwhxEXuW5gg7KQ/ywk8wYnbhWRoB5Lu
      c0bzZpGgld7EGJxrJG4V7e8SLf9yWtxNbmJdGHCznICL+VYw+jYwYxcYcAcY6tvE/lvEuoagT6r0mOOT
      /7UxTndJ5b8Yp8SgFiQaZ5mQw7pmaooAaaHnT9JD81TJMfor5/EcaAhHkdUJdf4eNISjMPIUNEBRmO+d
      h983b+fNqmaybTh5cCQR66dsS33HyUYhb7snRrLKG9EwLtnCIT/7Bdmhd98j9mcK7s3Uftjt5cEt5zYP
      RWhWQl1CWjzS7T0LmQ/5hlGmFeXbOBNX6O5U+oNqLfZ0naJ8W2Jsfkp1mixgPq4Q0cuE0jpLyX7PMBSF
      elwWJBgRI8nK5+g4SjIUi3xOGWgYEyX+Jx0tgWjHnnRMNhkOIBLnbRP87buod+4G3rTj7DcC7zMSsb9I
      cF+RiP1EgvuIxO4fMrxvCH+/kNA+Idz9QfB9QU4b1m2yjW7nDiJ9zDhyR4HF0fs+0qd+AR6IwD1H+zF4
      hrb6lJ80oRThdjIDfUx+FzPUw9RrLIusJDs7DjLSd4BDd0B8jNnD5TG8d0vczopDuypG7ag4sJsidydF
      fBdFte0Lu9DuAqV2xy+2O7zc7tT0TJJu/kVznjDH580wkGe1QAMcReUn139kA2byMUwuPOAmH8oECdwY
      tIbUW+sg6418Q38e0mOgj/w8pMccn3654/hGA73j7eOoP8KNevmXDF8tdamIvzpEDTdlStM3WTVBx7lP
      a5El27raJavDdkusBT3atbf75OhpdJrYAGFnkT1nxXEmaZNx7I4iFEd9zuj7Ig44kv7c2M2IE8l1DEai
      L/tEHEORfhzSIt/mshmOi9Z74IhqTyb6DLYLB9z6KnSOsiP0iqE4rGU5qGUo2kE24m8U0lIF4ra3BvvO
      ch1uJHJVCdaRnH2okT2ouUf/4af+sXa0Rnaz7ubNGY/oLNKxdmtP9CJnktQEHWe7so3Tc7dIxMroudso
      5O2HTWnxWNHlNh+O8JwWhywmhBb4MVizgfiOMyJijkME5zgEdzZC4LMRgj0bIQKzEczd49Gd46P2fx3Y
      9zVqR/qB3ei5O9Hju9CTd6AHdp9n7TyP7Drf312bA3EgbKOol97eOaxrNrKLPHh34ZCbPHz36CE7eQAP
      Grwo+31Vqx2PTnO5xBge70Rgzfgg8z3HP1O7MgbnGqvkeDACzdhzrlEvJKV3FQzOMTLWS4IrJRnvHoNv
      HB/fE6ZuVmVwuLHbXVM08mZ+5OotiR0rbXjn2ZkcbmQ8bwPwsJ/43A3Aw37iGXYA7vmZJ7LZpGflnMhl
      YKiPl4nBs7icz+lZGDyHy/ycPBD1YNv9/J6z/r2nPBtvNaYFek7Gc/OewmyMYuDBITexEHhwyM15hg4b
      0CjkguayvTm9yJPfp7fT+eQmuZ18nY61upxtnN1LeD5dLCi6E4S4ktsrlk5yhnGVJ00mW/tVukkO5Yta
      y9pkO9mRSuvR7XNQEo71Ulflo+wgPOaCMLgcNgFR10W1kqOwpD5/R45jsEHzeYT5PGi+iDBfBM3vI8zv
      g+ZfIsy/BM0fIswfQuZLvvgy5P2N7/0t5E1/8sXpz5B5teebV/ugOeKaV8FrXkeY10HzJuebN3nQHHHN
      m+A1i4hrFqFr/rnb8atQBYfd5zHu8wF31IWfD1153KUPXftFlP1iwP4+yv5+wP5LlP2XAfuHKPuHsD0q
      2QdSPSrRB9I8KskHUjwqwQfS+2OM+2PY/WuM+9ew+zLGfRl2/xbjhnoQeqAtu83tbkmbvM7WzXH1LDlW
      SAbE1jtOxEX0FUCcpk536tl2mZH9PQp4uxFHnTWHuiSrLRq3iyYdP6kJwiF3teerK7N3l4nzi8vH9U7k
      z4n8R/J99FoHAA16k6xcJz/PI/SdAYmyydYst+QQY7Ze6ZCrohq/ZAs3YFHk5zvxmPz8hRfihA/5L+P8
      l4j/+2bLEkvOMl58+Mgthy4a9NLLIWJAotDKocUhRm45RAxYFE45hPAh/2Wc/xLx08qhxVnGZN3Uun0i
      rEJwMNv39JKsV2v1A+rXfUNR2qRvber3F8dP27wVVD2g8OLIksm48o7ybF1ZZBgN0rfyjIit3VOrTRRi
      MfBp0H5Mcp7doG17WfFLm8tC5sgSh0qAWIxSZ3KAkZsmeHpElBOIRyIwywrEWxG6CvBJ7+H1kXQ4Ikzj
      9ij5kFt29F+fxz+hwngoQvdR8lTVJeH5BsJbEco8kV9iFHMbhJz0gm6DhlOU52rriG4BRFJk5eP4zQph
      2rFvqiTdrEjKFnE8qoNAWetuQYCLVGJNCHDVGenwY5cDjCJ9pusU5LgeM1ku0yL/O9vopU1NlYw/Mh43
      eFHU4SRVvs5khVTIUf/4UyExHoiwzbNik+wbuvtEAtau7LZVxbaq9WiasKJoUOTEzEW7/JCyMbgHus4m
      2yXrareSf6HfJB7t2Otsqx/DqypJzyPp+QbKyYYDGiyeatyqMuNF6WDHLSJLqhgsqc3rvltSnqQyxyqZ
      YxktBmhwohyaNfN+tsjeusqyQ7KrNrKKUyuM1QXUlG3VMN6IkFfdDKSQXULq6bEwbdu3m0Q8VYdCz96N
      Xx8BoLZX7Tcoy6tavqqSrbsA9ad0syH9grDJjqo+pKdRT/k2tTJf/jdV12GGr0xStQHSYSWrjVI0pHIC
      sLZ5s0leqnr8DkomY5nW1f6VrOohy7WRnT3Ob7U4y5j93Mt8J6hawHJs80bIG478Iy3ONqr3W3dV2TxW
      u4xwC3lkyJqIXVoUfHfLWxEe0+Ypqz8QnB1hWWSS1Gn5mJET1AZtp2i7yPIuIlsd1PXWWZE2+XNWvKqe
      AalcArRl/1e6rlY5QdgClqNY71j3jMXZxkyIpHlKS7MwzClqUIDEoGaXQ1rWXV4UelGP7GSRhh4QGzDL
      ngLphEFU4MQoc3nLJS/5Zvzo0OVsY7VpT41mlA+PBc3U3LM4zygr32SVym7NBfuSIQUYRxVNchXpw567
      65m9a293fhjUg0VkJ5nHoxGo9Z/HomaRreusiQpgKrw4hXjKt+qIbGYaeTwSITJAwL87FDGNO6bw4nD7
      mx4Lmjn1xYnzjIfzj+xrtVjHLG+18h3JpwnbIhObVUOanGdUEwjpL0RdC8GuS47rEnAxcsHkPKNKU6JM
      IaCH0XF1Uc9LvgGPjGfilBC/dFSyzJT6FWvV7axWz3l1ELLXKTNsXwnZ4yBEGHTZkUs9z8Eaz3isZd5X
      L7RcawHLUatxP2+84aK+t2tz9HeoYpO1zdnmsM5k0qxJzp7CbGoAtS9SrvaEO36R/81IWwOzfV1LSxaa
      HGA8prf+B9lr0ZCdd7nA1Yp12jS0Un9EbI+eOCVfl4k5voY9QvFYzywaOR5aM67WRj0vRwiYftSXPxM9
      Q1ymlErfBl0nvTXvIdh1yXFdAi56a25xnpHaWp4Yz0TO0SPjmn6ys/QnmqeMHi7cu7XaRHLqAbRlP3An
      BQ74jMCBO3A44KOGF/L07Ys3f1up54VCqB0U9+qQrWKrH4mNdiJ8H2F9kSeTxe158mm2TBZLJRgrB1DA
      O7tdTn+fzsnSjgOMd5/+e3q1JAtbzPCtVnqoomY4y9FrQW3Ktx3W4iJZZVRdhwG+ZvueJew40HjJsF3a
      JvU0W/01Iewa7XKmUZ9IR84Lk/Jt5LywMMBHzgubA42XDJuZF0+p/N+F3tTw9fz9uw9JtSfkCEiH7CIb
      397AtGFXC40qvepoXahxYVaqhQuja0yM7yNs1M1/daVemb+eLq7ms/vl7O52rB+mHTuv7tyE6s7+w6/3
      XO2RhKx3dzfTyS3d2XKAcXr78HU6nyyn12RpjwLebjuG2f9Or5ez8Ts5YDwegZnKFg3YZ5MPTPOJhKy0
      FnWDtqinT24fbm7IOgUBLlrrvMFa5/6Dq+WUfXeZMOC+l39fTj7d0EvWiQxZmRft8ECExfSfD9Pbq2ky
      uf1G1psw6F4ytUvEuPx4zkyJEwlZORUCUgssv90zXBICXA+3sz+n8wW7TnF4KMLyivXjOw40fr7kXu4J
      Bbx/zhYz/n1g0Y79YflFgstvslL7fNc10qQAkACL8cf02+yaZ9eo4z001X17xNQf41fz+6Rt/TRZzK6S
      q7tbmVwTWX+QUsODbffVdL6cfZ5dyVb6/u5mdjWbkuwA7vjnN8n1bLFM7u+oV+6gtvf6yz6t052gCI8M
      bEoIS+NczjHO5rK9u5t/o98cDup6F/c3k2/L6V9LmvOEeb4ucYm6jsJspK25ANTxLia8W8oCA05yxrtw
      yD1+s3KI9c2HVZGvGQlx5Dwj8fRGm8JsjCQ1SNRKTswe9J2L2e9Um0Q8D6MaOkK2a3rFuKoT5LruVYSs
      IZxB4XKekXUTmhxupJYXlw2YaWXGQV0v42Y5QYiL/tPRO6X/iPqjsftkej27n8yX36gVusk5xr+W09vr
      6bXqPSUPi8nvNK9H23bO3pAbdG9I95MFV+n0XWaLxYMkmO2vT9v22+lycTW5nyaL+z8mVxSzTeLWGVc6
      c5x3y5nsQE4/k3xHyHbdLb9M59RsP0G26/6Pq8X43bx6ArJQb++eAm20G/sE+a5fqZ5fAQfnx/0K/7ZL
      fmMA4GE/PREvA62C/lxN7PypayU15iTrbXzQz0ohXzEch5FSngGKwrp+5Io51+hdlRq7fiNn3YmCbP98
      mNzwjEfSsc7v/vqmB9xtyuq2cEF85IFKoFjt1dD1LecYyR0nqNfE6zJh/SVWZwnpKfF6x1jfOKIyDNWD
      7CowUPtxBqTIaHTOHenP8ZH+PGakPw+P9OcRI/15cKQ/Z4705+hI3/yEkwwmGzDTE8FAPW9yv1gkciAx
      +bogag0SsJLrojky4zFnz3jMAzMec+6Mxxyf8XhYyJ6u7jpThD1l29Qu/RSP+r5vSCY3v9/NqZ6WwmwL
      nm4B+ZbL+ezTw3JKVx5JyPrwF9338Bdg0q04R3cEIafsFdB9EoJc8xu6an4Dm8j9agtEnMR71uQQI+1+
      NTDAx+rg2WTIuuBrobuFOvY+QYgrmd4u599YxhYFvPSK38AAH+EsMJOBTbwSfgQRJ6eEdxxiZJTwFgN9
      f979QVtYZHKAkTh9fmQA058Teu0lGcDEyQM4/Rlpb6W7SBO9B8wuG/+ShAXZLn1kebKnP2kA2N6crZPf
      P3cvMqeb0QsGHQz2bVYFxycx2LfNimzXHQr/2ow/SDrkCEXaHQp+CAmH3OJHzXdLOORuqtj0ORrgKI91
      ddgn8s/5+LM1MT4UgbJzA0yH7HpzqUM9fue3gAKOo64g2deZel2SE8Tk4QjMEoqWTbX0V+2awJRqNmRu
      1k98tYRxd0QyG3jAr0fOcT/BdHiR5M3QqNNB19UmU2/yFWmt9qOh3sSYxosn8t2+0MfnJj+TdVXVm7xM
      G2rOIxYsWmQNjljC0Zi1IejAIkXUiIAhHOWRWW/BknAsRg3s8eEI4i1+jRj6NXpvEOYvaVnULJJU1dQq
      55pXZgTLEYhUlTFpZQiwGHr7Q70rGy9Ez4cj8MtVz4cjqCIh79q4jAFVwbgiyX4c0iIiXGewoqRb9V/d
      rl9pSY4B8lCE9q1vurnlIKNMuGNYutaAbTd1WGUylmmVP5YHXb/rip7gc0jE2rbALG2LWt6IxjrYQquu
      z6HJkpfbyWeK08AsX9to0oaTJwYwUcu7QQE2Vvcj2OdoPyyzR7JQMpBJ1tNqq95kl4rvdKdJA3byTW5i
      kO+wossOK8Ckulm6/JN9JxKxsnIb7PWpnpN5I6ldg6l61DEYiVyf4BI7lu5HldkLRX1kLNNTKp5Uyul+
      RrJ/f/lL8nOn9vtNP5xfJEK8HJJNnW6bd78SQo2XgtfSjYNcjn8dYaF1DcxJAHTsf2rE5WW0zSTB6sMD
      bvKAF1NYcfbfs1dq+31ibJPuoelq+VCqtKozITJKu4MYgCh65y7q/eeiQS917gXkhyLQ8hMWhGPQSzum
      GIij51OiwmjDmCjxCYfO/hxHGcRW2cRAX3O8AfvaXzD8kAaIx2hlbdB2tvnPSBULtJxqt7VKd49074h8
      K4O8FaHLaVrHt4cgl+7EUo8HQHDIz+oMeyxqpm8GiAqgGHn5/C4qhiMAYwjS6RseCDntHVjpapuHItAG
      Iz0Eudq9/+i6loOM5Nva4kAjaRDSQ5CLUZU5JGKNyXJkd0zkC6pg82sNVGXHbefFRLrtpq4ogVzWNrfz
      YfE3ecgTiPgmSTnOaF6FelIv5Cg2ecmbJ9XOrNujjb6X1cv/b+0MmhvFtTC6n3/ydhMymbzZvppNV3XV
      VDlds6UIKDEVG2iE3U7/+ieBDVzpXsx36V0qcI6MjLCQ4FOVZpX9YVootAxQzj/HMIv0M3n6M83Ol2TM
      ggTulESFUA6a9MvCghu6FFJOMLp+0LZPPBcslOEzCzeVcRMIZQwdMKi7wtH37PB96oJksayiPgHrfIkC
      oYzbOfykKmCk79ifN9ml9rXpTGLOoiJ5enr4SzEQH4KxEx8cCMHJ6QPN3vtBG3cVWusjEOfqI9JwW49x
      Pr+2KK7zFGez1ppHXNdjgc993g6uuRvEufCamzDOB9fcSHE2vOYmjPr60Tuw4m4MY4KrbaIYG1ppI8S4
      4CqbqMlWJtmGbEGeDuy6bD0GZbxgilzIMUYs+S3AGB+WjBNgc1+uTWlkUMYL12Qu1mSx6Ywq7pxRhb4e
      iqV6KJRplTHJWbG0ypBjjJoWVSy1qGJTWqXEyyUoa1lIqxy3w2mVMclZ0dZRLLUONK2SQIwLvWYV0jWr
      0KdVsjDjhtMqY3LJqvzQYlrluIcmrZKFWfc3pfabYITTKmOSs2ouCMJVAEmrJBDjUqZVSjxXApZWGXKs
      EU2rZFDGq0qr5OnAviWtUhRIZUBplQxKvepcSRam7g25kgIe+HW5kgxKvWiu5JzhTcj7XyEXGHW5kgwa
      euFcyQCLfGCuFaUkG/SOKYMGXk3aRAQuOOEvXk6biDevfxWQY2MzmjYRcpERfNmWUpJNUaVsykKwDa5M
      LmXhtgl4BXWGRB7FZSjOlfT/hnMlCRS68FzJkIuMqkbI50qGW9DzRc6VjLZi54yYKzlsVDQWJleS/Bs/
      dLGlaHIlQy4wKnIlQy4wqnMleZraNbmSIScbX7TKoO+iz5XkaWrX5UrGpGz9opV+CZxoriSBqAvOlSQQ
      dWG5khPBWdDmzeVKzv6PNWwmV/L272fU88w4NAf3zB/bLLnxS/VWa8yM4n45eIXGhsVSNh7J3aPYdgR3
      P31VFluP4Kq4X862IxkMTCm6zE8Bv+tX1dZS5qe0k6K2FjI/p31Un1/4xJrPGH0qOPOTUpwNzfyMycC6
      NfNzUcKVhWV+hlxghDu1XI9W152V+rKqjqzQi9XduUj3LRsu7UtXdfUFfeFarhksEEYKdtpRmJ08CrPb
      MgqzWx6F2W0YhdktjsLslKMwO3EURpv5ybELZrwS2MzP60ZF5mdMMlb4WrQTRqN26tGo3cJo1E47GrWT
      R6PwzE9KURuS+XnbPzZgmZ+UkmwvOt0L50MzP2OSs64P6ZwzjAnN/IxAzglkfhKIc+2+4qrdV94E96uF
      zE+yCWyzfOYn2YK1Vzbzk2zoXq1K6DjGqOoySimi8bYXvZZrf+hIC5MiSv6NpYgyKOPFf0rYFNFxA5Ai
      Omd4k67NxCmiZJOmzUQpomSLos2EKaKzDVCKaMgxRnCyJE4RHf8LpIjOGcak+Q74+lfUPVvvmutUdI1q
      jfrCF6C81581Su8V5b1KZ+Cr/cQQ3ukn2Nxn9U9B2qWnIKONKfiwmiBgyoCfKbTiM4V2y3N7dvm5vU73
      jGEnPWN41j+/e156fvesnLs6i3NXZ+3c1Vmau/r4X92W1bvb293MvHxvu28/Vl/rOHbZ/NVUW+QOn/n/
      aUzlN5vM1tVL5/f+O+uy1QUIvFTCv9nhtP4tYI5dNiN1w+OT/2DO5tC/J1fVxepX4CgV2tyfGt2ITb5j
      8Uf6eqjzj7Rw9e1fTTSrkxc4dm5+um7N7FFl5/mphHpYqBL93Qiwydd85PYhScvOtFlX1pVNszw3TZcB
      ry4uOaKS/Gtx7+tPNUpFtubVpKbK288Gi3EUcOp/7s9F/8KyKfovA7FHcOhustaadG8y4PyISWr9b39E
      hemPCJEScOY8vnb1h6lSc2ke3Jnp2tJqa4xK3vxQmqrrv2M8AGSFSirXnVD+jDV+d11xc4NcSpfu+9fc
      /Zvt7iKvLSrQSOWV1p5M+0tqk1VJ5bbufNQV40nJ6huQzupJyXqqNpzLV5h3J/pWkqSL3l/WShKklSSb
      W0myopUkv6aVJGtbSfLrWkmCtJJE3UqShVaSqFtJstBKki2tJGFaSe36Hp9pnuV7M/TKoJ9UlpbsrTE6
      sQMFpzWdSuk42Zges6ZBTnaBj0rou46Kahg53ghEcgZY5PNd8j4HGHfOUd6rOPKR441HJHAvAonzM919
      R9bKmCGTx8e/+evch2tofW7R6+ntzfh7SNeh9R3v1c32vmlWqmYVoZZfRaidVgIasgiB3xeOpWb3Z+bj
      EMC+MIPy3maYzE87V33W1d5RU0Ik4cvqI5Pa7IemiBsrmX8anfWnoUY4J4VAxPUzffg9+SN9z7q9aZ/6
      xCZAytCc3ecd6cw3krNW7jtMWnfDp1MTnPO7bYnfSeknOOe3edZ1+konOOv/3mrVV3Ky2qRUjRqHHGPU
      jBqz8My9zx7Ug04sTNw+GGmDncOJ3+dJb/Bz+Mzv/m1MA630MWcC08GsX4tgBBhH2nQt7PEQdZ0aRHJq
      CP0G9L+vu1Me6Ahddyd8WVlgqZoRoA6b2rrtDHIgI0NMQFdx2Duk0+p0OGCKHqGe9SsCDHsTuqmR88Ht
      HdLod3pDWI+7V1OoHEVtp/ULTV13JzxwbzXsHdL93cDbqcoxzYhR3758gz6P358aaqjN+N0Jf/YzKoCg
      358YkIzg6+4T3/mvuL/HXr/ex5yZTOfbjyI+t8mg1KuZ2ww52fiiVb7ITqCxMejM+5hmvudcrr6iTgS1
      HDrEcOgI/ZrXlQX4fn9iyN2tLWLo96eG9uDzawtg+SFKRTbg6j4RkaXtZ0ZB0QCFrgKz0G/YdUpcf8v9
      G5CMDDGZS5d+nADNABCH++2we2M78APNMeIriwbQuL0pXb3VCO52D/h9+epTEqtP6GPMMOLzDfRks3fk
      TB4ZYqqyo18gobJdm/lF3ABhiFKvTcvsKT2UFrluzKjAlgN9yxEgjjq3jZ9tdmcI8h3MsdhX1f3YEuq7
      YsTX5CWgcXtT+jrcq/omY5hzXweQFeIbSawWbFQ2alUW/mWz0S9b3bRvism4kGONm6bh7nnYEjUTcALO
      +jdNhd3zsCUik2ABxvqQ6a8AY33gxFdMzqxNZmyav+a350xWS0MwcnbtYzI+vdKPrlhQzhjCUsDxcwKF
      LlUNCEfv796uxUDtgoM5961WVO4ZPLkvypDyi5hRft3ybpDQfAJxLt92+6aLLjOxoODKaR6aB78SRZPg
      BUzsovlxg/mRNT/26/756VdFhc9pzj6szuFTvHH3xC6boUXdRMGdMuwxOxzQhdfum9hS16+0QyDO1dXQ
      T18ERk54Uuwirh1w3WJzcP2jkJsZn37/69/H/inFfvxouMLY/jnk1fYFBy0pLcp3fwvXz0dmh/e6Lbv9
      ESmHN/ClnE1bvn1CT4QKeOBvWr8wRz93aW2K5bSJgqCMfnK7u/RXIYvZKcp4faH+GtRdYO+EUq8fGUrK
      tGyQH6GAi4zDr4crbm8uoHSORt7hyRlz6UxlS2D4SsAjvysTXrCLQSPvoa4/rLuF/jBp4e6n/V06qGcM
      USnDzT9wyabYf377P1bsXgotpgQA
    EOF

    # We are renaming openssl to openssl_grpc so that there is no conflict with openssl if it exists
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <openssl/;#include <openssl_grpc/;g'

    # Include of boringssl_prefix_symbols.h does not follow Xcode import style. We add the package
    # name here so that Xcode knows where to find it.
    find . -type f \\( -path '*.h' -or -path '*.cc' -or -path '*.c' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include <boringssl_prefix_symbols.h>;#include <openssl_grpc/boringssl_prefix_symbols.h>;g'
  END_OF_COMMAND
end
