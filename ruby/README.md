gRPC in 3 minutes (Ruby)
========================

PREREQUISITES
-------------

This requires Ruby 2.1, as the gRPC API surface uses keyword args.

INSTALL
-------

- Clone this repository.
- Follow the instructions in [INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL) to install the gRPC C core.
- *Temporary* Install the full gRPC distribution from source on your local machine and update path: in [Gemfile](https://github.com/grpc/grpc-common/blob/master/ruby/Gemfile) to refer src/ruby within it.
  - this is necessary until the gRPC ruby gem is published
- Use bundler to install
```sh
$ # from this directory
$ gem install bundler && bundle install
```

Try it! 
-------

- Run the server
```sh
$ # from this directory
$ bundle exec ./greeter_server.rb &
```

- Run the client
```sh
$ # from this directory
$ bundle exec ./greeter_client.rb
```
