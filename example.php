<?php

$refer_me = array("I'm in a reference object");
$sequence = array( // will be encoded as a sequence (dense array); the keys are sequential
	111,
	222,
	333,
);
$not_sequence = array( // will be encoded as an associative array despite numeric keys
	0 => 444,
	2 => 555,
	3 => 666,
);
$obj = array(
	"bool" => true,
	"int" => 1234,
	"float" => -4567.89,
	"array" => array(
		"bool" => false,
		"int" => 5678,
		"ref" => &$refer_me, // first explicit reference
	),
	"ref" => &$refer_me, // second explicit reference
	"sequence" => $sequence,
	"not_sequence" => $not_sequence,
);

$byte_stream = amf3_encode($obj);
$s = bin2hex($byte_stream);
print substr($s, strpos($s, bin2hex("sequence")))."\n";
if ($byte_stream === false) {
	error_log("amf3_encode() failed!");
	return;
}
print "Byte-stream size: ".strlen($byte_stream)."\n";
$byte_count = 0;
$new_obj = amf3_decode($byte_stream, $byte_count);
if ($byte_count < 0) {
	error_log("amf3_decode() failed decoding byte-stream: ".bin2hex($byte_stream));
	return;
}
var_dump($new_obj);
print "Bytes read from byte-stream: $byte_count\n";

?>
