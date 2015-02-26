gRPC in 3 minutes (Ruby)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto](https://github.com/grpc/grpc-common/blob/master/protos/helloworld.proto). 

PREREQUISITES
-------------

This requires Ruby 2.1, as the gRPC API surface uses keyword args.
If you don't have that installed locally, you can use [RVM](https://www.rvm.io/) to use Ruby 2.1 for testing without upgrading the version of Ruby on your whole system.
RVM is also useful if you don't have the necessary privileges to update your system's Ruby.
```sh
$ # RVM installation as specified at https://rvm.io/rvm/install
$ gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
$ \curl -sSL https://get.rvm.io | bash -s stable --ruby=ruby-2.1
$
$ # follow the instructions to ensure that your're using the latest stable version of Ruby
$ # and that the rvm command is installed
```
- *N.B* Make sure your run `source $HOME/.rvm/scripts/rvm` as instructed to complete the set-up of RVM.

INSTALL
-------

- Clone this repository.
- Follow the instructions in [INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL) to install the gRPC C core.
- *Temporary* 
  - Install the full gRPC distribution from source on your local machine
  - Build gRPC Ruby as described in [installing from source](https://github.com/grpc/grpc/blob/master/src/ruby/README.md#installing-from-source)
  - update `path:` in [Gemfile](https://github.com/grpc/grpc-common/blob/master/ruby/Gemfile) to refer to src/ruby within the gRPC directory
  - N.B: these steps are necessary until the gRPC ruby gem is published
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
