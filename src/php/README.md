
#Overview

This directory contains source code for PHP implementation of gRPC layered on shared C library.

#Status

Beta

## Environment

Prerequisite: `php` >=5.5, `phpize`, `pecl`, `phpunit`

**Linux (Debian):**

```sh
$ sudo apt-get install php5 php5-dev php-pear
```

**Linux (CentOS):**

```sh
$ yum install php55w
$ yum --enablerepo=remi,remi-php55 install php-devel php-pear
```

**Mac OS X:**

```sh
$ curl -O http://pear.php.net/go-pear.phar
$ sudo php -d detect_unicode=0 go-pear.phar
```

**PHPUnit:**
```sh
$ wget https://phar.phpunit.de/phpunit-old.phar
$ chmod +x phpunit-old.phar
$ sudo mv phpunit-old.phar /usr/bin/phpunit
```

## Quick Install

Install the gRPC PHP extension

```sh
sudo pecl install grpc-beta
```

This will compile and install the gRPC PHP extension into the standard PHP extension directory. You should be able to run the [unit tests](#unit-tests), with the PHP extension installed.

To run tests with generated stub code from `.proto` files, you will also need the `composer`, `protoc` and `protoc-gen-php` binaries. You can find out how to get these [below](#generated-code-tests).

## Build from Source


### gRPC C core library

Clone this repository

```sh
$ git clone https://github.com/grpc/grpc.git
```

Build and install the gRPC C core library

```sh
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
$ make
$ sudo make install
```

### gRPC PHP extension

Install the gRPC PHP extension from PECL

```sh
$ sudo pecl install grpc-beta
```

Or, compile from source

```sh
$ cd grpc/src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ sudo make install
```

### Update php.ini

Add this line to your `php.ini` file, e.g. `/etc/php5/cli/php.ini`

```sh
extension=grpc.so
```

## Unit Tests

You will need the source code to run tests

```sh
$ git clone https://github.com/grpc/grpc.git
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
```

Run unit tests

```sh
$ cd grpc/src/php
$ ./bin/run_tests.sh
```

## Generated Code Tests

This section specifies the prerequisites for running the generated code tests, as well as how to run the tests themselves.

### Composer

If you don't have it already, install `composer` to pull in some runtime dependencies based on the `composer.json` file.

```sh
$ curl -sS https://getcomposer.org/installer | php
$ sudo mv composer.phar /usr/local/bin/composer

$ cd grpc/src/php
$ composer install
```

### Protobuf compiler

Again if you don't have it already, you need to install the protobuf compiler `protoc`, version 3.0.0+.

If you compiled the gRPC C core library from source above, the `protoc` binary should have been installed as well. If it hasn't been installed, you can run the following commands to install it.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

Alternatively, you can download `protoc` binaries from [the protocol buffers Github repository](https://github.com/google/protobuf/releases).


### PHP protobuf compiler

You need to install `protoc-gen-php` to generate stub class `.php` files from service definition `.proto` files.

```sh
$ cd grpc/src/php/vendor/datto/protobuf-php # if you had run `composer install` in the previous step

OR

$ git clone https://github.com/stanley-cheung/Protobuf-PHP # clone from github repo

$ gem install rake ronn
$ rake pear:package version=1.0
$ sudo pear install Protobuf-1.0.tgz
```

### Client Stub

Generate client stub classes from `.proto` files

```sh
$ cd grpc/src/php
$ ./bin/generate_proto_php.sh
```

### Run test server

Run a local server serving the math services. Please see [Node][] for how to run an example server.

```sh
$ cd grpc
$ npm install
$ nodejs src/node/test/math/math_server.js
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
$ nodejs src/node/test/math/math_server.js
```

Make sure you have run `composer install` to generate the `vendor/autoload.php` file

```sh
$ cd grpc/src/php
$ composer install
```

Make sure you have generated the client stub `math.php`

```sh
$ ./bin/generate_proto_php.sh
```

Copy the `math_client.php` file into your Apache document root, e.g.

```sh
$ cp tests/generated_code/math_client.php /var/www/html
```

You may have to fix the first two lines to point the includes to your installation:

```php
include 'vendor/autoload.php';
include 'tests/generated_code/math.php';
```

Connect to `localhost/math_client.php` in your browser, or run this from command line:

```sh
$ curl localhost/math_client.php
```

## Use the gRPC PHP extension with Nginx/PHP-FPM

Install `nginx` and `php5-fpm`, in addition to `php5` above

```sh
$ sudo apt-get install nginx php5-fpm
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
$ nodejs src/node/test/math/math_server.js
```

Make sure you have run `composer install` to generate the `vendor/autoload.php` file

```sh
$ cd grpc/src/php
$ composer install
```

Make sure you have generated the client stub `math.php`

```sh
$ ./bin/generate_proto_php.sh
```

Copy the `math_client.php` file into your Nginx document root, e.g.

```sh
$ cp tests/generated_code/math_client.php /var/www/html
```

You may have to fix the first two lines to point the includes to your installation:

```php
include 'vendor/autoload.php';
include 'tests/generated_code/math.php';
```

Connect to `localhost/math_client.php` in your browser, or run this from command line:

```sh
$ curl localhost/math_client.php
```

[Node]:https://github.com/grpc/grpc/tree/master/src/node/examples
