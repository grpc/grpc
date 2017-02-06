# -*- ruby -*-
require 'rake/extensiontask'
require 'rspec/core/rake_task'
require 'rubocop/rake_task'
require 'bundler/gem_tasks'
require 'fileutils'

require_relative 'build_config.rb'

load 'tools/distrib/docker_for_windows.rb'

# Add rubocop style checking tasks
RuboCop::RakeTask.new(:rubocop) do |task|
  task.options = ['-c', 'src/ruby/.rubocop.yml']
  task.patterns = ['src/ruby/{lib,spec}/**/*.rb']
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
    'x86-mingw32', 'x64-mingw32',
    'x86_64-linux', 'x86-linux',
    'universal-darwin'
  ]
  ext.cross_compiling do |spec|
    spec.files = %w( etc/roots.pem grpc_c.32.ruby grpc_c.64.ruby )
    spec.files += Dir.glob('src/ruby/bin/**/*')
    spec.files += Dir.glob('src/ruby/ext/**/*')
    spec.files += Dir.glob('src/ruby/lib/**/*')
    spec.files += Dir.glob('src/ruby/pb/**/*')
  end
end

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

desc 'Build the Windows gRPC DLLs for Ruby'
task 'dlls' do
  grpc_config = ENV['GRPC_CONFIG'] || 'opt'
  verbose = ENV['V'] || '0'

  env = 'CPPFLAGS="-D_WIN32_WINNT=0x600 -DUNICODE -D_UNICODE -Wno-unused-variable -Wno-unused-result" '
  env += 'LDFLAGS=-static '
  env += 'SYSTEM=MINGW32 '
  env += 'EMBED_ZLIB=true '
  env += 'BUILDDIR=/tmp '
  env += "V=#{verbose} "
  out = GrpcBuildConfig::CORE_WINDOWS_DLL

  w64 = { cross: 'x86_64-w64-mingw32', out: 'grpc_c.64.ruby' }
  w32 = { cross: 'i686-w64-mingw32', out: 'grpc_c.32.ruby' }

  [ w64, w32 ].each do |opt|
    env_comp = "CC=#{opt[:cross]}-gcc "
    env_comp += "LD=#{opt[:cross]}-gcc "
    docker_for_windows "#{env} #{env_comp} make -j #{out} && #{opt[:cross]}-strip -x -S #{out} && cp #{out} #{opt[:out]}"
  end

end

desc 'Build the native gem file under rake_compiler_dock'
task 'gem:native' do
  verbose = ENV['V'] || '0'

  grpc_config = ENV['GRPC_CONFIG'] || 'opt'

  if RUBY_PLATFORM =~ /darwin/
    FileUtils.touch 'grpc_c.32.ruby'
    FileUtils.touch 'grpc_c.64.ruby'
    system "rake cross native gem RUBY_CC_VERSION=2.3.0:2.2.2:2.1.5:2.0.0 V=#{verbose} GRPC_CONFIG=#{grpc_config}"
  else
    Rake::Task['dlls'].execute
    docker_for_windows "bundle && rake cross native gem RUBY_CC_VERSION=2.3.0:2.2.2:2.1.5:2.0.0 V=#{verbose} GRPC_CONFIG=#{grpc_config}"
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
