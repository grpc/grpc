--TEST--
Test getTarget of Channel : basic functionality
--FILE--
<?php
$channel = new Grpc\Channel('localhost:8888', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$target = $channel->getTarget();
var_dump($target);
?>
===DONE===
--EXPECTF--
%s
string(14) "localhost:8888"
===DONE===
