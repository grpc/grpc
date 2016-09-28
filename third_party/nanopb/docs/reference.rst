=====================
Nanopb: API reference
=====================

.. include :: menu.rst

.. contents ::




Compilation options
===================
The following options can be specified in one of two ways:

1. Using the -D switch on the C compiler command line.
2. By #defining them at the top of pb.h.

You must have the same settings for the nanopb library and all code that
includes pb.h.

============================  ================================================
__BIG_ENDIAN__                 Set this if your platform stores integers and
                               floats in big-endian format. Mixed-endian
                               systems (different layout for ints and floats)
                               are currently not supported.
PB_NO_PACKED_STRUCTS           Disable packed structs. Increases RAM usage but
                               is necessary on some platforms that do not
                               support unaligned memory access.
PB_ENABLE_MALLOC               Set this to enable dynamic allocation support
                               in the decoder.
PB_MAX_REQUIRED_FIELDS         Maximum number of required fields to check for
                               presence. Default value is 64. Increases stack
                               usage 1 byte per every 8 fields. Compiler
                               warning will tell if you need this.
PB_FIELD_16BIT                 Add support for tag numbers > 255 and fields
                               larger than 255 bytes or 255 array entries.
                               Increases code size 3 bytes per each field.
                               Compiler error will tell if you need this.
PB_FIELD_32BIT                 Add support for tag numbers > 65535 and fields
                               larger than 65535 bytes or 65535 array entries.
                               Increases code size 9 bytes per each field.
                               Compiler error will tell if you need this.
PB_NO_ERRMSG                   Disables the support for error messages; only
                               error information is the true/false return
                               value. Decreases the code size by a few hundred
                               bytes.
PB_BUFFER_ONLY                 Disables the support for custom streams. Only
                               supports encoding and decoding with memory
                               buffers. Speeds up execution and decreases code
                               size slightly.
PB_OLD_CALLBACK_STYLE          Use the old function signature (void\* instead
                               of void\*\*) for callback fields. This was the
                               default until nanopb-0.2.1.
PB_SYSTEM_HEADER               Replace the standard header files with a single
                               header file. It should define all the required
                               functions and typedefs listed on the
                               `overview page`_. Value must include quotes,
                               for example *#define PB_SYSTEM_HEADER "foo.h"*.
============================  ================================================

The PB_MAX_REQUIRED_FIELDS, PB_FIELD_16BIT and PB_FIELD_32BIT settings allow
raising some datatype limits to suit larger messages. Their need is recognized
automatically by C-preprocessor #if-directives in the generated .pb.h files.
The default setting is to use the smallest datatypes (least resources used).

.. _`overview page`: index.html#compiler-requirements


Proto file options
==================
The generator behaviour can be adjusted using these options, defined in the
'nanopb.proto' file in the generator folder:

============================  ================================================
max_size                       Allocated size for *bytes* and *string* fields.
max_count                      Allocated number of entries in arrays
                               (*repeated* fields).
int_size                       Override the integer type of a field.
                               (To use e.g. uint8_t to save RAM.)
type                           Type of the generated field. Default value
                               is *FT_DEFAULT*, which selects automatically.
                               You can use *FT_CALLBACK*, *FT_POINTER*,
                               *FT_STATIC* or *FT_IGNORE* to force a callback
                               field, a dynamically allocated field, a static
                               field or to completely ignore the field.
long_names                     Prefix the enum name to the enum value in
                               definitions, i.e. *EnumName_EnumValue*. Enabled
                               by default.
packed_struct                  Make the generated structures packed.
                               NOTE: This cannot be used on CPUs that break
                               on unaligned accesses to variables.
skip_message                   Skip the whole message from generation.
no_unions                      Generate 'oneof' fields as optional fields
                               instead of C unions.
msgid                          Specifies a unique id for this message type.
                               Can be used by user code as an identifier.
============================  ================================================

These options can be defined for the .proto files before they are converted
using the nanopb-generatory.py. There are three ways to define the options:

1. Using a separate .options file.
   This is the preferred way as of nanopb-0.2.1, because it has the best
   compatibility with other protobuf libraries.
2. Defining the options on the command line of nanopb_generator.py.
   This only makes sense for settings that apply to a whole file.
