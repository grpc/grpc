=====================================
Nanopb: Migration from older versions
=====================================

.. include :: menu.rst

This document details all the breaking changes that have been made to nanopb
since its initial release. For each change, the rationale and required
modifications of user applications are explained. Also any error indications
are included, in order to make it easier to find this document.

.. contents ::

Nanopb-0.3.5 (2016-02-13)
=========================

Add support for platforms without uint8_t
-----------------------------------------
**Rationale:** Some platforms cannot access 8-bit sized values directly, and
do not define *uint8_t*. Nanopb previously didn't support these platforms.

**Changes:** References to *uint8_t* were replaced with several alternatives,
one of them being a new *pb_byte_t* typedef. This in turn uses *uint_least8_t*
which means the smallest available type.

**Required actions:** If your platform does not have a standards-compliant
*stdint.h*, it may lack the definition for *[u]int_least8_t*. This must be
added manually, example can be found in *extra/pb_syshdr.h*.

**Error indications:** Compiler error: "unknown type name 'uint_least8_t'".

Nanopb-0.3.2 (2015-01-24)
=========================

Add support for OneOfs
----------------------
**Rationale:** Previously nanopb did not support the *oneof* construct in
*.proto* files. Those fields were generated as regular *optional* fields.

**Changes:** OneOfs are now generated as C unions. Callback fields are not
supported inside oneof and generator gives an error.

**Required actions:** The generator option *no_unions* can be used to restore old
behaviour and to allow callbacks to be used. To use unions, one change is
needed: use *which_xxxx* field to detect which field is present, instead
of *has_xxxx*. Compare the value against *MyStruct_myfield_tag*.

**Error indications:** Generator error: "Callback fields inside of oneof are
not supported". Compiler error: "Message" has no member named "has_xxxx".

Nanopb-0.3.0 (2014-08-26)
=========================

Separate field iterator logic to pb_common.c
--------------------------------------------
**Rationale:** Originally, the field iteration logic was simple enough to be
duplicated in *pb_decode.c* and *pb_encode.c*. New field types have made the
logic more complex, which required the creation of a new file to contain the
common functionality.

**Changes:** There is a new file, *pb_common.c*, which must be included in
builds.

**Required actions:** Add *pb_common.c* to build rules. This file is always
required. Either *pb_decode.c* or *pb_encode.c* can still be left out if some
functionality is not needed.

**Error indications:** Linker error: undefined reference to
*pb_field_iter_begin*, *pb_field_iter_next* or similar.

Change data type of field counts to pb_size_t
---------------------------------------------
**Rationale:** Often nanopb is used with small arrays, such as 255 items or
less. Using a full *size_t* field to store the array count wastes memory if
there are many arrays. There already exists parameters *PB_FIELD_16BIT* and
*PB_FIELD_32BIT* which tell nanopb what is the maximum size of arrays in use.

**Changes:** Generator will now use *pb_size_t* for the array *_count* fields.
The size of the type will be controlled by the *PB_FIELD_16BIT* and
*PB_FIELD_32BIT* compilation time options.

**Required actions:** Regenerate all *.pb.h* files. In some cases casts to the
*pb_size_t* type may need to be added in the user code when accessing the
*_count* fields.

**Error indications:** Incorrect data at runtime, crashes. But note that other
changes in the same version already require regenerating the files and have
better indications of errors, so this is only an issue for development
versions.

Renamed some macros and identifiers
-----------------------------------
**Rationale:** Some names in nanopb core were badly chosen and conflicted with
ISO C99 reserved names or lacked a prefix. While they haven't caused trouble
so far, it is reasonable to switch to non-conflicting names as these are rarely
used from user code.

**Changes:** The following identifier names have changed:

  * Macros:
  
    * STATIC_ASSERT(x) -> PB_STATIC_ASSERT(x)
    * UNUSED(x) -> PB_UNUSED(x)
  
  * Include guards:
  
    * _PB_filename_ -> PB_filename_INCLUDED
  
  * Structure forward declaration tags:
  
    * _pb_field_t -> pb_field_s
    * _pb_bytes_array_t -> pb_bytes_array_s
    * _pb_callback_t -> pb_callback_s
    * _pb_extension_type_t -> pb_extension_type_s
    * _pb_extension_t -> pb_extension_s
    * _pb_istream_t -> pb_istream_s
    * _pb_ostream_t -> pb_ostream_s

**Required actions:** Regenerate all *.pb.c* files. If you use any of the above
identifiers in your application code, perform search-replace to the new name.

**Error indications:** Compiler errors on lines with the macro/type names.

Nanopb-0.2.9 (2014-08-09)
=========================

Change semantics of generator -e option
---------------------------------------
**Rationale:** Some compilers do not accept filenames with two dots (like
in default extension .pb.c). The *-e* option to the generator allowed changing
the extension, but not skipping the extra dot.

**Changes:** The *-e* option in generator will no longer add the prepending
dot. The default value has been adjusted accordingly to *.pb.c* to keep the
default behaviour the same as before.

**Required actions:** Only if using the generator -e option. Add dot before
the parameter value on the command line.

**Error indications:** File not found when trying to compile generated files.

