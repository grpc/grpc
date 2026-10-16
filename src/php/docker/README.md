
# Docker Images for Testing

This directory contains a number of docker images to assist testing the
[gRPC PECL extension](http://pecl.php.net/package/grpc) against various
different PHP environments.


## Build and Run Tests
```sh
$ cd grpc
```

To build all docker images:
```sh
$ ./src/php/bin/build_all_docker_images.sh
```

Or to only build some selected images
```sh
$ ./src/php/bin/build_all_docker_images.sh grpc-ext php-src
```

Or to only print out individual `docker build` commands
```sh
$ ./src/php/bin/build_all_docker_images.sh --cmds
```

To run all tests:
```sh
$ ./src/php/bin/run_all_docker_images.sh
```

Or to only run some selected images
```sh
$ ./src/php/bin/run_all_docker_images.sh grpc-ext php-src
```

Or to only print out individual `docker run` commands
```sh
$ ./src/php/bin/run_all_docker_images.sh --cmds
```

## Build and Run Specified Image
### `grpc-ext`
This image builds the full `grpc` PECL extension (effectively the current
release candidate), installs it against the current PHP version, and runs the
unit tests.

Build `grpc-ext` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/grpc-ext -f ./src/php/docker/grpc-ext/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/grpc-ext
```

### `grpc-src`

This image builds the `grpc` PECL extension in a 'thin' way, only containing
the gRPC extension source files. The gRPC C Core library is expected to be
installed separately and dynamically linked. The extension is installed
against the current PHP version.

This also allows us to compile our `grpc` extension with some additional
configure options, like `--enable-tests`, which allows some additional unit
tests to be run.

Build `grpc-src` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/grpc-src -f ./src/php/docker/grpc-src/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/grpc-src
```

### `alpine`

This image builds the `grpc` extension against the current PHP version in an
Alpine-Linux base image.

Build `alpine` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/alpine -f ./src/php/docker/alpine/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/alpine
```
### `centos7`

This image builds the `grpc` extension against the GCC version in Centos7 base image. The default version of gcc in centos7 is gcc-4.8.5. Run `scl enable devtoolset-7 bash` command to enable gcc-7.3.1.

Build `centos7` docker image:
```sh
$ cd grpc
$ docker build -t grpc-gcc7/centos -f ./src/php/docker/centos7/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-gcc7/centos
```

### `php-src`

Instead of using a general purpose base docker image provided by PHP, here we
compile PHP itself from
[source](https://github.com/php/php-src). This will allow us to change some
`configure` options, like `--enable-debug`. Then we proceed to build the full
`grpc` PECL extension and run the unit tests.

Build `php-src` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/php-src -f ./src/php/docker/php-src/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/php-src
```

### `php-zts`

This image builds the `grpc` extension against the current PHP version with ZTS
enabled.

Build `php-zts` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/php-zts -f ./src/php/docker/php-zts/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/php-zts
```

### `php-future`

This image builds the `grpc` extension against the next future PHP version
currently in alpha, beta or release candidate stage.

Build `php-future` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/php-future -f ./src/php/docker/php-future/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/php-future
```
### `php5`

NOTE: PHP 5.x has reached the end-of-life state and is no longer supported.


### `fork-support`

This image tests `pcntl_fork()` support and makes sure scripts using
`pcntl_fork()` don't freeze or crash.

Build `grpc-ext` docker image:
```sh
$ cd grpc
$ docker build -t grpc-php/fork-support -f ./src/php/docker/fork-support/Dockerfile .
```

Run image:
```sh
$ docker run -it --rm grpc-php/fork-support
```