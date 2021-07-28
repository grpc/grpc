CMAKE_CXX_KNOWN_FEATURES
------------------------

.. versionadded:: 3.1

List of C++ features known to this version of CMake.

The features listed in this global property may be known to be available to the
C++ compiler.  If the feature is available with the C++ compiler, it will
be listed in the :variable:`CMAKE_CXX_COMPILE_FEATURES` variable.

The features listed here may be used with the :command:`target_compile_features`
command.  See the :manual:`cmake-compile-features(7)` manual for information on
compile features and a list of supported compilers.

The features known to this version of CMake are listed below.

High level meta features indicating C++ standard support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following meta features indicate general support for the associated
language standard.  It reflects the language support claimed by the compiler,
but it does not necessarily imply complete conformance to that standard.

``cxx_std_98``
  Compiler mode is at least C++ 98.

``cxx_std_11``
  Compiler mode is at least C++ 11.

``cxx_std_14``
  Compiler mode is at least C++ 14.

``cxx_std_17``
  Compiler mode is at least C++ 17.

``cxx_std_20``
  Compiler mode is at least C++ 20.


Low level individual compile features
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For C++ 11 and C++ 14, compilers were sometimes slow to implement certain
language features.  CMake provided some individual compile features to help
projects determine whether specific features were available.  These individual
features are now less relevant and projects should generally prefer to use the
high level meta features instead.  Individual compile features are not provided
for C++ 17 or later.

See the :manual:`cmake-compile-features(7)` manual for further discussion of
the use of individual compile features.

Individual features from C++ 98
"""""""""""""""""""""""""""""""

``cxx_template_template_parameters``
  Template template parameters, as defined in ``ISO/IEC 14882:1998``.


Individual features from C++ 11
"""""""""""""""""""""""""""""""

``cxx_alias_templates``
  Template aliases, as defined in N2258_.

  .. _N2258: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2258.pdf

``cxx_alignas``
  Alignment control ``alignas``, as defined in N2341_.

  .. _N2341: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2341.pdf

``cxx_alignof``
  Alignment control ``alignof``, as defined in N2341_.

  .. _N2341: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2341.pdf

``cxx_attributes``
  Generic attributes, as defined in N2761_.

  .. _N2761: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2761.pdf

``cxx_auto_type``
  Automatic type deduction, as defined in N1984_.

  .. _N1984: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n1984.pdf

``cxx_constexpr``
  Constant expressions, as defined in N2235_.

  .. _N2235: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2235.pdf


``cxx_decltype_incomplete_return_types``
  Decltype on incomplete return types, as defined in N3276_.

  .. _N3276 : http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3276.pdf

``cxx_decltype``
  Decltype, as defined in N2343_.

  .. _N2343: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2343.pdf

``cxx_default_function_template_args``
  Default template arguments for function templates, as defined in DR226_

  .. _DR226: http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#226

``cxx_defaulted_functions``
  Defaulted functions, as defined in N2346_.

  .. _N2346: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2346.htm

``cxx_defaulted_move_initializers``
  Defaulted move initializers, as defined in N3053_.

  .. _N3053: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2010/n3053.html

``cxx_delegating_constructors``
  Delegating constructors, as defined in N1986_.

  .. _N1986: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n1986.pdf

``cxx_deleted_functions``
  Deleted functions, as defined in N2346_.

  .. _N2346: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2346.htm

``cxx_enum_forward_declarations``
  Enum forward declarations, as defined in N2764_.

  .. _N2764: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2764.pdf

``cxx_explicit_conversions``
  Explicit conversion operators, as defined in N2437_.

  .. _N2437: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2437.pdf

``cxx_extended_friend_declarations``
  Extended friend declarations, as defined in N1791_.

  .. _N1791: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2005/n1791.pdf

``cxx_extern_templates``
  Extern templates, as defined in N1987_.

  .. _N1987: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n1987.htm

``cxx_final``
  Override control ``final`` keyword, as defined in N2928_, N3206_ and N3272_.

  .. _N2928: http://www.open-std.org/JTC1/SC22/WG21/docs/papers/2009/n2928.htm
  .. _N3206: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2010/n3206.htm
  .. _N3272: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3272.htm

``cxx_func_identifier``
  Predefined ``__func__`` identifier, as defined in N2340_.

  .. _N2340: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2340.htm

``cxx_generalized_initializers``
  Initializer lists, as defined in N2672_.

  .. _N2672: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2672.htm

``cxx_inheriting_constructors``
  Inheriting constructors, as defined in N2540_.

  .. _N2540: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2540.htm