Nanopb-0.2.7 (2014-04-07)
=========================

Changed pointer-type bytes field datatype
-----------------------------------------
**Rationale:** In the initial pointer encoding support since nanopb-0.2.5,
the bytes type used a separate *pb_bytes_ptr_t* type to represent *bytes*
fields. This made it easy to encode data from a separate, user-allocated
buffer. However, it made the internal logic more complex and was inconsistent
with the other types.

**Changes:** Dynamically allocated bytes fields now have the *pb_bytes_array_t*
type, just like statically allocated ones.

**Required actions:** Only if using pointer-type fields with the bytes datatype.
Change any access to *msg->field.size* to *msg->field->size*. Change any
allocation to reserve space of amount *PB_BYTES_ARRAY_T_ALLOCSIZE(n)*. If the
data pointer was begin assigned from external source, implement the field using
a callback function instead.

**Error indications:** Compiler error: unknown type name *pb_bytes_ptr_t*.

Nanopb-0.2.4 (2013-11-07)
=========================

Remove the NANOPB_INTERNALS compilation option
----------------------------------------------
**Rationale:** Having the option in the headers required the functions to
be non-static, even if the option is not used. This caused errors on some
static analysis tools.

**Changes:** The *#ifdef* and associated functions were removed from the
header.

**Required actions:** Only if the *NANOPB_INTERNALS* option was previously
used. Actions are as listed under nanopb-0.1.3 and nanopb-0.1.6.

**Error indications:** Compiler warning: implicit declaration of function
*pb_dec_string*, *pb_enc_string*, or similar.

Nanopb-0.2.1 (2013-04-14)
=========================

Callback function signature
---------------------------
**Rationale:** Previously the auxilary data to field callbacks was passed
as *void\**. This allowed passing of any data, but made it unnecessarily
complex to return a pointer from callback.

**Changes:** The callback function parameter was changed to *void\*\**.

**Required actions:** You can continue using the old callback style by
defining *PB_OLD_CALLBACK_STYLE*. Recommended action is to:

  * Change the callback signatures to contain *void\*\** for decoders and
    *void \* const \** for encoders.
  * Change the callback function body to use *\*arg* instead of *arg*.

**Error indications:** Compiler warning: assignment from incompatible
pointer type, when initializing *funcs.encode* or *funcs.decode*.

Nanopb-0.2.0 (2013-03-02)
=========================

Reformatted generated .pb.c file using macros
---------------------------------------------
**Rationale:** Previously the generator made a list of C *pb_field_t*
initializers in the .pb.c file. This led to a need to regenerate all .pb.c
files after even small changes to the *pb_field_t* definition.

**Changes:** Macros were added to pb.h which allow for cleaner definition
of the .pb.c contents. By changing the macro definitions, changes to the
field structure are possible without breaking compatibility with old .pb.c
files.

**Required actions:** Regenerate all .pb.c files from the .proto sources.

**Error indications:** Compiler warning: implicit declaration of function
*pb_delta_end*.

Changed pb_type_t definitions
-----------------------------
**Rationale:** The *pb_type_t* was previously an enumeration type. This
caused warnings on some compilers when using bitwise operations to set flags
inside the values.

**Changes:** The *pb_type_t* was changed to *typedef uint8_t*. The values
were changed to *#define*. Some value names were changed for consistency.

**Required actions:** Only if you directly access the `pb_field_t` contents
in your own code, something which is not usually done. Needed changes:

  * Change *PB_HTYPE_ARRAY* to *PB_HTYPE_REPEATED*.
  * Change *PB_HTYPE_CALLBACK* to *PB_ATYPE()* and *PB_ATYPE_CALLBACK*.

**Error indications:** Compiler error: *PB_HTYPE_ARRAY* or *PB_HTYPE_CALLBACK*
undeclared.

Nanopb-0.1.6 (2012-09-02)
=========================

Refactored field decoder interface
----------------------------------
**Rationale:** Similarly to field encoders in nanopb-0.1.3.

**Changes:** New functions with names *pb_decode_\** were added.

**Required actions:** By defining NANOPB_INTERNALS, you can still keep using
the old functions. Recommended action is to replace any calls with the newer
*pb_decode_\** equivalents.

**Error indications:** Compiler warning: implicit declaration of function
*pb_dec_string*, *pb_dec_varint*, *pb_dec_submessage* or similar.

Nanopb-0.1.3 (2012-06-12)
=========================

Refactored field encoder interface
----------------------------------
**Rationale:** The old *pb_enc_\** functions were designed mostly for the
internal use by the core. Because they are internally accessed through
function pointers, their signatures had to be common. This led to a confusing
interface for external users.

**Changes:** New functions with names *pb_encode_\** were added. These have
easier to use interfaces. The old functions are now only thin wrappers for
the new interface.

**Required actions:** By defining NANOPB_INTERNALS, you can still keep using
the old functions. Recommended action is to replace any calls with the newer
*pb_encode_\** equivalents.

**Error indications:** Compiler warning: implicit declaration of function
*pb_enc_string*, *pb_enc_varint, *pb_enc_submessage* or similar.

