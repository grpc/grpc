
# Overview

This directory contains source code for PHP implementation of gRPC layered on
shared C library. The same installation guides with more examples and
tutorials can be seen at [grpc.io](https://grpc.io/docs/quickstart/php.html).
gRPC PHP installation instructions for Google Cloud Platform is in
[cloud.google.com](https://cloud.google.com/php/grpc).

## Environment

###Prerequisite:
* `php` 5.5 or above, 7.0 or above
* `pecl`
* `composer`
* `phpunit` (optional)

**Install PHP and PECL on Ubuntu/Debian:**

For PHP5:

```sh
$ sudo apt-get install php5 php5-dev php-pear phpunit
```

For PHP7:

```sh
$ sudo apt-get install php7.0 php7.0-dev php-pear phpunit
```
or
```sh
$ sudo apt-get install php php-dev php-pear phpunit
```

**Install PHP and PECL on CentOS/RHEL 7:**
```sh
$ sudo rpm -Uvh https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
$ sudo rpm -Uvh https://mirror.webtatic.com/yum/el7/webtatic-release.rpm
$ sudo yum install php56w php56w-devel php-pear phpunit gcc zlib-devel
```

**Install PHP and PECL on Mac:**
```sh
$ brew install homebrew/php/php56-grpc
$ curl -O http://pear.php.net/go-pear.phar
$ sudo php -d detect_unicode=0 go-pear.phar
```

**Install Composer (Linux or Mac):**
```sh
$ curl -sS https://getcomposer.org/installer | php
$ sudo mv composer.phar /usr/local/bin/composer
```

**Install PHPUnit (Linux or Mac):**
```sh
$ wget https://phar.phpunit.de/phpunit-old.phar
$ chmod +x phpunit-old.phar
$ sudo mv phpunit-old.phar /usr/bin/phpunit
```

## Install the gRPC PHP extension

There are two ways to install gRPC PHP extension.
* `pecl`
* `build from source`

### Using PECL

```sh
sudo pecl install grpc
```

or specific version

```sh
sudo pecl install grpc-1.12.0
```

Note: for users on CentOS/RHEL 6, unfortunately this step won’t work. 
Please follow the instructions below to compile the PECL extension from source.

#### Install on Windows

You can download the pre-compiled gRPC extension from the PECL
[website](https://pecl.php.net/package/grpc)

### Build from Source with gRPC C core library

Clone this repository

```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
```

#### Build and install the gRPC C core library

```sh
$ cd grpc
$ git submodule update --init
$ make
$ sudo make install
```

#### Build and install gRPC PHP extension

Prerequisite for Max OSX:
```
# You can follow this document where problems have been encountered
# https://github.com/kevinabtasty/guides-how-to/blob/master/how-to-install-grpc-php-macosx.md

# Install xcode dev tools (error php.h missing)
$ xcode-select --install
# Install missing libraries with homebrew (error aclocal command not found)
$ brew install autoconf automake libtool
$ brew doctor
$ brew prune
```

Compile the gRPC PHP extension

```sh
$ cd grpc/src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ sudo make install
```

This will compile and install the gRPC PHP extension into the 
standard PHP extension directory. You should be able to run 
the [unit tests](#unit-tests), with the PHP extension installed.


### Update php.ini

After installing the gRPC extension, make sure you add this line 
to your `php.ini` file, (e.g. `/etc/php5/cli/php.ini`, 
`/etc/php5/apache2/php.ini`, or `/usr/local/etc/php/5.6/php.ini`), 
depending on where your PHP installation is.

```sh
extension=grpc.so
# Or link the absolute link path (on mac if it is not installed in the right folder)
# extension=/Users/toto/tmp/grpc/src/php/ext/grpc/modules/grpc.so
```

**Add the gRPC PHP library as a Composer dependency**

You need to add this to your project's `composer.json` file.

```
  "require": {
    "grpc/grpc": "v1.12.0"
  }
```

To run tests with generated stub code from `.proto` files, you will also 
need the `composer` and `protoc` binaries. You can find out how to get these below.

## Install other prerequisites for both Mac OS X and Linux

* `protoc: protobuf compiler`
* `protobuf.so: protobuf runtime library`
* `grpc_php_plugin: Generates PHP gRPC service interface out of Protobuf IDL`

### Install Protobuf compiler

If you don't have it already, you need to install the protobuf compiler
`protoc`, version 3.5.0+ (the newer the better) for the current gRPC version.
If you installed already, make the protobuf version is compatible to the 
grpc version you installed. If you build grpc.so from the souce, you can check
the version of grpc inside package.xml file.

The compatibility between the grpc and protobuf version is listed as table below:

grpc | protobuf
--- | --- 
v1.0.0 | 3.0.0(GA)
v1.0.1 | 3.0.2
v1.1.0 | 3.1.0 
v1.2.0 | 3.2.0 
v1.2.0 | 3.2.0 
v1.3.4 | 3.3.0 
v1.3.5 | 3.2.0
v1.4.0 | 3.3.0 
v1.6.0 | 3.4.0
v1.8.0 | 3.5.0
v1.12.0 | 3.5.2

If `protoc` hasn't been installed, you can download the `protoc` binaries from
[the protocol buffers Github repository](https://github.com/google/protobuf/releases).
Then unzip this file and update the environment variable `PATH` to include the path to 
the protoc binary file.

If you really must compile `protoc` from source, you can run the following
commands, but this is risky because there is no easy way to uninstall /
upgrade to a newer release.

```sh
$ cd grpc/third_party/protobuf
$ ./autogen.sh && ./configure && make
$ sudo make install
```

### Protobuf Runtime library

There are two protobuf runtime libraries to choose from. They are identical
in terms of APIs offered. The C implementation provides better performance, 
while the native implementation is easier to install. Make sure the installed 
protobuf version works with grpc version.

#### 1. C implementation (for better performance)

``` sh
$ sudo pecl install protobuf
```
or specific version

``` sh
$ sudo pecl install protobuf-3.5.1.1
```

Add this to your `php.ini` file:

```sh
extension=protobuf.so
```

#### 2. PHP implementation (for easier installation)

Add this to your `composer.json` file:

```
  "require": {
    "google/protobuf": "^v3.5.0"
  }
```

### PHP Protoc Plugin

You need the gRPC PHP protoc plugin to generate the client stub classes.
It can generate server and client code from .proto service definitions.

It should already been compiled when you run `make` from the root directory
of this repo. The plugin can be found in the `bins/opt` directory. We are
planning to provide a better way to download and install the plugin
in the future.

You can also just build the gRPC PHP protoc plugin by running:

```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
$ cd grpc
$ git submodule update --init
$ make grpc_php_plugin
```

Plugin may use the new feature of the new protobuf version, thus please also
make sure that the protobuf version installed is compatible with the grpc version 
you build this plugin.

## Unit Tests

You will need the source code to run tests

```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
$ cd grpc
$ git submodule update --init
```

Run unit tests

```sh
$ cd grpc/src/php
$ ./bin/run_tests.sh
```

## Generated Code Tests

This section specifies the prerequisites for running the generated code tests,
as well as how to run the tests themselves.

### Composer

Install the runtime dependencies via `composer install`.

```sh
$ cd grpc/src/php
$ composer install
```


### Client Stub

Generate client stub classes from `.proto` files

```sh
$ cd grpc/src/php
$ ./bin/generate_proto_php.sh
```

### Run test server

Run a local server serving the math services. Please see [Node][] for how to
run an example server.

```sh
$ cd grpc
$ npm install
$ node src/node/test/math/math_server.js
```

### Run test client

Run the generated code tests

```sh
$ cd grpc/src/php
$ ./bin/run_gen_code_test.sh
```

## Use the gRPC PHP extension with Apache

Install `apache2`, in addition to `php5` above

```sh
$ sudo apt-get install apache2
```

Add this line to your `php.ini` file, e.g. `/etc/php5/apache2/php.ini`
or `/etc/php/7.0/apache2/php.ini`

```sh
extension=grpc.so
```

Restart apache

```sh
$ sudo service apache2 restart
```

Make sure the Node math server is still running, as above. 

```sh
$ cd grpc
$ npm install
$ node src/node/test/math/math_server.js
```

Make sure you have run `composer install` to generate the `vendor/autoload.php` file

```sh
$ cd grpc/src/php
$ composer install
```

Make sure you have generated the client stubs

```sh
$ ./bin/generate_proto_php.sh
```

Copy the `math_client.php` file into your Apache document root, e.g.

```sh
$ cp tests/generated_code/math_client.php /var/www/html
```

You may have to fix the first line to point the includes to your installation:

```php
include 'vendor/autoload.php';
```

Connect to `localhost/math_client.php` in your browser, or run this from command line:

```sh
$ curl localhost/math_client.php
```

## Use the gRPC PHP extension with Nginx/PHP-FPM

Install `nginx` and `php5-fpm`, in addition to `php5` above

```sh
$ sudo apt-get install nginx php5-fpm

OR

$ sudo apt-get install nginx php7.0-fpm
```

Add this line to your `php.ini` file, e.g. `/etc/php5/fpm/php.ini`

```sh
extension=grpc.so
```

Uncomment the following lines in your `/etc/nginx/sites-available/default` file:

```
location ~ \.php$ {
    include snippets/fastcgi-php.conf;
    fastcgi_pass unix:/var/run/php5-fpm.sock;
}
```

Restart nginx and php-fpm

```sh
$ sudo service nginx restart
$ sudo service php5-fpm restart
```

Make sure the Node math server is still running, as above. 

```sh
$ cd grpc
$ npm install
$ node src/node/test/math/math_server.js
```

Make sure you have run `composer install` to generate the `vendor/autoload.php` file

```sh
$ cd grpc/src/php
$ composer install
```

Make sure you have generated the client stubs

```sh
$ ./bin/generate_proto_php.sh
```

Copy the `math_client.php` file into your Nginx document root, e.g.

```sh
$ cp tests/generated_code/math_client.php /var/www/html
```

You may have to fix the first line to point the includes to your installation:

```php
include 'vendor/autoload.php';
```

Connect to `localhost/math_client.php` in your browser, or run this from command line:

```sh
$ curl localhost/math_client.php
```

[Node]:https://github.com/grpc/grpc/tree/master/src/node/examples
