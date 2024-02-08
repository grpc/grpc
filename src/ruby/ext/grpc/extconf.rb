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
require_relative '../../lib/grpc/version.rb'

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

def debug_symbols_output_dir
  d = ENV['GRPC_RUBY_DEBUG_SYMBOLS_OUTPUT_DIR']
  return nil if d.nil? or d.size == 0
  d
end

def maybe_remove_strip_all_linker_flag(flags)
  if debug_symbols_output_dir
    # Hack to prevent automatic stripping during shared library linking.
    # rake-compiler-dock sets the -s LDFLAG when building rubies for
    # cross compilation, and this -s flag propagates into RbConfig. Stripping
    # during the link is problematic because it prevents us from saving
    # debug symbols. We want to first link our shared library, then save
    # debug symbols, and only after that strip.
    flags = flags.split(' ')
    flags = flags.reject {|flag| flag == '-s'}
    flags = flags.join(' ')
  end
  flags
end

def env_unset?(name)
  ENV[name].nil? || ENV[name].size == 0
end

def inherit_env_or_rbconfig(name)
  ENV[name] = inherit_rbconfig(name) if env_unset?(name)
end

def inherit_rbconfig(name, linker_flag: false)
  value = RbConfig::CONFIG[name] || ''
  if linker_flag
    value = maybe_remove_strip_all_linker_flag(value)
  end
  p "extconf.rb setting ENV[#{name}] = #{value}"
  ENV[name] = value
end

def env_append(name, string)
  ENV[name] += ' ' + string
end

# build grpc C-core
inherit_env_or_rbconfig 'AR'
inherit_env_or_rbconfig 'CC'
inherit_env_or_rbconfig 'CXX'
inherit_env_or_rbconfig 'RANLIB'
inherit_env_or_rbconfig 'STRIP'
inherit_rbconfig 'CPPFLAGS'
inherit_rbconfig('LDFLAGS', linker_flag: true)

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

env_append 'CPPFLAGS', '-DGRPC_XDS_USER_AGENT_NAME_SUFFIX="\"RUBY\""'

require_relative '../../lib/grpc/version'
env_append 'CPPFLAGS', '-DGRPC_XDS_USER_AGENT_VERSION_SUFFIX="\"' + GRPC::VERSION + '\""'
env_append 'CPPFLAGS', '-DGRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK=1'

output_dir = File.expand_path(RbConfig::CONFIG['topdir'])
grpc_lib_dir = File.join(output_dir, 'libs', grpc_config)
ENV['BUILDDIR'] = output_dir

strip_tool = RbConfig::CONFIG['STRIP']
strip_tool += ' -x' if apple_toolchain

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

# C-core built, generate Makefile for ruby extension
$LDFLAGS = maybe_remove_strip_all_linker_flag($LDFLAGS)
$DLDFLAGS = maybe_remove_strip_all_linker_flag($DLDFLAGS)

$CFLAGS << ' -DGRPC_RUBY_WINDOWS_UCRT' if windows_ucrt
$CFLAGS << ' -I' + File.join(grpc_root, 'include')
$CFLAGS << ' -g'

def have_ruby_abi_version()
  return true if RUBY_ENGINE == 'truffleruby'
  # ruby_abi_version is only available in development versions: https://github.com/ruby/ruby/pull/6231
  return false if RUBY_PATCHLEVEL >= 0

  m = /(\d+)\.(\d+)/.match(RUBY_VERSION)
  if m.nil?
    puts "Failed to parse ruby version: #{RUBY_VERSION}. Assuming ruby_abi_version symbol is NOT present."
    return false
  end
  major = m[1].to_i
  minor = m[2].to_i
  if major >= 3 and minor >= 2
    puts "Ruby version #{RUBY_VERSION} >= 3.2. Assuming ruby_abi_version symbol is present."
    return true
  end
  puts "Ruby version #{RUBY_VERSION} < 3.2. Assuming ruby_abi_version symbol is NOT present."
  false
end

def ext_export_filename()
  name = 'ext-export'
  name += '-truffleruby' if RUBY_ENGINE == 'truffleruby'
  name += '-with-ruby-abi-version' if have_ruby_abi_version()
  name
