[![Gem](https://img.shields.io/gem/v/grpc.svg)](https://rubygems.org/gems/grpc/)
gRPC Ruby
=========

A Ruby implementation of gRPC.

Status
------

Beta

PREREQUISITES
-------------

- Ruby 2.x. The gRPC API uses keyword args.
- [homebrew][] on Mac OS X.  These simplify the installation of the gRPC C core.

INSTALLATION
---------------

**Linux (Debian):**

Add [Debian jessie-backports][] to your `sources.list` file. Example:

```sh
echo "deb http://http.debian.net/debian jessie-backports main" | \
sudo tee -a /etc/apt/sources.list
```

Install the gRPC Debian package

```sh
sudo apt-get update
sudo apt-get install libgrpc-dev
```

Install the gRPC Ruby package

```sh
gem install grpc
```

**Mac OS X**

Install [homebrew][]. Run the following command to install gRPC Ruby.
```sh
$ curl -fsSL https://goo.gl/getgrpc | bash -s ruby
```
This will download and run the [gRPC install script][], then install the latest version of gRPC Ruby gem.  It also installs Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin for ruby.

BUILD FROM SOURCE
---------------------
- Clone this repository

- Install Ruby 2.x. Consider doing this with [RVM](http://rvm.io), it's a nice way of controlling
  the exact ruby version that's used.
```sh
$ command curl -sSL https://rvm.io/mpapis.asc | gpg --import -
$ \curl -sSL https://get.rvm.io | bash -s stable --ruby=ruby-2
$
$ # follow the instructions to ensure that your're using the latest stable version of Ruby
$ # and that the rvm command is installed
```
- Make sure your run `source $HOME/.rvm/scripts/rvm` as instructed to complete the set up of RVM

- Install [bundler](http://bundler.io/)
```
$ gem install bundler
```

- Finally,  build and install the gRPC gem locally.
```sh
$ # from this directory
$ bundle install  # creates the ruby bundle, including building the grpc extension
$ rake  # runs the unit tests, see rake -T for other options
```

DOCUMENTATION
-------------
- rubydoc for the gRPC gem is available online at [rubydoc][].
- the gRPC Ruby reference documentation is available online at [grpc.io][]

CONTENTS
--------
Directory structure is the layout for [ruby extensions][]
- ext: the gRPC ruby extension
- lib: the entrypoint gRPC ruby library to be used in a 'require' statement
- spec: Rspec unittests
- bin: example gRPC clients and servers, e.g,

  ```ruby
  stub = Math::Math::Stub.new('my.test.math.server.com:8080')
  req = Math::DivArgs.new(dividend: 7, divisor: 3)
  GRPC.logger.info("div(7/3): req=#{req.inspect}")
  resp = stub.div(req)
  GRPC.logger.info("Answer: #{resp.inspect}")
  ```
[homebrew]:http://brew.sh
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[ruby extensions]:http://guides.rubygems.org/gems-with-extensions/
[rubydoc]: http://www.rubydoc.info/gems/grpc
[grpc.io]: http://www.grpc.io/docs/installation/ruby.html
[Debian jessie-backports]:http://backports.debian.org/Instructions/
