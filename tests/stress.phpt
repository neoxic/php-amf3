--TEST--
AMF3 general stress test
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

function randf() {
	return rand() / (getrandmax() + 1);
}

function prob($p) {
	return randf() < $p;
}

class AAA {};
class BBB {};
class CCC {};

function randClass() {
	switch (rand(0, 3)) {
		case 0: return new stdClass();
		case 1: return new AAA();
		case 2: return new BBB();
		case 3: return new CCC();
	}
}

$vals = array(
	function () { return NULL; }, // NULL
	function () { return prob(0.5); }, // boolean
	function () { return rand(-268435456, 268435455); }, // integer
	function () { return (randf() - 0.5) * 1234567890; }, // double
	function () { // string
		$t = array();
		$n = rand(0, 30);
		for ($i = 0; $i < $n; ++$i) $t[] = chr(rand(0, 10));
		return implode($t);
	},
);
$refs = array();
$any;
$objs = array(
	function () use (&$refs) { // reference
		$n = count($refs);
		return $n > 0 ? $refs[rand(0, $n - 1)] : NULL;
	},
	function ($d) use (&$refs, &$any) { // dense array
		$a = array();
		$n = rand(0, 10);
		for ($i = 0; $i < $n; ++$i) {
			$v = $any($d + 1);
			if ($v !== NULL) $a[] = $v;
		}
		$refs[] = $a;
		return $a;
	},
	function ($d) use (&$vals, &$refs, &$any) { // associative array
		$a = array();
		$n = rand(0, 10);
		for ($i = 0; $i < $n; ++$i) {
			$k = $vals[4](); // random string key
			$v = $any($d + 1);
			if ($k && $v !== NULL) $a[$k] = $v;
		}
		$refs[] = $a;
		return $a;
	},
	function ($d) use (&$vals, &$refs, &$any) { // object
		$o = randClass();
		$n = rand(0, 10);
		for ($i = 0; $i < $n; ++$i) {
			$k = md5($vals[4]()); // random property
			$v = $any($d + 1);
			if ($k && $v !== NULL) $o->$k = $v;
		}
		$refs[] = $o;
		return $o;
	},
);
$any = function ($d) use (&$vals, &$objs) {
	if (($d < 4) && prob(0.7)) return $objs[rand(0, count($objs) - 1)]($d);
	return $vals[rand(0, count($vals) - 1)]();
};

function spawn() {
	global $refs, $any;
	$refs = array();
	return $any(0);
}


// Stress Test

for ($i = 0; $i < 50; ++$i) {
	for ($j = 0; $j < 20; ++$j) {
		$obj = spawn();
		$str = amf3_encode($obj);
		$size = strlen($str);
		$_size;
		$_obj = amf3_decode($str, $_size, AMF3_MAP);
		if (($size != $_size) || ($obj != $_obj)) {
			print(bin2hex(var_export($obj, true)) . "\n");
			print(bin2hex(var_export($_obj, true)) . "\n");
			print(bin2hex($str) . "\n");
			die();
		}
	}
}

?>
--EXPECT--
