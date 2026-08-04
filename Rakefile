# -*- ruby -*-
require 'rake/extensiontask'
require 'rspec/core/rake_task'
require 'rubocop/rake_task'
require 'bundler/gem_tasks'
require 'fileutils'
require 'tmpdir'

require_relative 'build_config.rb'

load 'tools/distrib/rake_compiler_docker_image.rb'

# Add rubocop style checking tasks
RuboCop::RakeTask.new(:rubocop) do |task|
  task.options = ['-c', 'src/ruby/.rubocop.yml']
  # add end2end tests to formatter but don't add generated proto _pb.rb's
  task.patterns = ['src/ruby/{lib,spec}/**/*.rb', 'src/ruby/end2end/*.rb']
end

spec = Gem::Specification.load('grpc.gemspec')

Gem::PackageTask.new(spec) do |pkg|
end

# Add the extension compiler task
Rake::ExtensionTask.new('grpc_c', spec) do |ext|
  ext.source_pattern = '**/*.{c,h}'
  ext.ext_dir = File.join('src', 'ruby', 'ext', 'grpc')
  ext.lib_dir = File.join('src', 'ruby', 'lib', 'grpc')
  ext.cross_compile = true
  ext.cross_platform = [
    'x86-mingw32', 'x64-mingw32', 'x64-mingw-ucrt',
    'x86_64-linux-gnu', 'x86_64-linux-musl', 'x86-linux-gnu',
    'x86-linux-musl', 'aarch64-linux-gnu', 'aarch64-linux-musl',
    'x86_64-darwin', 'arm64-darwin'
  ]
  ext.cross_compiling do |spec|
    spec.files = spec.files.select {
      |file| file.start_with?(
        "src/ruby/bin/", "src/ruby/ext/", "src/ruby/lib/", "src/ruby/pb/")
    }
    spec.files += %w( etc/roots.pem grpc_c.32-msvcrt.ruby grpc_c.64-ucrt.ruby )
  end
end

CLEAN.add "src/ruby/lib/grpc/[0-9].[0-9]", "src/ruby/lib/grpc/grpc_c.{bundle,so}"

