--TEST--
PHP-AMF3 test
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
	$n = rand(0, 10);
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
	function () { return prob(0.5); }, // Boolean
	function () { return rand(-268435456, 268435455); }, // Integer
	function () { return (randf() - 0.5) * 1234567890; }, // Double
	function () { return randStr(); }, // String
);
$refs = array();
$any;
$objs = array(
	function () use (&$refs) { // Reference
		$n = count($refs);
		return $n ? $refs[rand(0, $n - 1)] : NULL;
	},
	function ($d) use (&$refs, &$any) { // Indexed array
		$a = array();
		$n = rand(0, 10);
		for ($i = 0; $i < $n; ++$i) {
			$v = $any($d + 1);
			if ($v !== NULL) $a[] = $v;
		}
		$refs[] = $a;
		return $a;
	},
	function ($d) use (&$vals, &$refs, &$any) { // Associative array
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
	function ($d) use (&$vals, &$refs, &$any) { // Object
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


// Compliance test

$strs = array(
	// Date, XML, XMLDoc, ByteArray
	"\x09\x11\x01" // Array (length 8)
	.	"\x08\x01\x3f\xb9\x99\x99\x99\x99\x99\x9a" // Date (0.1)
	.	"\x0b\x07\x41\x42\x43" // XML ('ABC')
	.	"\x07\x07\x44\x45\x46" // XMLDoc ('DEF')
	.	"\x0c\x07\x11\x22\x33" // ByteArray (0x11 0x22 0x33)
	.	"\x08\x02" // Date (reference 1)
	.	"\x0b\x04" // XML (reference 2)
	.	"\x0b\x06" // XMLDoc (reference 3)
	.	"\x0c\x08" // ByteArray (reference 4)
	,
	// Array
	"\x09\x05\x01" // Array (length 2)
	.	"\x09\x07" // Array (length 3)
	.		"\x03\x41\x04\x00\x03\x42\x04\x01\x00\x04\x02" // Associative part: A:0, B:1, A:2 (should reset the key)
	.		"\x01" // End of associative part
	.		"\x02\x03\x04\x00" // Dense part: [false, true, 0]
	.	"\x09\x02" // Array (reference 1)
	,
	// Object
	"\x09\x11\x01" // Array (length 8)
	.	"\x0a\x3b\x07\x41\x42\x43\x03\x41\x03\x42\x03\x43" // Dynamic class ABC with static members A, B, C
	.		"\x04\x01\x04\x02\x04\x03" // Static member values: A:1, B:2, C:3
	.		"\x03\x44\x04\x04\x03\x45\x04\x05" // Dynamic members: D:4, E:5
	.		"\x01" // End of dymanic part
	.	"\x0a\x01" // Object (class reference 0)
	.		"\x01\x02\x03" // Static member values: A:null, B:false, C:true
	.		"\x03\x46\x02\x03\x46\x03" // Dynamic members: F:false, F:true (should reset the key)
	.		"\x01" // End of dymanic part
	.	"\x0a\x07\x07\x44\x45\x46" // Externalizable class DEF
	.		"\x02" // _data:false
	.	"\x0a\x05" // Object (class reference 1)
	.		"\x03" // _data:true
	.	"\x0a\x02" // Object (reference 1)
	.	"\x0a\x04" // Object (reference 2)
	.	"\x0a\x06" // Object (reference 3)
	.	"\x0a\x08" // Object (reference 4)
);

$ba = "\x11\x22\x33";
$ma = array('A' => 2, 'B' => 1, 0 => false, true, 0);
$o1 = array('A' => 1, 'B' => 2, 'C' => 3, 'D' => 4, 'E' => 5, '_class' => 'ABC');
$o2 = array('A' => null, 'B' => false, 'C' => true, 'F' => true, '_class' => 'ABC');
$o3 = array('_data' => false, '_class' => 'DEF');
$o4 = array('_data' => true, '_class' => 'DEF');
$objs = array(
	array(0.1, 'ABC', 'DEF', $ba, 0.1, 'ABC', 'DEF', $ba),
	array($ma, $ma),
	array($o1, $o2, $o3, $o4, $o1, $o2, $o3, $o4),
);

for ($i = 0; $i < count($strs); ++$i) {
	$str = $strs[$i];
	$obj = $objs[$i];
	$size = strlen($str);
	$_size;
	$_obj = amf3_decode($str, $_size);
	if (($size != $_size) || ($obj != $_obj)) {
		print(bin2hex($str) . "\n");
		print(bin2hex(var_export($obj, true)) . "\n");
		print(bin2hex(var_export($_obj, true)) . "\n");
		die();
	}
}

?>
--EXPECT--
