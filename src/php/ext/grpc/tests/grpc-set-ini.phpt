--TEST--
Ensure ini settings are handled
--SKIPIF--
<?php if (!extension_loaded("grpc")) print "skip"; ?>
--INI--
grpc.enable_fork_support = 1
grpc.poll_strategy = epoll1
--FILE--
<?php
if (!ini_get('grpc.enable_fork_support')) {
    die('grpc.enable_fork_support not set');
}
if (!getenv('GRPC_ENABLE_FORK_SUPPORT')) {
    die('env GRPC_ENABLE_FORK_SUPPORT not set');
}
if (ini_get('grpc.poll_strategy') !== 'epoll1') {
    die('grpc.poll_strategy !== epoll1');
}
if (getenv('GRPC_POLL_STRATEGY') !== 'epoll1') {
    die('env GRPC_POLL_STRATEGY not epoll1');
}
echo 'ok';
--EXPECT--
ok
