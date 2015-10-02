gRPC in 3 minutes (Ruby)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto][]

PREREQUISITES
-------------

- Ruby 2.x
This requires Ruby 2.x, as the gRPC API surface uses keyword args.
If you don't have that installed locally, you can use [RVM][] to use Ruby 2.x for testing without upgrading the version of Ruby on your whole system.
RVM is also useful if you don't have the necessary privileges to update your system's Ruby.

  ```sh
  $ # RVM installation as specified at https://rvm.io/rvm/install
  $ gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
  $ \curl -sSL https://get.rvm.io | bash -s stable --ruby=ruby-2
  $
  $ # follow the instructions to ensure that your're using the latest stable version of Ruby
  $ # and that the rvm command is installed
  ```
- *N.B* Make sure your run `source $HOME/.rvm/scripts/rvm` as instructed to complete the set-up of RVM.

INSTALL
-------
- [Install gRPC Ruby][]

- Use bundler to install the example package's dependencies

  ```sh
  $ # from this directory
  $ gem install bundler # if you don't already have bundler available
  $ bundle install
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

Tutorial
--------

You can find a more detailed tutorial in [gRPC Basics: Ruby][]

[helloworld.proto]:../protos/helloworld.proto
[RVM]:https://www.rvm.io/
[Install gRPC ruby]:../../src/ruby#installation
[gRPC Basics: Ruby]:http://www.grpc.io/docs/tutorials/basic/ruby.html
