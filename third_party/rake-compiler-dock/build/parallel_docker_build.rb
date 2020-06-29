require "fileutils"
require "rake"

module RakeCompilerDock
  # Run docker builds in parallel, but ensure that common docker layers are reused
  class ParallelDockerBuild
    include Rake::DSL

    def initialize(dockerfiles, workdir: "tmp/docker", inputdir: ".", task_prefix: "common-")
      FileUtils.mkdir_p(workdir)

      files = parse_dockerfiles(dockerfiles, inputdir)
      # pp files

      vcs = find_commons(files)
      # pp vcs

      define_common_tasks(vcs, workdir, task_prefix)
    end

    # Read given dockerfiles from inputdir and split into a list of commands.
    #
    # Returns:
    #   {"File0"=>["      FROM a\n", "      RUN a\n", "      RUN d\n"],
    #    "File1"=>["      FROM a\n",
    #    ...
    def parse_dockerfiles(dockerfiles, inputdir)
      files = dockerfiles.map do |fn|
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

    # Write intermediate dockerfiles to workdir and define rake tasks
    #
    # The rake tasks are named after the dockerfiles given to #new .
    # This also adds dependant intermediate tasks as prerequisites.
    def define_common_tasks(vcs, workdir, task_prefix, plines=[])
      vcs.map do |files, (lines, nvcs)|
        fn = "#{task_prefix}#{files.join}"
        File.write(File.join(workdir, fn), (plines + lines).join)
        task fn do
          docker_build(fn, workdir)
        end

        nfn = define_common_tasks(nvcs, workdir, task_prefix, plines + lines)
        nfn.each do |file|
          task file => fn
        end
        files.each do |file|
          task file => fn
        end
        fn
      end
    end

    # Run an intermediate dockerfile without tag
    #
    # The layers will be reused in subsequent builds, even if they run in parallel.
    def docker_build(filename, workdir)
      sh "docker", "build", "-f", File.join(workdir, filename), "."
    end
  end
end
