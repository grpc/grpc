gRPC Ruby
=========

A Ruby implementation of gRPC, Google's RPC library.

Status
-------

Alpha : Ready for early adopters

INSTALLATION PREREQUISITES
--------------------------

This requires Ruby 2.x, as the rpc api surface uses keyword args.


INSTALLING
----------

- Install the gRPC core library
  TODO: describe this, once the core distribution mechanism is defined.
```
$ gem install grpc
```


Installing from source
----------------------

- Build or Install the gRPC core
E.g, from the root of the grpc [git repo](https://github.com/google/grpc)
```
$ cd ../..
$ make && sudo make install
```

- Install Ruby 2.x. Consider doing this with [RVM](http://rvm.io), it's a nice way of controlling
  the exact ruby version that's used.
```
$ command curl -sSL https://rvm.io/mpapis.asc | gpg --import -
$ \curl -sSL https://get.rvm.io | bash -s stable --ruby
$
$ # follow the instructions to ensure that your're using the latest stable version of Ruby
$ # and that the rvm command is installed
```

- Install [bundler](http://bundler.io/)
```
$ gem install bundler
```

- Finally, install grpc ruby locally.
```
$ cd <install_dir>
$ bundle install
$ rake  # compiles the extension, runs the unit tests, see rake -T for other options
```

CONTENTS
--------

Directory structure is the layout for [ruby extensions](http://guides.rubygems.org/gems-with-extensions/)

- ext:
  the gRPC ruby extension
- lib:
  the entrypoint grpc ruby library to be used in a 'require' statement
- spec:
  Rspec unittest
- bin:
  example gRPC clients and servers, e.g,
```ruby
stub = Math::Math::Stub.new('my.test.math.server.com:8080')
req = Math::DivArgs.new(dividend: 7, divisor: 3)
logger.info("div(7/3): req=#{req.inspect}")
resp = stub.div(req, INFINITE_FUTURE)
logger.info("Answer: #{resp.inspect}")
```
