#! /usr/bin/env ruby

require "fileutils"

Dir["/usr/bin/x86_64-linux-gnu-*"].each do |file|
  case File.basename(file)
  when /^x86_64-linux-gnu-(gcc|g\+\+|ld)$/
    e = $1
    puts "create /usr/bin/i686-linux-gnu-#{e}"
    File.write("/usr/bin/i686-linux-gnu-#{e}", <<-EOT)
#!/bin/sh
x86_64-linux-gnu-#{e} -m32 "$@"
    EOT
    File.chmod(0755, "/usr/bin/i686-linux-gnu-#{e}")
  when /^x86_64-linux-gnu-([a-z+\.]+)$/
    FileUtils.ln_s(file, "/usr/bin/i686-linux-gnu-#{$1}", verbose: true)
  end
end
