--TEST--
Test addHttp2Port of Server : basic functionality
--FILE--
<?php
$server = new Grpc\Server([]);
$port = $server->addHttp2Port("127.0.0.1:1234");
var_dump($port);
?>
===DONE===
--EXPECTF--
%s
int(1234)
===DONE===
