AMF3 encoding/decoding extension for PHP
========================================

[PHP-AMF3] extension provides the following API:

### amf3_encode(mixed $value [, int $opts = 0 ])
Returns a binary string containing an AMF3 representation of `$value`. On error, returns `FALSE`
and issues a warning message. The `$opts` argument is a bitmask of the following bit constants:
- `AMF3_FORCE_OBJECT`: force encoding non-indexed arrays as anonymous objects;

Objects implementing `AMF3Serializable` interface can customize their AMF3 representation:
```php
class MyClass implements AMF3Serializable {
    ...
    public function __toAMF3() {
        return [
            'foo' => $this->getFoo(),
            'bar' => $this->bar,
        ];
    }
}
```

### amf3_decode(string $data [, int &$pos [, int $opts = 0 ]])
Returns the value encoded in `$data`. Optional `$pos` marks where to start reading in `$data`
(default is 0). Upon return, it contains the index of the first unread byte (-1 indicates an error).
The `$opts` argument is a bitmask of the following bit constants:
- `AMF3_CLASS_MAP`: enable class mapping mode (see the usage constrains below);
- `AMF3_CLASS_AUTOLOAD`: enable the PHP class autoloading mechanism in class mapping mode;
- `AMF3_CLASS_CONSTRUCT`: call the default constructor for every new object in class mapping mode;


Installation
------------

To install the extension, type the following in the source directory:

    phpize
    ./configure
    make
    make install

This should install the extension in your default PHP extension directory. If it doesn't work as
expected, manually put the target `amf3.so` library where the `extension_dir` variable in your
`php.ini` points to. Add the following line to the corresponding section in your `php.ini`:

    extension=amf3.so

To run tests, type:

    make test


Usage constraints
-----------------

- PHP `NULL`, `boolean`, `integer`, `float` (double), `string`, `array`, and `object` values are
  fully convertible to/from their corresponding AMF3 types;
- AMF3 `Date` becomes a float value whereas `XML`, `XMLDocument`, and `ByteArray` become strings;
- In a special case, PHP integers are converted to AMF3 doubles according to the specification.
- A PHP `array` is encoded as an indexed array when it has purely integer keys that start from zero
  and have no gaps. An empty array adheres to this rule. In all other cases, an array is encoded as
  as associative array to avoid ambiguity.
- When class mapping is disabled (the default), AMF3 objects are returned as associative PHP arrays.
  Otherwise, they are returned as PHP objects.


[PHP-AMF3]: https://github.com/neoxic/php-amf3
