require "fileutils"
require "rake"
require "digest/sha1"

module RakeCompilerDock
  class << self
    def docker_build_cmd(platform=nil)
      cmd = if ENV['RCD_USE_BUILDX_CACHE']
        if platform
          cache_version = RakeCompilerDock::IMAGE_VERSION.split(".").take(2).join(".")
          cache = File.join("cache", cache_version, platform)
          "docker buildx build --cache-to=type=local,dest=#{cache},mode=max --cache-from=type=local,src=#{cache} --progress=plain"
        else
          return nil
        end
      else
        ENV['RCD_DOCKER_BUILD'] || "docker buildx build --progress=plain"
      end
      Shellwords.split(cmd)
    end

    # Run an intermediate dockerfile without tag
    #
    # The layers will be reused in subsequent builds, even if they run in parallel.
    def docker_build(filename, tag: nil, output: false, platform: )
      cmd = docker_build_cmd(platform)
      return if cmd.nil?
      tag_args = Array(tag).flat_map{|t| ["-t", t] } if tag
      push_args = ["--push"] if output == 'push'
      push_args = ["--load"] if output == 'load'
      Class.new.extend(FileUtils).sh(*cmd, "-f", filename, ".", "--platform", platform, *tag_args, *push_args)
    end
  end

  # Run docker builds in parallel, but ensure that common docker layers are reused
  class ParallelDockerBuild
    include Rake::DSL

    attr_reader :file_deps
    attr_reader :tree_deps
    attr_reader :final_deps

    # All intermediate build nodes, including isolated ones with no tree dependencies.
    # Use this instead of tree_deps when iterating all CI build jobs.
    def all_deps
      file_deps.keys
    end

    def initialize(dockerfiles, workdir: "tmp/docker", inputdir: ".", task_prefix: "common-")
      FileUtils.mkdir_p(workdir)

      files = parse_dockerfiles(dockerfiles, inputdir)
      # pp files

      vcs = find_commons(files)
      # pp vcs

      @file_deps = {}
      @tree_deps = {}
      @final_deps = {}

      write_docker_files(vcs, workdir, task_prefix)
    end

    # Read given dockerfiles from inputdir and split into a list of commands.
    #
    # Returns:
    #   {"File0"=>["      FROM a\n", "      RUN a\n", "      RUN d\n"],
    #    "File1"=>["      FROM a\n",
    #    ...
    def parse_dockerfiles(dockerfiles, inputdir)
      dockerfiles.map do |fn|
        [fn, File.read(File.join(inputdir, fn))]
      end.map do |fn, f|
        # Split file contant in lines unless line ends with backslash
        fc = f.each_line.with_object([]) do |line, lines|
          if lines.last=~/\\\n\z/
            lines.last << line
          else
            lines << line
          end
        end
        [fn, fc]
      end.to_h
    end

    # Build a tree of common parts of given files.
    #
    # Returns:
    #   {["File0", "File1", "File2", "File3"]=>
    #     [["  FROM a\n"],
    #      {["File0", "File1"]=>
    #        [["  RUN a\n", "  RUN d\n"], {["File1"]=>[["  RUN f\n"], {}]}],
    #       ["File2", "File3"]=>[["  RUN b\n", "  RUN c\n", "  RUN d\n"], {}]}]}
    def find_commons(files, vmask=nil, li=0)
      vmask ||= files.keys
      vcs = Hash.new { [] }
      files.each do |fn, lines|
        next unless vmask.include?(fn)
        vcs[lines[li]] += [fn]
      end

      vcs.map do |line, vc|
        next unless line
        nvcs = find_commons(files, vc, li+1)
        if nvcs.first && nvcs.first[0] == vc
          # Append lines that are equal between file(s)
          nl = [[line] + nvcs.first[1][0], nvcs.first[1][1]]
        else
          nl = [[line], nvcs]
        end
        [vc, nl]
      end.compact.to_h
    end

    # Write intermediate dockerfiles to workdir and fill properties
    def write_docker_files(vcs, workdir, task_prefix, plines=[])
      vcs.map do |files, (lines, nvcs)|
        fn = "#{task_prefix}#{Digest::SHA1.hexdigest(files.join)[0, 7]}"
        wfn = File.join(workdir, fn)
        File.write(wfn, (plines + lines).join)
        @file_deps[fn] = wfn

        files.each do |file|
          @final_deps[file] = fn
        end

        nfn = write_docker_files(nvcs, workdir, task_prefix, plines + lines)
        nfn.each do |file|
          @tree_deps[file] = fn
        end
        fn
      end
    end

    # Define rake tasks for building common docker files
    #
    # The rake tasks are named after the dockerfiles given to #new .
    # This also adds dependant intermediate tasks as prerequisites.
    def define_rake_tasks(**build_options)
      define_file_tasks(**build_options)
      define_tree_tasks
      define_final_tasks
    end

    def define_file_tasks(**build_options)
      file_deps.each do |fn, wfn|
        # p file_deps: {fn => wfn}
        task fn do
          RakeCompilerDock.docker_build(wfn, **build_options)
        end
      end
    end

    def define_tree_tasks
      tree_deps.each do |file, prereq|
        # p tree_deps: {file => prereq}
        task file => prereq
      end
    end

    def define_final_tasks
      final_deps.each do |file, prereq|
        # p final_deps: {file => prereq}
        task file => prereq
      end
    end
  end
end
