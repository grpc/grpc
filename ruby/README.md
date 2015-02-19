gRPC Ruby Helloworld
====================

INSTALLATION PREREQUISITES
--------------------------

This requires Ruby 2.x, as the gRPC API surface uses keyword args.

INSTALL
-------

- Clone this repository.
- Follow the instructions in [INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL) to install the gRPC C core.
- *Temporary* Install gRPC for Ruby from source on your local machine and update path: to refer to it [Gemfile].
  - this is needed until the gRPC ruby gem is published
- Use bundler to install
```sh
$ # from this directory
$ gem install bundler && bundle install
```

USAGE
-----

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
