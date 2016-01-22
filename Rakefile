# -*- ruby -*-
require 'rake/extensiontask'
require 'rspec/core/rake_task'
require 'rubocop/rake_task'
require 'bundler/gem_tasks'
require 'fileutils'
require 'rbconfig'
require 'grpc/version'

LIB_DIR = File.join('src', 'ruby', 'lib', 'grpc')

# Add rubocop style checking tasks
RuboCop::RakeTask.new(:rubocop) do |task|
  task.options = ['-c', 'src/ruby/.rubocop.yml']
  task.patterns = ['src/ruby/{lib,spec}/**/*.rb']
end

# Add the extension compiler task
Rake::ExtensionTask.new 'grpc' do |ext|
  ext.source_pattern = '**/*.{c,h}'
  ext.ext_dir = File.join('src', 'ruby', 'ext', 'grpc')
  ext.lib_dir = LIB_DIR
end

task :stage_publish_binary => :compile do
  abi = "#{RbConfig::CONFIG['MAJOR']}.#{RbConfig::CONFIG['MINOR']}.0"
  local_platform = Gem::Platform.local
  ext = RbConfig::CONFIG['DLEXT']
  remote_path = "grpc-precompiled-binaries/ruby/grpc/v#{GRPC::VERSION}"
  remote_name = "#{abi}-#{RbConfig::CONFIG['target_os']}-#{RbConfig::CONFIG['target_cpu']}.#{ext}"
  local_path = File.expand_path(File.join(File.dirname(__FILE__), LIB_DIR, "grpc.#{ext}"))
  publish_stage = File.join(File.dirname(__FILE__), 'tmp', remote_path, remote_name)

  print "Copying binary file from #{local_path} to #{publish_stage}\n"
  FileUtils.mkdir_p(File.dirname(publish_stage))
  FileUtils.copy_file(local_path, publish_stage)
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

# Define dependencies between the suites.
task 'suite:wrapper' => [:compile, :rubocop]
task 'suite:idiomatic' => 'suite:wrapper'
task 'suite:bidi' => 'suite:wrapper'
task 'suite:server' => 'suite:wrapper'
task 'suite:pb' => 'suite:server'

desc 'Compiles the gRPC extension then runs all the tests'
task all: ['suite:idiomatic', 'suite:bidi', 'suite:pb', 'suite:server']
task default: :all
