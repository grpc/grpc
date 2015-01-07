Ruby for GRPC
=============

LAYOUT
------

Directory structure is the recommended layout for [ruby extensions](http://guides.rubygems.org/gems-with-extensions/)

 * ext: the extension code
 * lib: the entrypoint grpc ruby library to be used in a 'require' statement
 * test: tests


DEPENDENCIES
------------


* Extension

The extension can be built and tested using
[rake](https://rubygems.org/gems/rake).  However, the rake-extensiontask rule
is not supported on older versions of rubygems, and the necessary version of
rubygems is not available on the latest version of Goobuntu.

This is resolved by using [RVM](https://rvm.io/) instead; install a single-user
ruby environment, and develop on the latest stable version of ruby (2.1.2).


* Proto code generation

To build generate service stubs and skeletons, it's currently necessary to use
a patched version of a beefcake, a simple third-party proto2 library.  This is
feature compatible with proto3 and will be replaced by official proto3 support
in protoc.

* Patched protoc

The patched version of beefcake in turn depends on a patched version of protoc.
This is an update of the latest open source release of protoc with some forward
looking proto3 patches.


INSTALLATION PREREQUISITES
--------------------------

Install the patched protoc

$ cd <git_repo_dir>
$ git clone sso://team/one-platform-grpc-team/protobuf
$ cd protobuf
$ ./configure --prefix=/usr
$ make
$ sudo make install

Install an update to OpenSSL with ALPN support

$ wget https://www.openssl.org/source/openssl-1.0.2-beta3.tar.gz
$ tar -zxvf openssl-1.0.2-beta3.tar.gz
$ cd openssl-1.0.2-beta3
$ ./config shared
$ make
$ sudo make install

Install RVM

$ # the -with-openssl-dir ensures that ruby uses the updated version of SSL
$ command curl -sSL https://rvm.io/mpapis.asc | gpg --import -
$ \curl -sSL https://get.rvm.io | bash -s stable --ruby
$
$ # follow the instructions to ensure that your're using the latest stable version of Ruby
$ # and that the rvm command is installed
$
$ rvm reinstall 2.1.5 --with-openssl-dir=/usr/local/ssl
$ gem install bundler  # install bundler, the standard ruby package manager

Install the patched beefcake, and update the Gemfile to reference

$ cd <git_repo_dir>
$ git clone sso://team/one-platform-grpc-team/grpc-ruby-beefcake beefcake
$ cd beefcake
$ bundle install
$

HACKING
-------

The extension can be built and tested using the Rakefile.

$ # create a workspace
$ git5 start <your-git5-branch> net/grpc
$
$ # build the C library and install it in $HOME/grpc_dev
$ <google3>/net/grpc/c/build_gyp/build_grpc_dev.sh
$
$ # build the ruby extension and test it.
$ cd google3_dir/net/grpc/ruby
$ rake

Finally, install grpc ruby locally.

$ cd <this_dir>
$
$ # update the Gemfile, modify the line beginning # gem 'beefcake' to refer to
$ # the patched beefcake dir
$
$ bundle install
