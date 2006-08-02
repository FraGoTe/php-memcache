--TEST--
memcache->getServerStatus(), memcache->setServerParams()
--SKIPIF--
<?php include 'connect.inc'; if(!extension_loaded("memcache") || !isset($host2)) print "skip"; ?>
--FILE--
<?php

include 'connect.inc';

// Run hash destination test
$memcache = new Memcache();
$memcache->addServer($host, $port, false);
$memcache->addServer($host2, $port2, false);

$memcache1 = memcache_connect($host, $port);
$memcache1->set('load_test_key1', '');
$memcache1->set('load_test_key2', '');

$memcache2 = memcache_pconnect($host2, $port2);
$memcache2->set('load_test_key1', '');
$memcache2->set('load_test_key2', '');

$result1 = $memcache->set('load_test_key1', 'test-030-01', false, 1);	// hashes to $host2
$result2 = $memcache->set('load_test_key2', 'test-030-02', false, 1);	// hashes to $host1

var_dump($result1);
var_dump($result2);

$result4 = $memcache1->get('load_test_key1');	// return false; key1 is at $host2 
$result5 = $memcache1->get('load_test_key2');
$result6 = $memcache2->get('load_test_key1');	
$result7 = $memcache2->get('load_test_key2');	// return false; key2 is at $host1

var_dump($result4);
var_dump($result5);
var_dump($result6);
var_dump($result7);

// Add server in failed mode
$memcache = new Memcache();
$memcache->addServer($host, $port, false);
$memcache->addServer($host2, $port2, false, 1, 1, -1, false);

$result8 = $memcache->getServerStatus($host, $port);
$result9 = $memcache->getServerStatus($host2, $port2);

var_dump($result8);
var_dump($result9);

// Hot-add failed server
$result10 = $memcache->setServerParams($host2, $port2, 1, 15, true);
$result11 = $memcache->getServerStatus($host2, $port2);

var_dump($result10);
var_dump($result11);

$result12 = $memcache->set('load_test_key1', 'test-030-03', false, 1);	// hashes to $host2
$result13 = $memcache2->get('load_test_key1');	// key should be at $host2 (reconnected $host2)

var_dump($result12);
var_dump($result13);

?>
--EXPECT--
bool(true)
bool(true)
string(0) ""
string(11) "test-030-02"
string(11) "test-030-01"
string(0) ""
int(1)
int(0)
bool(true)
int(1)
bool(true)
string(11) "test-030-03"