3. Defining the options in the .proto file using the nanopb extensions.
   This is the way used in nanopb-0.1, and will remain supported in the
   future. It however sometimes causes trouble when using the .proto file
   with other protobuf libraries.

The effect of the options is the same no matter how they are given. The most
common purpose is to define maximum size for string fields in order to
statically allocate them.

Defining the options in a .options file
---------------------------------------
The preferred way to define options is to have a separate file
'myproto.options' in the same directory as the 'myproto.proto'. ::

    # myproto.proto
    message MyMessage {
        required string name = 1;
        repeated int32 ids = 4;
    }

::

    # myproto.options
    MyMessage.name         max_size:40
    MyMessage.ids          max_count:5

The generator will automatically search for this file and read the
options from it. The file format is as follows:

* Lines starting with '#' or '//' are regarded as comments.
* Blank lines are ignored.
* All other lines should start with a field name pattern, followed by one or
  more options. For example: *"MyMessage.myfield max_size:5 max_count:10"*.
* The field name pattern is matched against a string of form *'Message.field'*.
  For nested messages, the string is *'Message.SubMessage.field'*.
* The field name pattern may use the notation recognized by Python fnmatch():

  - *\** matches any part of string, like 'Message.\*' for all fields
  - *\?* matches any single character
  - *[seq]* matches any of characters 's', 'e' and 'q'
  - *[!seq]* matches any other character

* The options are written as *'option_name:option_value'* and several options
  can be defined on same line, separated by whitespace.
* Options defined later in the file override the ones specified earlier, so
  it makes sense to define wildcard options first in the file and more specific
  ones later.
  
If preferred, the name of the options file can be set using the command line
switch *-f* to nanopb_generator.py.

Defining the options on command line
------------------------------------
The nanopb_generator.py has a simple command line option *-s OPTION:VALUE*.
The setting applies to the whole file that is being processed.

Defining the options in the .proto file
---------------------------------------
The .proto file format allows defining custom options for the fields.
The nanopb library comes with *nanopb.proto* which does exactly that, allowing
you do define the options directly in the .proto file::

    import "nanopb.proto";
    
    message MyMessage {
        required string name = 1 [(nanopb).max_size = 40];
        repeated int32 ids = 4   [(nanopb).max_count = 5];
    }

A small complication is that you have to set the include path of protoc so that
nanopb.proto can be found. This file, in turn, requires the file
*google/protobuf/descriptor.proto*. This is usually installed under
*/usr/include*. Therefore, to compile a .proto file which uses options, use a
protoc command similar to::

    protoc -I/usr/include -Inanopb/generator -I. -omessage.pb message.proto

The options can be defined in file, message and field scopes::

    option (nanopb_fileopt).max_size = 20; // File scope
    message Message
    {
        option (nanopb_msgopt).max_size = 30; // Message scope
        required string fieldsize = 1 [(nanopb).max_size = 40]; // Field scope
    }









pb.h
====

pb_type_t
---------
Defines the encoder/decoder behaviour that should be used for a field. ::

    typedef uint8_t pb_type_t;

The low-order nibble of the enumeration values defines the function that can be used for encoding and decoding the field data:

==================== ===== ================================================
LTYPE identifier     Value Storage format
==================== ===== ================================================
PB_LTYPE_VARINT      0x00  Integer.
PB_LTYPE_SVARINT     0x01  Integer, zigzag encoded.
PB_LTYPE_FIXED32     0x02  32-bit integer or floating point.
PB_LTYPE_FIXED64     0x03  64-bit integer or floating point.
PB_LTYPE_BYTES       0x04  Structure with *size_t* field and byte array.
PB_LTYPE_STRING      0x05  Null-terminated string.
PB_LTYPE_SUBMESSAGE  0x06  Submessage structure.
==================== ===== ================================================

The bits 4-5 define whether the field is required, optional or repeated:

==================== ===== ================================================
HTYPE identifier     Value Field handling
==================== ===== ================================================
PB_HTYPE_REQUIRED    0x00  Verify that field exists in decoded message.
PB_HTYPE_OPTIONAL    0x10  Use separate *has_<field>* boolean to specify
                           whether the field is present.
                           (Unless it is a callback)
