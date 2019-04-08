--TEST--
Ensure default ini settings
--SKIPIF--
<?php if (!extension_loaded("grpc")) print "skip"; ?>
--FILE--
<?php
if (ini_get('grpc.enable_fork_support')) {
    die('grpc.enable_fork_support not off by default');
}
if (ini_get('grpc.poll_strategy') !== "") {
    die('grpc.poll_strategy not empty by default');
}
echo 'ok';
--EXPECT--
ok
