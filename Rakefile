# -*- ruby -*-
require 'rake/extensiontask'
require 'rspec/core/rake_task'
require 'rubocop/rake_task'
require 'bundler/gem_tasks'

# Add rubocop style checking tasks
RuboCop::RakeTask.new(:rubocop) do |task|
  task.options = ['-c', 'src/ruby/.rubocop.yml']
  task.patterns = ['src/ruby/{lib,spec}/**/*.rb']
end

spec = Gem::Specification.load('grpc.gemspec')

Gem::PackageTask.new(spec) do |pkg|
end

# Add the extension compiler task
Rake::ExtensionTask.new('grpc', spec) do |ext|
  ext.source_pattern = '**/*.{c,h}'
  ext.ext_dir = File.join('src', 'ruby', 'ext', 'grpc')
  ext.lib_dir = File.join('src', 'ruby', 'lib', 'grpc')
  ext.cross_compile = true
  ext.cross_platform = ['x86-mingw32', 'x64-mingw32']
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

desc 'Build the gem file under rake_compiler_dock'
task 'gem:windows' do
  require 'digest'
  require 'rake_compiler_dock'

  grpc_config = ENV['GRPC_CONFIG'] || 'opt'
  V = ENV['V'] || '0'

  version = Digest::SHA1.file('third_party/rake-compiler-dock/Dockerfile').hexdigest
  image_name = 'grpc/rake-compiler-dock:' + version
  cmd = "docker build -t #{image_name} third_party/rake-compiler-dock"
  puts cmd
  system cmd
  exit 1 unless $? == 0
  ENV['RAKE_COMPILER_DOCK_IMAGE'] = image_name
  RakeCompilerDock.sh "bundle && rake cross native gem RUBY_CC_VERSION=2.3.0:2.2.2:2.1.6:2.0.0 GRPC_CONFIG=#{grpc_config} V=#{V}"
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
