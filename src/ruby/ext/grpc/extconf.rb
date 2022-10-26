# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

require 'etc'
require 'mkmf'

windows = RUBY_PLATFORM =~ /mingw|mswin/
windows_ucrt = RUBY_PLATFORM =~ /(mingw|mswin).*ucrt/
bsd = RUBY_PLATFORM =~ /bsd/
darwin = RUBY_PLATFORM =~ /darwin/
linux = RUBY_PLATFORM =~ /linux/
cross_compiling = ENV['RCD_HOST_RUBY_VERSION'] # set by rake-compiler-dock in build containers
# TruffleRuby uses the Sulong LLVM runtime, which is different from Apple's.
apple_toolchain = darwin && RUBY_ENGINE != 'truffleruby'

grpc_root = File.expand_path(File.join(File.dirname(__FILE__), '../../../..'))

grpc_config = ENV['GRPC_CONFIG'] || 'opt'

ENV['MACOSX_DEPLOYMENT_TARGET'] = '10.10'

def env_unset?(name)
  ENV[name].nil? || ENV[name].size == 0
end

def rbconfig_set?(name)
  RbConfig::CONFIG[name] && RbConfig::CONFIG[name].size > 0
end

def inherit_rbconfig(name)
  ENV[name] = RbConfig::CONFIG[name] if env_unset?(name) && rbconfig_set?(name)
end

def env_append(name, string)
  ENV[name] ||= ''
  ENV[name] += ' ' + string
end

inherit_rbconfig 'AR'
inherit_rbconfig 'CC'
inherit_rbconfig 'CXX'
inherit_rbconfig 'RANLIB'
inherit_rbconfig 'STRIP'
inherit_rbconfig 'CPPFLAGS'
inherit_rbconfig 'LDFLAGS'

ENV['LD'] = ENV['CC'] if env_unset?('LD')
ENV['LDXX'] = ENV['CXX'] if env_unset?('LDXX')

if RUBY_ENGINE == 'truffleruby'
  # ensure we can find the system's OpenSSL
  env_append 'CPPFLAGS', RbConfig::CONFIG['cppflags']
end

if apple_toolchain && !cross_compiling
  ENV['AR'] = 'libtool'
  ENV['ARFLAGS'] = '-o'
end

# Don't embed on TruffleRuby (constant-time crypto is unsafe with Sulong, slow build times)
ENV['EMBED_OPENSSL'] = (RUBY_ENGINE != 'truffleruby').to_s
# Don't embed on TruffleRuby (the system zlib is already linked for the zlib C extension, slow build times)
ENV['EMBED_ZLIB'] = (RUBY_ENGINE != 'truffleruby').to_s

ENV['EMBED_CARES'] = 'true'

ENV['ARCH_FLAGS'] = RbConfig::CONFIG['ARCH_FLAG']
if apple_toolchain && !cross_compiling
  if RUBY_PLATFORM =~ /arm64/
    ENV['ARCH_FLAGS'] = '-arch arm64'
  else
    ENV['ARCH_FLAGS'] = '-arch i386 -arch x86_64'
  end
end

env_append 'CPPFLAGS', '-DGPR_BACKWARDS_COMPATIBILITY_MODE'
env_append 'CPPFLAGS', '-DGRPC_XDS_USER_AGENT_NAME_SUFFIX="\"RUBY\""'

require_relative '../../lib/grpc/version'
env_append 'CPPFLAGS', '-DGRPC_XDS_USER_AGENT_VERSION_SUFFIX="\"' + GRPC::VERSION + '\""'

output_dir = File.expand_path(RbConfig::CONFIG['topdir'])
grpc_lib_dir = File.join(output_dir, 'libs', grpc_config)
ENV['BUILDDIR'] = output_dir

unless windows
  puts 'Building internal gRPC into ' + grpc_lib_dir
  nproc = 4
  nproc = Etc.nprocessors if Etc.respond_to? :nprocessors
  nproc_override = ENV['GRPC_RUBY_BUILD_PROCS']
  unless nproc_override.nil? or nproc_override.size == 0
    nproc = nproc_override
    puts "Overriding make parallelism to #{nproc}"
  end
  make = bsd ? 'gmake' : 'make'
  cmd = "#{make} -j#{nproc} -C #{grpc_root} #{grpc_lib_dir}/libgrpc.a CONFIG=#{grpc_config} Q="
  puts "Building grpc native library: #{cmd}"
  system(cmd)
  exit 1 unless $? == 0
end

$CFLAGS << ' -DGRPC_RUBY_WINDOWS_UCRT' if windows_ucrt
$CFLAGS << ' -I' + File.join(grpc_root, 'include')

ext_export_file = File.join(grpc_root, 'src', 'ruby', 'ext', 'grpc', 'ext-export')
ext_export_file += '-truffleruby' if RUBY_ENGINE == 'truffleruby'
$LDFLAGS << ' -Wl,--version-script="' + ext_export_file + '.gcc"' if linux
$LDFLAGS << ' -Wl,-exported_symbols_list,"' + ext_export_file + '.clang"' if apple_toolchain

$LDFLAGS << ' ' + File.join(grpc_lib_dir, 'libgrpc.a') unless windows
if grpc_config == 'gcov'
  $CFLAGS << ' -O0 -fprofile-arcs -ftest-coverage'
  $LDFLAGS << ' -fprofile-arcs -ftest-coverage -rdynamic'
end

if grpc_config == 'dbg'
  $CFLAGS << ' -O0 -ggdb3'
end

$LDFLAGS << ' -Wl,-wrap,memcpy' if linux
# Do not statically link standard libraries on TruffleRuby as this does not work when compiling to bitcode
if linux && RUBY_ENGINE != 'truffleruby'
  $LDFLAGS << ' -static-libgcc -static-libstdc++'
end
$LDFLAGS << ' -static' if windows

$CFLAGS << ' -std=c11 '
$CFLAGS << ' -Wall '
$CFLAGS << ' -Wextra '
$CFLAGS << ' -pedantic '

output = File.join('grpc', 'grpc_c')
puts 'Generating Makefile for ' + output
create_makefile(output)

strip_tool = RbConfig::CONFIG['STRIP']
strip_tool += ' -x' if apple_toolchain

if grpc_config == 'opt'
  File.open('Makefile.new', 'w') do |o|
    o.puts 'hijack: all strip'
    o.puts
    File.foreach('Makefile') do |i|
      o.puts i
    end
    o.puts
    o.puts 'strip: $(DLLIB)'
    o.puts "\t$(ECHO) Stripping $(DLLIB)"
    o.puts "\t$(Q) #{strip_tool} $(DLLIB)"
  end
  File.rename('Makefile.new', 'Makefile')
end