PB_HTYPE_REPEATED    0x20  A repeated field with preallocated array.
                           Separate *<field>_count* for number of items.
                           (Unless it is a callback)
==================== ===== ================================================

The bits 6-7 define the how the storage for the field is allocated:

==================== ===== ================================================
ATYPE identifier     Value Allocation method
==================== ===== ================================================
PB_ATYPE_STATIC      0x00  Statically allocated storage in the structure.
PB_ATYPE_CALLBACK    0x40  A field with dynamic storage size. Struct field
                           actually contains a pointer to a callback
                           function.
==================== ===== ================================================


pb_field_t
----------
Describes a single structure field with memory position in relation to others. The descriptions are usually autogenerated. ::

    typedef struct _pb_field_t pb_field_t;
    struct _pb_field_t {
        uint8_t tag;
        pb_type_t type;
        uint8_t data_offset;
        int8_t size_offset;
        uint8_t data_size;
        uint8_t array_size;
        const void *ptr;
    } pb_packed;

:tag:           Tag number of the field or 0 to terminate a list of fields.
:type:          LTYPE, HTYPE and ATYPE of the field.
:data_offset:   Offset of field data, relative to the end of the previous field.
:size_offset:   Offset of *bool* flag for optional fields or *size_t* count for arrays, relative to field data.
:data_size:     Size of a single data entry, in bytes. For PB_LTYPE_BYTES, the size of the byte array inside the containing structure. For PB_HTYPE_CALLBACK, size of the C data type if known.
:array_size:    Maximum number of entries in an array, if it is an array type.
:ptr:           Pointer to default value for optional fields, or to submessage description for PB_LTYPE_SUBMESSAGE.

The *uint8_t* datatypes limit the maximum size of a single item to 255 bytes and arrays to 255 items. Compiler will give error if the values are too large. The types can be changed to larger ones by defining *PB_FIELD_16BIT*.

pb_bytes_array_t
----------------
An byte array with a field for storing the length::

    typedef struct {
        size_t size;
        uint8_t bytes[1];
    } pb_bytes_array_t;

In an actual array, the length of *bytes* may be different.

pb_callback_t
-------------
Part of a message structure, for fields with type PB_HTYPE_CALLBACK::

    typedef struct _pb_callback_t pb_callback_t;
    struct _pb_callback_t {
        union {
            bool (*decode)(pb_istream_t *stream, const pb_field_t *field, void **arg);
            bool (*encode)(pb_ostream_t *stream, const pb_field_t *field, void * const *arg);
        } funcs;
        
        void *arg;
    };

A pointer to the *arg* is passed to the callback when calling. It can be used to store any information that the callback might need.

Previously the function received just the value of *arg* instead of a pointer to it. This old behaviour can be enabled by defining *PB_OLD_CALLBACK_STYLE*.

When calling `pb_encode`_, *funcs.encode* is used, and similarly when calling `pb_decode`_, *funcs.decode* is used. The function pointers are stored in the same memory location but are of incompatible types. You can set the function pointer to NULL to skip the field.

pb_wire_type_t
--------------
Protocol Buffers wire types. These are used with `pb_encode_tag`_. ::

    typedef enum {
        PB_WT_VARINT = 0,
        PB_WT_64BIT  = 1,
        PB_WT_STRING = 2,
        PB_WT_32BIT  = 5
    } pb_wire_type_t;

pb_extension_type_t
-------------------
Defines the handler functions and auxiliary data for a field that extends
another message. Usually autogenerated by *nanopb_generator.py*::

    typedef struct {
        bool (*decode)(pb_istream_t *stream, pb_extension_t *extension,
                   uint32_t tag, pb_wire_type_t wire_type);
        bool (*encode)(pb_ostream_t *stream, const pb_extension_t *extension);
        const void *arg;
    } pb_extension_type_t;

In the normal case, the function pointers are *NULL* and the decoder and
encoder use their internal implementations. The internal implementations
assume that *arg* points to a *pb_field_t* that describes the field in question.

To implement custom processing of unknown fields, you can provide pointers
to your own functions. Their functionality is mostly the same as for normal
callback fields, except that they get called for any unknown field when decoding.

