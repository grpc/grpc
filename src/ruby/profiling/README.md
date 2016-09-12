Profiling Ruby gRPC
===================
The "Hello World" client and server here are set up to profile the ruby code in ruby-gRPC that is in the repo in src/ruby.
It uses the sampling RubyProf profiler for ruby.

With RubyProf, this can profile cpu time, wall time, memory allocation counts, memory allocation totals, and more.
(for memory-related profiling, it requires a patched version of ruby).
See https://github.com/ruby-prof/ruby-prof for more detailed usage of RubyProf.

Example Usage
-------------

After running `bundle install` form the root of the repo:

Start the server:
```
$ ruby greeter_server.rb
```

In a separate terminal, start the client: (profiling WALL_TIME and saving html results into results.html here)
```
$ ruby greeter_client.rb -m WALL_TIME > results.html
```

Results can be viewed in results.html

Note that the server process can also be profiled in the same way. For example:
```
$ ruby greeter_server.rb -m CPU_TIME > server_results.html
$ Ctrl^C
```


