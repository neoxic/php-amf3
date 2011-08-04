PHP_ARG_ENABLE(amf3, whether to enable AMF3 support,
[  --enable-amf3           enable AMF3 support])

if test "$PHP_AMF3" = "yes"; then
  AC_DEFINE(HAVE_AMF3, 1, [Whether you have AMF3 support])
  PHP_NEW_EXTENSION(amf3, amf3.c, $ext_shared)
fi
