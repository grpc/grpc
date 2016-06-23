--TEST--
Test close of Channel : basic functionality
--FILE--
<?php
$channel = new Grpc\Channel('localhost:0', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$channel->close();
var_dump($channel);
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Channel)#1 (0) {
}
===DONE===