``cxx_inline_namespaces``
  Inline namespaces, as defined in N2535_.

  .. _N2535: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2535.htm

``cxx_lambdas``
  Lambda functions, as defined in N2927_.

  .. _N2927: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2009/n2927.pdf

``cxx_local_type_template_args``
  Local and unnamed types as template arguments, as defined in N2657_.

  .. _N2657: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2657.htm

``cxx_long_long_type``
  ``long long`` type, as defined in N1811_.

  .. _N1811: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2005/n1811.pdf

``cxx_noexcept``
  Exception specifications, as defined in N3050_.

  .. _N3050: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2010/n3050.html

``cxx_nonstatic_member_init``
  Non-static data member initialization, as defined in N2756_.

  .. _N2756: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2756.htm

``cxx_nullptr``
  Null pointer, as defined in N2431_.

  .. _N2431: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2431.pdf

``cxx_override``
  Override control ``override`` keyword, as defined in N2928_, N3206_
  and N3272_.

  .. _N2928: http://www.open-std.org/JTC1/SC22/WG21/docs/papers/2009/n2928.htm
  .. _N3206: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2010/n3206.htm
  .. _N3272: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3272.htm

``cxx_range_for``
  Range-based for, as defined in N2930_.

  .. _N2930: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2009/n2930.html

``cxx_raw_string_literals``
  Raw string literals, as defined in N2442_.

  .. _N2442: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2442.htm

``cxx_reference_qualified_functions``
  Reference qualified functions, as defined in N2439_.

  .. _N2439: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2439.htm

``cxx_right_angle_brackets``
  Right angle bracket parsing, as defined in N1757_.

  .. _N1757: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2005/n1757.html

``cxx_rvalue_references``
  R-value references, as defined in N2118_.

  .. _N2118: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2118.html

``cxx_sizeof_member``
  Size of non-static data members, as defined in N2253_.

  .. _N2253: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2253.html

``cxx_static_assert``
  Static assert, as defined in N1720_.

  .. _N1720: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2004/n1720.html

``cxx_strong_enums``
  Strongly typed enums, as defined in N2347_.

  .. _N2347: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2347.pdf

``cxx_thread_local``
  Thread-local variables, as defined in N2659_.

  .. _N2659: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2659.htm

``cxx_trailing_return_types``
  Automatic function return type, as defined in N2541_.

  .. _N2541: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2541.htm

``cxx_unicode_literals``
  Unicode string literals, as defined in N2442_.

  .. _N2442: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2442.htm

``cxx_uniform_initialization``
  Uniform initialization, as defined in N2640_.

  .. _N2640: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2640.pdf

``cxx_unrestricted_unions``
  Unrestricted unions, as defined in N2544_.

  .. _N2544: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2544.pdf

``cxx_user_literals``
  User-defined literals, as defined in N2765_.

  .. _N2765: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2765.pdf

``cxx_variadic_macros``
  Variadic macros, as defined in N1653_.

  .. _N1653: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2004/n1653.htm

``cxx_variadic_templates``
  Variadic templates, as defined in N2242_.

  .. _N2242: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2242.pdf


Individual features from C++ 14
"""""""""""""""""""""""""""""""

``cxx_aggregate_default_initializers``
  Aggregate default initializers, as defined in N3605_.

  .. _N3605: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3605.html

``cxx_attribute_deprecated``
  ``[[deprecated]]`` attribute, as defined in N3760_.

  .. _N3760: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3760.html

``cxx_binary_literals``
  Binary literals, as defined in N3472_.

  .. _N3472: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3472.pdf

``cxx_contextual_conversions``
  Contextual conversions, as defined in N3323_.

  .. _N3323: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3323.pdf

``cxx_decltype_auto``
  ``decltype(auto)`` semantics, as defined in N3638_.

  .. _N3638: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3638.html

``cxx_digit_separators``
  Digit separators, as defined in N3781_.

  .. _N3781: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3781.pdf

``cxx_generic_lambdas``
  Generic lambdas, as defined in N3649_.

  .. _N3649: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3649.html

``cxx_lambda_init_captures``
  Initialized lambda captures, as defined in N3648_.

  .. _N3648: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3648.html

``cxx_relaxed_constexpr``
  Relaxed constexpr, as defined in N3652_.

  .. _N3652: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3652.html

``cxx_return_type_deduction``
  Return type deduction on normal functions, as defined in N3386_.

  .. _N3386: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3386.html

``cxx_variable_templates``
  Variable templates, as defined in N3651_.

  .. _N3651: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3651.pdf
