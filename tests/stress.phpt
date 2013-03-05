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

function randStr($safe = false) {
	$t = array();
	$n = rand(0, 30);
	$c = $safe ? 1 : 0;
	for ($i = 0; $i < $n; ++$i) $t[] = chr(rand($c, 10));
	return implode($t);
}

class AAA {};
class BBB {};
class CCC {};

function randObj() {
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
	function () { return randStr(); }, // string
);
$refs = array();
$any;
$objs = array(
	function () use (&$refs) { // reference
		$n = count($refs);
		return $n ? $refs[rand(0, $n - 1)] : NULL;
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
			$k = randStr(true);
			$v = $any($d + 1);
			if ($k && ($v !== NULL)) $a[$k] = $v;
		}
		$refs[] = $a;
		return $a;
	},
	function ($d) use (&$vals, &$refs, &$any) { // object
		$o = randObj();
		$n = rand(0, 10);
		for ($i = 0; $i < $n; ++$i) {
			$k = randStr(true);
			$v = $any($d + 1);
			if ($k && ($v !== NULL)) $o->$k = $v;
		}
		$refs[] = $o;
		return $o;
	},
);
$nobj;
$any = function ($d) use (&$vals, &$objs, &$nobj) {
	if (($d < 4) && prob(0.7)) return $objs[rand(0, count($objs) - ($nobj ? 2 : 1))]($d);
	return $vals[rand(0, count($vals) - 1)]();
};

function spawn() {
	global $refs, $any;
	$refs = array();
	return $any(0);
}


// Stress test

for ($i = 0; $i < 1000; ++$i) {
	$nobj = prob(0.5);
	$obj = spawn();
	$str = amf3_encode($obj, $nobj ? AMF3_FORCE_OBJECT : 0);
	$size = strlen($str);
	$_size;
	$_obj = amf3_decode($str, $_size, !$nobj ? AMF3_CLASS_MAP : 0);
	if (($size != $_size) || ($obj != $_obj)) {
		print(bin2hex($str) . "\n");
		print(bin2hex(var_export($obj, true)) . "\n");
		print(bin2hex(var_export($_obj, true)) . "\n");
		die();
	}

	// Additional decoder's robustness test
	for ($pos = 1; $pos < $size; ++$pos) {
		@amf3_decode(substr($str, $pos), $_size);
	}
}

?>
--EXPECT--
