# nanopb 0.3.5 CocoaPods podspec

# Copyright 2016, Google Inc.
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
  s.name     = 'nanopb'
  s.version  = '0.3.5'
  s.summary  = 'Protocol buffers with small code size.'
  # Adapted from the homepage:
  s.description = <<-DESC
    Nanopb is a plain-C implementation of Google's Protocol Buffers data format.
    It is targeted at 32 bit microcontrollers, but is also fit for other
    embedded systems with tight (2-10 kB ROM, <1 kB RAM) memory constraints.
  DESC
  s.homepage = 'http://koti.kapsi.fi/jpa/nanopb/'
  s.license  = { :type => 'zlib', :file => 'LICENSE.txt' }
  # "The name and email addresses of the library maintainers, not the Podspec maintainer."
  s.authors  = 'Petteri Aimonen'

  s.source = { :git => 'http://koti.kapsi.fi/~jpa/nanopb/download/nanopb-0.3.5.tar.gz',
               :tag => 'version_for_cocoapods_0.3.5' }

  s.source_files = '*.{h,c}'

  s.public_header_files = '*.h'
  s.header_mappings_dir = '.'

  s.requires_arc = false
end
