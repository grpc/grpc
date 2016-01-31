#!/usr/bin/env ruby

def grpc_root()
  File.expand_path(File.join(File.dirname(__FILE__), '..', '..'))
end

def docker_for_windows_image()
  require 'digest'

  dockerfile = File.join(grpc_root, 'third_party', 'rake-compiler-dock', 'Dockerfile') 
  dockerpath = File.dirname(dockerfile)
  version = Digest::SHA1.file(dockerfile).hexdigest
  image_name = 'grpc/rake-compiler-dock:' + version
  cmd = "docker build -t #{image_name} --file #{dockerfile} #{dockerpath}"
  puts cmd
  system cmd
  raise "Failed to build the docker image." unless $? == 0
  image_name
end

def docker_for_windows(args)
  require 'rake_compiler_dock'

  args = 'bash -l' if args.empty?

  ENV['RAKE_COMPILER_DOCK_IMAGE'] = docker_for_windows_image

  RakeCompilerDock.sh args
end

if __FILE__ == $0
  docker_for_windows $*.join(' ')
end
