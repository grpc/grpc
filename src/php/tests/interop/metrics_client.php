<?php

$args = getopt('', ['metric_server_address:', 'total_only::']);
$parts = explode(':', $args['metric_server_address']);
$server_host = $parts[0];
$server_port = (count($parts) == 2) ? $parts[1] : '';

$socket = socket_create(AF_INET, SOCK_STREAM, 0);
if (@!socket_connect($socket, $server_host, $server_port)) {
  echo "Cannot connect to merics server...\n";
  exit(1);
}
socket_write($socket, 'qps');
while ($out = socket_read($socket, 1024)) {
  echo "$out\n";
}
socket_close($socket);