pb_extension_t
--------------
Ties together the extension field type and the storage for the field value::

    typedef struct {
        const pb_extension_type_t *type;
        void *dest;
        pb_extension_t *next;
    } pb_extension_t;

:type:      Pointer to the structure that defines the callback functions.
:dest:      Pointer to the variable that stores the field value
            (as used by the default extension callback functions.)
:next:      Pointer to the next extension handler, or *NULL*.

PB_GET_ERROR
------------
Get the current error message from a stream, or a placeholder string if
there is no error message::

    #define PB_GET_ERROR(stream) (string expression)

This should be used for printing errors, for example::

    if (!pb_decode(...))
    {
        printf("Decode failed: %s\n", PB_GET_ERROR(stream));
    }

The macro only returns pointers to constant strings (in code memory),
so that there is no need to release the returned pointer.

PB_RETURN_ERROR
---------------
Set the error message and return false::

    #define PB_RETURN_ERROR(stream,msg) (sets error and returns false)

This should be used to handle error conditions inside nanopb functions
and user callback functions::

    if (error_condition)
    {
        PB_RETURN_ERROR(stream, "something went wrong");
    }

The *msg* parameter must be a constant string.



pb_encode.h
===========

pb_ostream_from_buffer
----------------------
Constructs an output stream for writing into a memory buffer. This is just a helper function, it doesn't do anything you couldn't do yourself in a callback function. It uses an internal callback that stores the pointer in stream *state* field. ::

    pb_ostream_t pb_ostream_from_buffer(uint8_t *buf, size_t bufsize);

:buf:           Memory buffer to write into.
:bufsize:       Maximum number of bytes to write.
:returns:       An output stream.

After writing, you can check *stream.bytes_written* to find out how much valid data there is in the buffer.

pb_write
--------
Writes data to an output stream. Always use this function, instead of trying to call stream callback manually. ::

    bool pb_write(pb_ostream_t *stream, const uint8_t *buf, size_t count);

:stream:        Output stream to write to.
:buf:           Pointer to buffer with the data to be written.
:count:         Number of bytes to write.
:returns:       True on success, false if maximum length is exceeded or an IO error happens.

If an error happens, *bytes_written* is not incremented. Depending on the callback used, calling pb_write again after it has failed once may be dangerous. Nanopb itself never does this, instead it returns the error to user application. The builtin pb_ostream_from_buffer is safe to call again after failed write.

pb_encode
---------
Encodes the contents of a structure as a protocol buffers message and writes it to output stream. ::

    bool pb_encode(pb_ostream_t *stream, const pb_field_t fields[], const void *src_struct);

:stream:        Output stream to write to.
:fields:        A field description array, usually autogenerated.
:src_struct:    Pointer to the data that will be serialized.
:returns:       True on success, false on IO error, on detectable errors in field description, or if a field encoder returns false.

Normally pb_encode simply walks through the fields description array and serializes each field in turn. However, submessages must be serialized twice: first to calculate their size and then to actually write them to output. This causes some constraints for callback fields, which must return the same data on every call.

pb_encode_delimited
-------------------
Calculates the length of the message, encodes it as varint and then encodes the message. ::

    bool pb_encode_delimited(pb_ostream_t *stream, const pb_field_t fields[], const void *src_struct);

(parameters are the same as for `pb_encode`_.)

A common way to indicate the message length in Protocol Buffers is to prefix it with a varint.
This function does this, and it is compatible with *parseDelimitedFrom* in Google's protobuf library.

.. sidebar:: Encoding fields manually

    The functions with names *pb_encode_\** are used when dealing with callback fields. The typical reason for using callbacks is to have an array of unlimited size. In that case, `pb_encode`_ will call your callback function, which in turn will call *pb_encode_\** functions repeatedly to write out values.

    The tag of a field must be encoded separately with `pb_encode_tag_for_field`_. After that, you can call exactly one of the content-writing functions to encode the payload of the field. For repeated fields, you can repeat this process multiple times.

    Writing packed arrays is a little bit more involved: you need to use `pb_encode_tag` and specify `PB_WT_STRING` as the wire type. Then you need to know exactly how much data you are going to write, and use `pb_encode_varint`_ to write out the number of bytes before writing the actual data. Substreams can be used to determine the number of bytes beforehand; see `pb_encode_submessage`_ source code for an example.

