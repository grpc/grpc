<?php
function getNewPort() {
  static $port = 10000;
  $port += 1;
  return $port;
}