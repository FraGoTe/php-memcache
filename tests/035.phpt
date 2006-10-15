--TEST--
memcache::connect() with unix domain socket
--SKIPIF--
<?php include 'connect.inc'; if (!isset($domainsocket)) die('skip $domainsocket not set'); ?>
--FILE--
<?php

include 'connect.inc';

$memcache = new Memcache();
$result = $memcache->connect($domainsocket, 0);
var_dump($result);

$result1 = $memcache->set('test_key', 'abc');
$result2 = $memcache->get('test_key');
var_dump($result1);
var_dump($result2);

$memcache = memcache_connect($domainsocket, null);
var_dump($result);

?>
--EXPECTF--
bool(true)
bool(true)
string(3) "abc"
bool(true)