# Define the test suites
SPEC_SUITES = [
  { id: :wrapper, title: 'wrapper layer', files: %w(src/ruby/spec/*.rb) },
  { id: :idiomatic, title: 'idiomatic layer', dir: %w(src/ruby/spec/generic),
    tags: ['~bidi', '~server'] },
  { id: :bidi, title: 'bidi tests', dir: %w(src/ruby/spec/generic),
    tag: 'bidi' },
  { id: :server, title: 'rpc server thread tests', dir: %w(src/ruby/spec/generic),
    tag: 'server' },
  { id: :pb, title: 'protobuf service tests', dir: %w(src/ruby/spec/pb) }
]
namespace :suite do
  SPEC_SUITES.each do |suite|
    desc "Run all specs in the #{suite[:title]} spec suite"
    RSpec::Core::RakeTask.new(suite[:id]) do |t|
      ENV['COVERAGE_NAME'] = suite[:id].to_s
      spec_files = []
      suite[:files].each { |f| spec_files += Dir[f] } if suite[:files]

      if suite[:dir]
        suite[:dir].each { |f| spec_files += Dir["#{f}/**/*_spec.rb"] }
      end
      helper = 'src/ruby/spec/spec_helper.rb'
      spec_files << helper unless spec_files.include?(helper)

      t.pattern = spec_files
      t.rspec_opts = "--tag #{suite[:tag]}" if suite[:tag]
      if suite[:tags]
        t.rspec_opts = suite[:tags].map { |x| "--tag #{x}" }.join(' ')
      end
    end
  end
end

desc 'Build the Windows gRPC DLLs for Ruby. The argument contains the list of platforms for which to build dll. Empty placeholder files will be created for platforms that were not selected.'
task 'dlls', [:plat] do |t, args|
  grpc_config = ENV['GRPC_CONFIG'] || 'opt'
  verbose = ENV['V'] || '0'
  # use env variable to set artifact build parallelism
  nproc_override = ENV['GRPC_RUBY_BUILD_PROCS'] || `nproc`.strip
  plat_list = args[:plat]

  build_configs = [
    { cross: 'x86_64-w64-mingw32', out: 'grpc_c.64-ucrt.ruby', platform: 'x64-mingw-ucrt' },
    { cross: 'i686-w64-mingw32', out: 'grpc_c.32-msvcrt.ruby', platform: 'x86-mingw32' }
  ]
  selected_build_configs = []
  build_configs.each do |config|
    if plat_list.include?(config[:platform])
      # build the DLL (as grpc_c.*.ruby)
      selected_build_configs.append(config)
    else
      # create an empty grpc_c.*.ruby file as a placeholder
      FileUtils.touch config[:out]
    end
  end

  env = 'CPPFLAGS="-D_WIN32_WINNT=0x600 -DNTDDI_VERSION=0x06000000 -DUNICODE -D_UNICODE -Wno-unused-variable -Wno-unused-result -DCARES_STATICLIB -Wno-error=conversion -Wno-sign-compare -Wno-parentheses -Wno-format -DWIN32_LEAN_AND_MEAN" '
  env += 'CFLAGS="-Wno-incompatible-pointer-types" '
  env += 'CXXFLAGS="-std=c++17 -fno-exceptions" '
  env += 'LDFLAGS=-static '
  env += 'SYSTEM=MINGW32 '
  env += 'EMBED_ZLIB=true '
  env += 'EMBED_OPENSSL=true '
  env += 'BUILDDIR=/tmp '
  env += "V=#{verbose} "
  env += "GRPC_RUBY_BUILD_PROCS=#{nproc_override} "

  out = GrpcBuildConfig::CORE_WINDOWS_DLL

  # propagate env variables with ccache configuration to the rake-compiler-dock docker container
  # and setup ccache symlinks as needed.
  # TODO(jtattermusch): deduplicate creation of prepare_ccache_cmd
  prepare_ccache_cmd = "export GRPC_BUILD_ENABLE_CCACHE=\"#{ENV.fetch('GRPC_BUILD_ENABLE_CCACHE', '')}\" && "
  prepare_ccache_cmd += "export CCACHE_SECONDARY_STORAGE=\"#{ENV.fetch('CCACHE_SECONDARY_STORAGE', '')}\" && "
  prepare_ccache_cmd += "export PATH=\"$PATH:/usr/local/bin\" && "
  prepare_ccache_cmd += "source tools/internal_ci/helper_scripts/prepare_ccache_symlinks_rc "

  selected_build_configs.each do |opt|
    env_comp = "CC=#{opt[:cross]}-gcc "
    env_comp += "CXX=#{opt[:cross]}-g++ "
    env_comp += "LD=#{opt[:cross]}-gcc "
    env_comp += "LDXX=#{opt[:cross]}-g++ "
    run_rake_compiler(opt[:platform], <<~EOT)
      #{prepare_ccache_cmd} && \
      gem update --system --no-document && \
      #{env} #{env_comp} make -j#{nproc_override} #{out} && \
      #{opt[:cross]}-strip -x -S #{out} && \
      cp #{out} #{opt[:out]}
    EOT
  end
end

desc 'Build the native gem file under rake_compiler_dock. Optionally one can pass argument to build only native gem for a chosen platform.'
task 'gem:native', [:plat, :build_type] do |t, args|
  verbose = ENV['V'] || '0'

  grpc_config = ENV['GRPC_CONFIG'] || 'opt'
  target_ruby_minor_versions = ['4.0', '3.4', '3.3', '3.2']
  # For presubmits, only build the earliest and latest versions
  target_ruby_minor_versions = [target_ruby_minor_versions.first, target_ruby_minor_versions.last] if args[:build_type] == 'presubmit'
  selected_plat = "#{args[:plat]}"

  # use env variable to set artifact build parallelism
  nproc_override = ENV['GRPC_RUBY_BUILD_PROCS'] || `nproc`.strip

  # propagate env variables with ccache configuration to the rake-compiler-dock docker container
  # and setup ccache symlinks as needed.
  prepare_ccache_cmd = "export GRPC_BUILD_ENABLE_CCACHE=\"#{ENV.fetch('GRPC_BUILD_ENABLE_CCACHE', '')}\" && "
  prepare_ccache_cmd += "export CCACHE_SECONDARY_STORAGE=\"#{ENV.fetch('CCACHE_SECONDARY_STORAGE', '')}\" && "
  prepare_ccache_cmd += "export PATH=\"$PATH:/usr/local/bin\" && "
  prepare_ccache_cmd += "source tools/internal_ci/helper_scripts/prepare_ccache_symlinks_rc "

  supported_windows_platforms = ['x86-mingw32', 'x64-mingw32', 'x64-mingw-ucrt']
  supported_unix_platforms = [
    'x86_64-linux-gnu', 'x86_64-linux-musl', 'x86-linux-gnu',
    'x86-linux-musl', 'aarch64-linux-gnu', 'aarch64-linux-musl',
    'x86_64-darwin', 'arm64-darwin'
  ]
  supported_platforms = supported_windows_platforms + supported_unix_platforms

  if selected_plat.empty?
    # build everything
    windows_platforms = supported_windows_platforms
    unix_platforms = supported_unix_platforms
  else
    # build only selected platform
    if supported_windows_platforms.include?(selected_plat)
      windows_platforms = [selected_plat]
      unix_platforms = []
    elsif supported_unix_platforms.include?(selected_plat)
      windows_platforms = []
      unix_platforms = [selected_plat]
    else
      fail "Unsupported platform '#{selected_plat}' passed as an argument."
    end
  end

  require 'rake_compiler_dock'

  # Create the windows dlls or create the empty placeholders
  Rake::Task['dlls'].execute(plat: windows_platforms)

  windows_platforms.each do |plat|
    run_rake_compiler(plat, <<~EOT)
      #{prepare_ccache_cmd} && \
      gem update --system --no-document && \
      bundle update --all && \
      bundle exec rake clean && \
      (ccache --show-stats || true) && \
      bundle exec rake native:#{plat} pkg/#{spec.full_name}-#{plat}.gem pkg/#{spec.full_name}.gem \
        RUBY_CC_VERSION=#{RakeCompilerDock.ruby_cc_version(*target_ruby_minor_versions)} \
        V=#{verbose} \
        GRPC_CONFIG=#{grpc_config} \
        GRPC_RUBY_BUILD_PROCS=#{nproc_override} && \
      (ccache --show-stats || true)
    EOT
  end

  # Truncate grpc_c.*.ruby files because they're for Windows only and we don't want
  # them to take up space in the gems that don't target windows.
  File.truncate('grpc_c.32-msvcrt.ruby', 0)
  File.truncate('grpc_c.64-ucrt.ruby', 0)

  `mkdir -p src/ruby/nativedebug/symbols`
  # TODO(apolcyn): make debug symbols work on apple platforms.
  # Currently we hit "objcopy: grpc_c.bundle: file format not recognized"
  # TODO(apolcyn): make debug symbols work on aarch64 linux.
  # Currently we hit "objcopy: Unable to recognise the format of the input file `grpc_c.so'"
  unix_platforms_without_debug_symbols = [
    'x86_64-linux-musl', 'x86-linux-musl',
    'aarch64-linux-gnu', 'aarch64-linux-musl', 'x86_64-darwin',
    'arm64-darwin'
  ]

  unix_platforms.each do |plat|
    if unix_platforms_without_debug_symbols.include?(plat)
      debug_symbols_dir = ''
    else
      debug_symbols_dir = File.join(Dir.pwd, 'src/ruby/nativedebug/symbols')
    end
    makefile_system_override = ''
    if plat =~ /darwin/
      # When cross-compiling c-core for macos from linux, we need to overwrite
      # SYSTEM for our Makefile to work. Note this is not needed for mingw b/c
      # C-core is built in a separate command.
      makefile_system_override = 'Darwin'
    end
    run_rake_compiler(plat, <<~EOT)
      #{prepare_ccache_cmd} && \
      gem update --system --no-document && \
      bundle update --all && \
      bundle exec rake clean && \
      export GRPC_RUBY_DEBUG_SYMBOLS_OUTPUT_DIR=#{debug_symbols_dir} && \
      (ccache --show-stats || true) && \
      bundle exec rake native:#{plat} pkg/#{spec.full_name}-#{plat}.gem pkg/#{spec.full_name}.gem \
        RUBY_CC_VERSION=#{RakeCompilerDock.ruby_cc_version(*target_ruby_minor_versions)} \
        V=#{verbose} \
        GRPC_CONFIG=#{grpc_config} \
        GRPC_RUBY_BUILD_PROCS=#{nproc_override} \
        SYSTEM=#{makefile_system_override} && \
      (ccache --show-stats || true)
    EOT
  end
  # Generate debug symbol packages to complement the native libraries we just built
  unix_platforms.each do |plat|
    unless unix_platforms_without_debug_symbols.include?(plat)
      `bash src/ruby/nativedebug/build_package.sh #{plat}`
      # Native debug gems uploaded to GCS, are copied to ruby-native-debug-symbols for grpc_publish_packages to recognize
      target = 'pkg/ruby-native-debug-symbols'
      FileUtils.mkdir_p(target)
      FileUtils.cp(Dir.glob('src/ruby/nativedebug/pkg/*.gem'), target)
    end
  end
end

desc 'Publish native debug rubygems to GCS'
task 'publish:native_debug', [:gem_dir] do |_t, args|
  require 'digest'
  require 'rubygems/package'
  require 'open3'
  require 'shellwords'

  # Helper to log and execute commands. Usage: run_cmd.call('gcloud', 'storage', 'ls', gcs_base)
  run_cmd = lambda do |*cmd_parts|
    puts "Executing: #{Shellwords.join(cmd_parts)}"
    success = system(*cmd_parts)
    fail "Command failed: #{Shellwords.join(cmd_parts)}" unless success
  end

  gem_dir = File.expand_path(args[:gem_dir] || 'build/ruby/nativedebug')
  force_upload = ENV['REUPLOAD'].to_s.downcase == 'true'
  gcs_bucket = 'gs://packages.grpc.io'
  gcs_base = "#{gcs_bucket}/grpc-ruby-native-debug-symbols"

  fail "Directory '#{gem_dir}' not found" unless Dir.exist?(gem_dir)

  gem_files = Dir["#{gem_dir}/*native-debug*.gem"]
  fail "No native-debug gems found in '#{gem_dir}'" if gem_files.empty?

  puts 'Checking google cloud storage availability and bucket access.'
  run_cmd.call('gcloud', 'storage', 'buckets', 'describe', gcs_bucket)

  gems_by_version = gem_files.group_by do |path|
    full_version = Gem::Package.new(path).spec.version.to_s
    match = full_version.match(/^(\d+\.\d+\.\d+)/)
    fail "Unexpected version format: #{full_version}" unless match
    match[1]
  rescue StandardError => e
    fail "Error: Cannot extract metadata from #{File.basename(path)}. Is it a valid gem? (#{e.message})"
  end

  Dir.chdir(gem_dir) do
    gems_by_version.each do |base_version, version_gem_files|
      puts "Processing base version #{base_version}."

      gcs_version_path = "#{gcs_base}/v#{base_version}"

      # Check only for existence of gems
      stdout, _stderr, status = Open3.capture3('gcloud', 'storage', 'ls', "#{gcs_version_path}/*.gem")
      has_gems = status.success? && !stdout.strip.empty?

      if has_gems && !force_upload
        puts "Skipping v#{base_version}. Gems already exist in #{gcs_version_path}. Use 'REUPLOAD=true' to overwrite"
        next
      end

      if force_upload && has_gems
        puts "Force upload enabled. Clearing existing files in #{gcs_version_path}."
        run_cmd.call('gcloud', 'storage', 'rm', "#{gcs_version_path}/*.gem", "#{gcs_version_path}/checksums.txt")
      end

      begin
        # Generate checksums only for the gems belonging to this version
        File.open('checksums.txt', 'w') do |f|
          version_gem_files.each do |gem_path|
            gem_name = File.basename(gem_path)
            checksum = Digest::SHA256.file(gem_name).hexdigest
            f.puts "#{checksum}  #{gem_name}"
          end
        end

        puts 'Verifying checksums.'
        run_cmd.call('sha256sum', '-c', 'checksums.txt')

        # Upload all gems and the checksums file
        files_to_upload = version_gem_files.map { |f| File.basename(f) } + ['checksums.txt']
        run_cmd.call('gcloud', 'storage', 'cp', *files_to_upload, "#{gcs_version_path}/")
      ensure
        FileUtils.rm_f('checksums.txt')
      end

      puts "Successfully published version #{base_version}."
    end
  end
end

# Define dependencies between the suites.
task 'suite:wrapper' => [:compile, :rubocop]
task 'suite:idiomatic' => 'suite:wrapper'
task 'suite:bidi' => 'suite:wrapper'
task 'suite:server' => 'suite:wrapper'
task 'suite:pb' => 'suite:server'

desc 'Compiles the gRPC extension then runs all the tests'
task all: ['suite:idiomatic', 'suite:bidi', 'suite:pb', 'suite:server']
task default: :all