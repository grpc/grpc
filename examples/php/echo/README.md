
# gRPC PHP End-to-End Examples

This page shows a number of ways to create a PHP gRPC client and connect with
a gRPC backend service.


## Run the Server

For all the following examples, we use a simple gRPC server, written in Node.

```sh
$ git clone https://github.com/grpc/grpc-web
$ cd grpc-web
$ docker-compose build prereqs node-server
$ docker run -d -p 9090:9090 --name node-server grpcweb/node-server
```


## Install the gRPC PECL extension

All the following commands are assumed to be run from this current directory.

```sh
$ cd grpc/examples/php/echo
```


In order to build a PHP gRPC client, we need to install the `grpc` extension
first.

```sh
$ docker build -t grpc-php/base -f ./base.Dockerfile .
```


## CLI


Let's first build a simple CLI gRPC client:

```sh
$ docker build -t grpc-php/echo-client -f ./cli.Dockerfile .
$ docker run -it --rm --link node-server:node-server grpc-php/echo-client
$ php client.php
```



## Apache


Now let's see how the gRPC PHP client can run with Apache:

```sh
$ docker build -t grpc-php/apache -f ./apache.Dockerfile .
$ docker run -it --rm --link node-server:node-server -p 80:80 grpc-php/apache
```

Open the browser to `http://localhost`.



## Nginx + FPM


We can also try running PHP-FPM and put Nginx in front of it.


The PHP-FPM part:

```sh
$ docker build -t grpc-php/fpm -f ./fpm.Dockerfile .
$ docker run -it --rm --link node-server:node-server -p 9000:9000 \
  --name fpm grpc-php/fpm
```

The Nginx part:

```sh
$ docker run -it --rm -v $(pwd)/nginx.conf:/etc/nginx/conf.d/default.conf:ro \
  --link fpm:fpm -p 80:80 nginx:1.17.4
```


Open the browser to `http://localhost`.
