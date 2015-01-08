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
rubygems.

This is resolved by using [RVM](https://rvm.io/) instead; install a single-user
ruby environment, and develop on the latest stable version of ruby (2.1.5).


INSTALLATION PREREQUISITES
--------------------------

Install RVM

$ command curl -sSL https://rvm.io/mpapis.asc | gpg --import -
$ \curl -sSL https://get.rvm.io | bash -s stable --ruby
$
$ # follow the instructions to ensure that your're using the latest stable version of Ruby
$ # and that the rvm command is installed
$
$ gem install bundler  # install bundler, the standard ruby package manager

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
