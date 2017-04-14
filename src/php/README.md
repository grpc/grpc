
# Overview

This directory contains source code for PHP implementation of gRPC layered on
shared C library.

## Environment

**Prerequisite:**
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

**Install PHP and PECL on CentOS/RHEL 7:**
```sh
$ sudo rpm -Uvh https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
$ sudo rpm -Uvh https://mirror.webtatic.com/yum/el7/webtatic-release.rpm
$ sudo yum install php56w php56w-devel php-pear phpunit gcc zlib-devel
```

**Install PECL on Mac:**
```sh
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

## Quick Install

**Install the gRPC PHP extension**

```sh
sudo pecl install grpc
```

This will compile and install the gRPC PHP extension into the standard PHP
extension directory. You should be able to run the [unit tests](#unit-tests),
with the PHP extension installed.

Note: For users on CentOS/RHEL 6, unfortunately this step won't work. Please
follow the instructions below to compile the extension from source.


**Update php.ini**

Add this line to your `php.ini` file, e.g. `/etc/php5/cli/php.ini`

```sh
extension=grpc.so
```


**Add the gRPC PHP library as a Composer dependency**

You need to add this to your project's `composer.json` file.

```
  "require": {
    "grpc/grpc": "v1.1.0"
  }
```

To run tests with generated stub code from `.proto` files, you will also need
the `composer` and `protoc` binaries. You can find out how to get these
[below](#generated-code-tests).


## Build from Source


### gRPC C core library

Clone this repository

```sh
$ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
```

Build and install the gRPC C core library

```sh
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
$ make
$ sudo make install
```

### gRPC PHP extension

Compile the gRPC PHP extension

```sh
$ cd grpc/src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ sudo make install
```

## Unit Tests

You will need the source code to run tests

```sh
$ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
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

### Protobuf compiler

Again if you don't have it already, you need to install the protobuf compiler
`protoc`, version 3.1.0+ (the newer the better).

If `protoc` hasn't been installed, you can download the `protoc` binaries from
[the protocol buffers Github repository](https://github.com/google/protobuf/releases).

If you really must compile `protoc` from source, you can run the following
commands, but this is risky because there is no easy way to uninstall /
upgrade to a newer release.

```sh
$ cd grpc/third_party/protobuf
$ ./autogen.sh && ./configure && make
$ sudo make install
```


### PHP Protoc Plugin

You need the gRPC PHP protoc plugin to generate the client stub classes.

It should already been compiled when you run `make` from the root directory
of this repo. The plugin can be found in the `bins/opt` directory. We are
planning to provide a better way to download and install the plugin
in the future.

You can also just build the gRPC PHP protoc plugin by running:

```sh
$ cd grpc
$ make grpc_php_plugin
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