pb_encode_tag
-------------
Starts a field in the Protocol Buffers binary format: encodes the field number and the wire type of the data. ::

    bool pb_encode_tag(pb_ostream_t *stream, pb_wire_type_t wiretype, int field_number);

:stream:        Output stream to write to. 1-5 bytes will be written.
:wiretype:      PB_WT_VARINT, PB_WT_64BIT, PB_WT_STRING or PB_WT_32BIT
:field_number:  Identifier for the field, defined in the .proto file. You can get it from field->tag.
:returns:       True on success, false on IO error.

pb_encode_tag_for_field
-----------------------
Same as `pb_encode_tag`_, except takes the parameters from a *pb_field_t* structure. ::

    bool pb_encode_tag_for_field(pb_ostream_t *stream, const pb_field_t *field);

:stream:        Output stream to write to. 1-5 bytes will be written.
:field:         Field description structure. Usually autogenerated.
:returns:       True on success, false on IO error or unknown field type.

This function only considers the LTYPE of the field. You can use it from your field callbacks, because the source generator writes correct LTYPE also for callback type fields.

Wire type mapping is as follows:

========================= ============
LTYPEs                    Wire type
========================= ============
VARINT, SVARINT           PB_WT_VARINT
FIXED64                   PB_WT_64BIT  
STRING, BYTES, SUBMESSAGE PB_WT_STRING 
FIXED32                   PB_WT_32BIT
========================= ============

pb_encode_varint
----------------
Encodes a signed or unsigned integer in the varint_ format. Works for fields of type `bool`, `enum`, `int32`, `int64`, `uint32` and `uint64`::

    bool pb_encode_varint(pb_ostream_t *stream, uint64_t value);

:stream:        Output stream to write to. 1-10 bytes will be written.
:value:         Value to encode. Just cast e.g. int32_t directly to uint64_t.
:returns:       True on success, false on IO error.

.. _varint: http://code.google.com/apis/protocolbuffers/docs/encoding.html#varints

pb_encode_svarint
-----------------
Encodes a signed integer in the 'zig-zagged' format. Works for fields of type `sint32` and `sint64`::

    bool pb_encode_svarint(pb_ostream_t *stream, int64_t value);

