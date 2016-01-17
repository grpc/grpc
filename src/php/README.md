
#Overview

This directory contains source code for PHP implementation of gRPC layered on shared C library.

#Status

Beta

## Environment

Prerequisite: PHP 5.5 or later, `phpunit`, `pecl`

**Linux:**

```sh
$ sudo apt-get install php5 php5-dev php-pear
```

**Mac OS X:**

```sh
$ curl -O http://pear.php.net/go-pear.phar
$ sudo php -d detect_unicode=0 go-pear.phar
```

**PHPUnit: (Both Linux and Mac OS X)**
```sh
$ curl https://phar.phpunit.de/phpunit.phar -o phpunit.phar
$ chmod +x phpunit.phar
$ sudo mv phpunit.phar /usr/local/bin/phpunit
```

## Quick Install

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

Install the gRPC PHP extension

```sh
sudo pecl install grpc-beta
```

**Mac OS X:**

Install [homebrew][]. Example:

```sh
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the gRPC core library and the PHP extension in one step

```sh
$ curl -fsSL https://goo.gl/getgrpc | bash -s php
```

This will download and run the [gRPC install script][] and compile the gRPC PHP extension.


## Build from Source

Clone this repository

```sh
$ git clone https://github.com/grpc/grpc.git
```

Build and install the gRPC C core libraries

```sh
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
$ make
$ sudo make install
```

Note: you may encounter a warning about the Protobuf compiler `protoc` 3.0.0+ not being installed. The following might help, and will be useful later on when we need to compile the `protoc-gen-php` tool.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

Install the gRPC PHP extension

```sh
$ sudo pecl install grpc-beta
```

OR

```sh
$ cd grpc/src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ sudo make install
```

Add this line to your `php.ini` file, e.g. `/etc/php5/cli/php.ini`

```sh
extension=grpc.so
```

Install Composer

```sh
$ cd grpc/src/php
$ curl -sS https://getcomposer.org/installer | php
$ sudo mv composer.phar /usr/local/bin/composer
$ composer install
```

## Unit Tests

Run unit tests

```sh
$ cd grpc/src/php
$ ./bin/run_tests.sh
```

## Generated Code Tests

Install `protoc-gen-php`

```sh
$ cd grpc/src/php/vendor/datto/protobuf-php
$ gem install rake ronn
$ rake pear:package version=1.0
$ sudo pear install Protobuf-1.0.tgz
```

Generate client stub code

```sh
$ cd grpc/src/php
$ ./bin/generate_proto_php.sh
```

Run a local server serving the math services

 - Please see [Node][] on how to run an example server

```sh
$ cd grpc/src/node
$ npm install
$ nodejs examples/math_server.js
```

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
$ cd grpc/src/node
$ nodejs examples/math_server.js
```

Make sure you have run `composer install` to generate the `vendor/autoload.php` file

```sh
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
$ cd grpc/src/node
$ nodejs examples/math_server.js
```

Make sure you have run `composer install` to generate the `vendor/autoload.php` file

```sh
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

[homebrew]:http://brew.sh
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Node]:https://github.com/grpc/grpc/tree/master/src/node/examples
[Debian jessie-backports]:http://backports.debian.org/Instructions/
