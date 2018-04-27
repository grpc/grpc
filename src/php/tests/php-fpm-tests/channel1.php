<?php

$channel = new Grpc\Channel('localhost:50001',
   ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