(parameters are the same as for `pb_encode_varint`_

pb_encode_string
----------------
Writes the length of a string as varint and then contents of the string. Works for fields of type `bytes` and `string`::

    bool pb_encode_string(pb_ostream_t *stream, const uint8_t *buffer, size_t size);

:stream:        Output stream to write to.
:buffer:        Pointer to string data.
:size:          Number of bytes in the string. Pass `strlen(s)` for strings.
:returns:       True on success, false on IO error.

pb_encode_fixed32
-----------------
Writes 4 bytes to stream and swaps bytes on big-endian architectures. Works for fields of type `fixed32`, `sfixed32` and `float`::

    bool pb_encode_fixed32(pb_ostream_t *stream, const void *value);

:stream:    Output stream to write to.
:value:     Pointer to a 4-bytes large C variable, for example `uint32_t foo;`.
:returns:   True on success, false on IO error.

pb_encode_fixed64
-----------------
Writes 8 bytes to stream and swaps bytes on big-endian architecture. Works for fields of type `fixed64`, `sfixed64` and `double`::

    bool pb_encode_fixed64(pb_ostream_t *stream, const void *value);

:stream:    Output stream to write to.
:value:     Pointer to a 8-bytes large C variable, for example `uint64_t foo;`.
:returns:   True on success, false on IO error.

pb_encode_submessage
--------------------
Encodes a submessage field, including the size header for it. Works for fields of any message type::

    bool pb_encode_submessage(pb_ostream_t *stream, const pb_field_t fields[], const void *src_struct);

:stream:        Output stream to write to.
:fields:        Pointer to the autogenerated field description array for the submessage type, e.g. `MyMessage_fields`.
:src:           Pointer to the structure where submessage data is.
:returns:       True on success, false on IO errors, pb_encode errors or if submessage size changes between calls.

In Protocol Buffers format, the submessage size must be written before the submessage contents. Therefore, this function has to encode the submessage twice in order to know the size beforehand.

If the submessage contains callback fields, the callback function might misbehave and write out a different amount of data on the second call. This situation is recognized and *false* is returned, but garbage will be written to the output before the problem is detected.












pb_decode.h
===========

pb_istream_from_buffer
----------------------
Helper function for creating an input stream that reads data from a memory buffer. ::

    pb_istream_t pb_istream_from_buffer(uint8_t *buf, size_t bufsize);

:buf:           Pointer to byte array to read from.
:bufsize:       Size of the byte array.
:returns:       An input stream ready to use.

pb_read
-------
Read data from input stream. Always use this function, don't try to call the stream callback directly. ::

    bool pb_read(pb_istream_t *stream, uint8_t *buf, size_t count);

:stream:        Input stream to read from.
:buf:           Buffer to store the data to, or NULL to just read data without storing it anywhere.
:count:         Number of bytes to read.
:returns:       True on success, false if *stream->bytes_left* is less than *count* or if an IO error occurs.

End of file is signalled by *stream->bytes_left* being zero after pb_read returns false.

pb_decode
---------
Read and decode all fields of a structure. Reads until EOF on input stream. ::

    bool pb_decode(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct);

:stream:        Input stream to read from.
:fields:        A field description array. Usually autogenerated.
:dest_struct:   Pointer to structure where data will be stored.
:returns:       True on success, false on IO error, on detectable errors in field description, if a field encoder returns false or if a required field is missing.

In Protocol Buffers binary format, EOF is only allowed between fields. If it happens anywhere else, pb_decode will return *false*. If pb_decode returns false, you cannot trust any of the data in the structure.

In addition to EOF, the pb_decode implementation supports terminating a message with a 0 byte. This is compatible with the official Protocol Buffers because 0 is never a valid field tag.

For optional fields, this function applies the default value and sets *has_<field>* to false if the field is not present.

If *PB_ENABLE_MALLOC* is defined, this function may allocate storage for any pointer type fields.
In this case, you have to call `pb_release`_ to release the memory after you are done with the message.
On error return `pb_decode` will release the memory itself.

pb_decode_noinit
----------------
Same as `pb_decode`_, except does not apply the default values to fields. ::

    bool pb_decode_noinit(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct);

(parameters are the same as for `pb_decode`_.)

The destination structure should be filled with zeros before calling this function. Doing a *memset* manually can be slightly faster than using `pb_decode`_ if you don't need any default values.

In addition to decoding a single message, this function can be used to merge two messages, so that
values from previous message will remain if the new message does not contain a field.

This function *will not* release the message even on error return. If you use *PB_ENABLE_MALLOC*,
you will need to call `pb_release`_ yourself.

pb_decode_delimited
-------------------
Same as `pb_decode`_, except that it first reads a varint with the length of the message. ::

    bool pb_decode_delimited(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct);

(parameters are the same as for `pb_decode`_.)

A common method to indicate message size in Protocol Buffers is to prefix it with a varint.
This function is compatible with *writeDelimitedTo* in the Google's Protocol Buffers library.

pb_release
----------
Releases any dynamically allocated fields.

    void pb_release(const pb_field_t fields[], void *dest_struct);

:fields:        A field description array. Usually autogenerated.
:dest_struct:   Pointer to structure where data will be stored.

This function is only available if *PB_ENABLE_MALLOC* is defined. It will release any
pointer type fields in the structure and set the pointers to NULL.

pb_skip_varint
--------------
Skip a varint_ encoded integer without decoding it. ::

    bool pb_skip_varint(pb_istream_t *stream);

:stream:        Input stream to read from. Will read 1 byte at a time until the MSB is clear.
:returns:       True on success, false on IO error.

pb_skip_string
--------------
Skip a varint-length-prefixed string. This means skipping a value with wire type PB_WT_STRING. ::

    bool pb_skip_string(pb_istream_t *stream);

:stream:        Input stream to read from.
:returns:       True on success, false on IO error or length exceeding uint32_t.

pb_decode_tag
-------------
Decode the tag that comes before field in the protobuf encoding::

    bool pb_decode_tag(pb_istream_t *stream, pb_wire_type_t *wire_type, int *tag, bool *eof);

:stream:        Input stream to read from.
:wire_type:     Pointer to variable where to store the wire type of the field.
:tag:           Pointer to variable where to store the tag of the field.
:eof:           Pointer to variable where to store end-of-file status.
:returns:       True on success, false on error or EOF.

When the message (stream) ends, this function will return false and set *eof* to true. On other
errors, *eof* will be set to false.

pb_skip_field
-------------
Remove the data for a field from the stream, without actually decoding it::

    bool pb_skip_field(pb_istream_t *stream, pb_wire_type_t wire_type);

:stream:        Input stream to read from.
:wire_type:     Type of field to skip.
:returns:       True on success, false on IO error.

.. sidebar:: Decoding fields manually
    
    The functions with names beginning with *pb_decode_* are used when dealing with callback fields. The typical reason for using callbacks is to have an array of unlimited size. In that case, `pb_decode`_ will call your callback function repeatedly, which can then store the values into e.g. filesystem in the order received in.

    For decoding numeric (including enumerated and boolean) values, use `pb_decode_varint`_, `pb_decode_svarint`_, `pb_decode_fixed32`_ and `pb_decode_fixed64`_. They take a pointer to a 32- or 64-bit C variable, which you may then cast to smaller datatype for storage.

    For decoding strings and bytes fields, the length has already been decoded. You can therefore check the total length in *stream->bytes_left* and read the data using `pb_read`_.

    Finally, for decoding submessages in a callback, simply use `pb_decode`_ and pass it the *SubMessage_fields* descriptor array.

pb_decode_varint
----------------
Read and decode a varint_ encoded integer. ::

    bool pb_decode_varint(pb_istream_t *stream, uint64_t *dest);

:stream:        Input stream to read from. 1-10 bytes will be read.
:dest:          Storage for the decoded integer. Value is undefined on error.
:returns:       True on success, false if value exceeds uint64_t range or an IO error happens.

pb_decode_svarint
-----------------
Similar to `pb_decode_varint`_, except that it performs zigzag-decoding on the value. This corresponds to the Protocol Buffers *sint32* and *sint64* datatypes. ::

    bool pb_decode_svarint(pb_istream_t *stream, int64_t *dest);

(parameters are the same as `pb_decode_varint`_)

pb_decode_fixed32
-----------------
Decode a *fixed32*, *sfixed32* or *float* value. ::

    bool pb_decode_fixed32(pb_istream_t *stream, void *dest);

:stream:        Input stream to read from. 4 bytes will be read.
:dest:          Pointer to destination *int32_t*, *uint32_t* or *float*.
:returns:       True on success, false on IO errors.

This function reads 4 bytes from the input stream.
On big endian architectures, it then reverses the order of the bytes.
Finally, it writes the bytes to *dest*.

pb_decode_fixed64
-----------------
Decode a *fixed64*, *sfixed64* or *double* value. ::

    bool pb_dec_fixed(pb_istream_t *stream, const pb_field_t *field, void *dest);

:stream:        Input stream to read from. 8 bytes will be read.
:field:         Not used.
:dest:          Pointer to destination *int64_t*, *uint64_t* or *double*.
:returns:       True on success, false on IO errors.

Same as `pb_decode_fixed32`_, except this reads 8 bytes.

pb_make_string_substream
------------------------
Decode the length for a field with wire type *PB_WT_STRING* and create a substream for reading the data. ::

    bool pb_make_string_substream(pb_istream_t *stream, pb_istream_t *substream);

:stream:        Original input stream to read the length and data from.
:substream:     New substream that has limited length. Filled in by the function.
:returns:       True on success, false if reading the length fails.

This function uses `pb_decode_varint`_ to read an integer from the stream. This is interpreted as a number of bytes, and the substream is set up so that its `bytes_left` is initially the same as the length, and its callback function and state the same as the parent stream.

pb_close_string_substream
-------------------------
Close the substream created with `pb_make_string_substream`_. ::

    void pb_close_string_substream(pb_istream_t *stream, pb_istream_t *substream);

:stream:        Original input stream to read the length and data from.
:substream:     Substream to close

This function copies back the state from the substream to the parent stream.
It must be called after done with the substream.