end

ext_export_file = File.join(grpc_root, 'src', 'ruby', 'ext', 'grpc', ext_export_filename())
$LDFLAGS << ' -Wl,--version-script="' + ext_export_file + '.gcc"' if linux
if apple_toolchain
  $LDFLAGS << ' -weak_framework CoreFoundation'
  $LDFLAGS << ' -Wl,-exported_symbols_list,"' + ext_export_file + '.clang"'
end

$LDFLAGS << ' ' + File.join(grpc_lib_dir, 'libgrpc.a') unless windows
if grpc_config == 'gcov'
  $CFLAGS << ' -O0 -fprofile-arcs -ftest-coverage'
  $LDFLAGS << ' -fprofile-arcs -ftest-coverage -rdynamic'
end

if grpc_config == 'dbg'
  $CFLAGS << ' -O0'
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
puts "extconf.rb $LDFLAGS: #{$LDFLAGS}"
puts "extconf.rb $DLDFLAGS: #{$DLDFLAGS}"
puts "extconf.rb $CFLAGS: #{$CFLAGS}"
puts 'Generating Makefile for ' + output
create_makefile(output)

ruby_major_minor = /(\d+\.\d+)/.match(RUBY_VERSION).to_s
debug_symbols = "grpc-#{GRPC::VERSION}-#{RUBY_PLATFORM}-ruby-#{ruby_major_minor}.dbg"

File.open('Makefile.new', 'w') do |o|
  o.puts 'hijack_remove_unused_artifacts: all remove_unused_artifacts'
  o.puts
  o.write(File.read('Makefile'))
  o.puts
  o.puts 'remove_unused_artifacts: $(DLLIB)'
  # Now that the extension library has been linked, we can remove unused artifacts
  # that take up a lot of disk space.
  rm_obj_cmd = "rm -rf #{File.join(output_dir, 'objs')}"
  o.puts "\t$(ECHO) Removing unused object artifacts: #{rm_obj_cmd}"
  o.puts "\t$(Q) #{rm_obj_cmd}"
  rm_grpc_core_libs = "rm -f #{grpc_lib_dir}/*.a"
  o.puts "\t$(ECHO) Removing unused grpc core libraries: #{rm_grpc_core_libs}"
  o.puts "\t$(Q) #{rm_grpc_core_libs}"
end
File.rename('Makefile.new', 'Makefile')

if grpc_config == 'opt'
  File.open('Makefile.new', 'w') do |o|
    o.puts 'hijack: all strip'
    o.puts
    o.write(File.read('Makefile'))
    o.puts
    o.puts 'strip: $(DLLIB)'
    if debug_symbols_output_dir
      o.puts "\t$(ECHO) Saving debug symbols in #{debug_symbols_output_dir}/#{debug_symbols}"
      o.puts "\t$(Q) objcopy --only-keep-debug $(DLLIB) #{debug_symbols_output_dir}/#{debug_symbols}"
    end
    o.puts "\t$(ECHO) Stripping $(DLLIB)"
    o.puts "\t$(Q) #{strip_tool} $(DLLIB)"
  end
  File.rename('Makefile.new', 'Makefile')
end

if ENV['GRPC_RUBY_TEST_ONLY_WORKAROUND_MAKE_INSTALL_BUG']
  # Note: this env var setting is intended to work around a problem observed
  # with the ginstall command on grpc's macos automated test infrastructure,
  # and is not  guaranteed to work in the wild.
  # Also see https://github.com/rake-compiler/rake-compiler/issues/210.
  puts 'Overriding the generated Makefile install target to use cp'
  File.open('Makefile.new', 'w') do |o|
    File.foreach('Makefile') do |i|
      if i.start_with?('INSTALL_PROG = ')
        override = 'INSTALL_PROG = cp'
        puts "Replacing generated Makefile line: |#{i}|, with: |#{override}|"
        o.puts override
      else
        o.puts i
      end
    end
  end
  File.rename('Makefile.new', 'Makefile')
end
