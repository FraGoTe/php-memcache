--TEST--
memcache_set() function
--SKIPIF--
<?php if(!extension_loaded("memcache")) print "skip"; ?>
--FILE--
<?php

include 'connect.inc';

$var = new stdClass;
$var->plain_attribute = 'value';
$var->array_attribute = Array('test1', 'test2');

memcache_set($memcache, 'test_key', $var, false, 10);

echo "Done\n";

?>
--EXPECT--
Done
