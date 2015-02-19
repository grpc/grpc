gRPC Ruby
=========

A Ruby implementation of gRPC.

Status
-------

Alpha : Ready for early adopters

INSTALLATION PREREQUISITES
--------------------------

This requires Ruby 2.x, as the RPC API surface uses keyword args.


QUICK - INSTALL
---------------

- Clone this repository.
- Follow the instructions in [INSTALL](../../INSTALL) to install the gRPC C core.
- Use bundler to install
```sh
$ # from this directory
$ gem install bundler && bundle install
```

Installing from source
----------------------

- Build the gRPC C core
E.g, from the root of the gRPC [git repo](https://github.com/google/grpc)
```sh
$ cd ../..
$ make && sudo make install
```

- Install Ruby 2.x. Consider doing this with [RVM](http://rvm.io), it's a nice way of controlling
  the exact ruby version that's used.
```sh
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

- Finally, install the gRPC gem locally.
```sh
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
  the entrypoint gRPC ruby library to be used in a 'require' statement
- spec:
  Rspec unittest
- bin:
  example gRPC clients and servers, e.g,
```ruby
stub = Math::Math::Stub.new('my.test.math.server.com:8080')
req = Math::DivArgs.new(dividend: 7, divisor: 3)
logger.info("div(7/3): req=#{req.inspect}")
resp = stub.div(req)
logger.info("Answer: #{resp.inspect}")
```
