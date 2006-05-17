--TEST--
ini_set("memcache.allow_failover")
--SKIPIF--
<?php include 'connect.inc'; ?>
--FILE--
<?php

include 'connect.inc';

$var1 = 'test1';
$var2 = 'test2';

ini_set('memcache.allow_failover', 1);

$memcache = new Memcache();
$memcache->addServer($host, $port);
$memcache->addServer($nonExistingHost, $nonExistingPort);

$result1 = $memcache->set('load_test_key1', $var1, false, 1);	// hashes to $host2
$result2 = $memcache->set('load_test_key2', $var2, false, 1);	// hashes to $host1
$result3 = $memcache->get('load_test_key1');
$result4 = $memcache->get('load_test_key2');

var_dump($result1);
var_dump($result2);
var_dump($result3);
var_dump($result4);

ini_set('memcache.allow_failover', 0);

$result5 = $memcache->get('load_test_key1');
$result6 = $memcache->get('load_test_key2');

var_dump($result5);
var_dump($result6);

$result7 = ini_set('memcache.allow_failover', "abc");

var_dump($result7);

?>
--EXPECTF--
bool(true)
bool(true)
string(5) "test1"
string(5) "test2"
bool(false)
string(5) "test2"
string(1) "0"
