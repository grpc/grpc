
macro(_record_compiler_features lang compile_flags feature_list)
  include("${CMAKE_ROOT}/Modules/Compiler/${CMAKE_${lang}_COMPILER_ID}-${lang}-FeatureTests.cmake" OPTIONAL)

  string(TOLOWER ${lang} lang_lc)
  file(REMOVE "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.bin")
  file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.${lang_lc}" "
  const char features[] = {\"\\n\"\n")

  get_property(known_features GLOBAL PROPERTY CMAKE_${lang}_KNOWN_FEATURES)

  foreach(feature ${known_features})
    if (_cmake_feature_test_${feature})
      if (${_cmake_feature_test_${feature}} STREQUAL 1)
        set(_feature_condition "\"1\" ")
      else()
        set(_feature_condition "#if ${_cmake_feature_test_${feature}}\n\"1\"\n#else\n\"0\"\n#endif\n")
      endif()
      file(APPEND "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.${lang_lc}" "\"${lang}_FEATURE:\"\n${_feature_condition}\"${feature}\\n\"\n")
    endif()
  endforeach()
  file(APPEND "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.${lang_lc}"
    "\n};\n\nint main(int argc, char** argv) { (void)argv; return features[argc]; }\n")

  if(CMAKE_${lang}_LINK_WITH_STANDARD_COMPILE_OPTION)
    # This toolchain requires use of the language standard flag
    # when linking in order to use the matching standard library.
    set(compile_flags_for_link "${compile_flags}")
  else()
    set(compile_flags_for_link "")
  endif()

  try_compile(CMAKE_${lang}_FEATURE_TEST
    ${CMAKE_BINARY_DIR} "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.${lang_lc}"
    COMPILE_DEFINITIONS "${compile_flags}"
    LINK_LIBRARIES "${compile_flags_for_link}"
    OUTPUT_VARIABLE _output
    COPY_FILE "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.bin"
    COPY_FILE_ERROR _copy_error
    )
  if(CMAKE_${lang}_FEATURE_TEST AND NOT _copy_error)
    set(_result 0)
  else()
    set(_result 255)
  endif()
  unset(CMAKE_${lang}_FEATURE_TEST CACHE)
  unset(compile_flags_for_link)

  if (_result EQUAL 0)
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "\n\nDetecting ${lang} [${compile_flags}] compiler features compiled with the following output:\n${_output}\n\n")
    if(EXISTS "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.bin")
      file(STRINGS "${CMAKE_BINARY_DIR}/CMakeFiles/feature_tests.bin"
        features REGEX "${lang}_FEATURE:.*")
      foreach(info ${features})
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
          "    Feature record: ${info}\n")
        string(REPLACE "${lang}_FEATURE:" "" info ${info})
        string(SUBSTRING ${info} 0 1 has_feature)
        if(has_feature)
          string(REGEX REPLACE "^1" "" feature ${info})
          list(APPEND ${feature_list} ${feature})
        endif()
      endforeach()
    endif()
  else()
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Detecting ${lang} [${compile_flags}] compiler features failed to compile with the following output:\n${_output}\n${_copy_error}\n\n")
  endif()
endmacro()

macro(_record_compiler_features_c std)
  list(APPEND CMAKE_C${std}_COMPILE_FEATURES c_std_${std})

  get_property(lang_level_has_features GLOBAL PROPERTY CMAKE_C${std}_KNOWN_FEATURES)
  if(lang_level_has_features)
    _record_compiler_features(C "${CMAKE_C${std}_STANDARD_COMPILE_OPTION}" CMAKE_C${std}_COMPILE_FEATURES)
  endif()
  unset(lang_level_has_features)
endmacro()

macro(_record_compiler_features_cxx std)
  list(APPEND CMAKE_CXX${std}_COMPILE_FEATURES cxx_std_${std})

  get_property(lang_level_has_features GLOBAL PROPERTY CMAKE_CXX${std}_KNOWN_FEATURES)
  if(lang_level_has_features)
    _record_compiler_features(CXX "${CMAKE_CXX${std}_STANDARD_COMPILE_OPTION}" CMAKE_CXX${std}_COMPILE_FEATURES)
  endif()
  unset(lang_level_has_features)
endmacro()

macro(_record_compiler_features_cuda std)
  list(APPEND CMAKE_CUDA${std}_COMPILE_FEATURES cuda_std_${std})

  get_property(lang_level_has_features GLOBAL PROPERTY CMAKE_CUDA${std}_KNOWN_FEATURES)
  if(lang_level_has_features)
    _record_compiler_features(CUDA "${CMAKE_CUDA${std}_STANDARD_COMPILE_OPTION}" CMAKE_CUDA${std}_COMPILE_FEATURES)
  endif()
  unset(lang_level_has_features)
endmacro()

macro(_has_compiler_features lang level compile_flags feature_list)
  # presume all known features are supported
  get_property(known_features GLOBAL PROPERTY CMAKE_${lang}${level}_KNOWN_FEATURES)
  list(APPEND ${feature_list} ${known_features})
endmacro()

macro(_has_compiler_features_c std)
  list(APPEND CMAKE_C${std}_COMPILE_FEATURES c_std_${std})
  _has_compiler_features(C ${std} "${CMAKE_C${std}_STANDARD_COMPILE_OPTION}" CMAKE_C${std}_COMPILE_FEATURES)
endmacro()
macro(_has_compiler_features_cxx std)
  list(APPEND CMAKE_CXX${std}_COMPILE_FEATURES cxx_std_${std})
  _has_compiler_features(CXX ${std} "${CMAKE_CXX${std}_STANDARD_COMPILE_OPTION}" CMAKE_CXX${std}_COMPILE_FEATURES)
endmacro()
macro(_has_compiler_features_cuda std)
  list(APPEND CMAKE_CUDA${std}_COMPILE_FEATURES cuda_std_${std})
  _has_compiler_features(CUDA ${std} "${CMAKE_CUDA${std}_STANDARD_COMPILE_OPTION}" CMAKE_CUDA${std}_COMPILE_FEATURES)
endmacro()
