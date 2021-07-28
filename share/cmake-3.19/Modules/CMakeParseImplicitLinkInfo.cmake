# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_policy(PUSH)
cmake_policy(SET CMP0053 NEW)
cmake_policy(SET CMP0054 NEW)

# Function parse implicit linker options.
# This is used internally by CMake and should not be included by user
# code.

function(CMAKE_PARSE_IMPLICIT_LINK_INFO text lib_var dir_var fwk_var log_var obj_regex)
  set(implicit_libs_tmp "")
  set(implicit_dirs_tmp)
  set(implicit_fwks_tmp)
  set(log "")

  # Parse implicit linker arguments.
  set(linker "CMAKE_LINKER-NOTFOUND")
  if(CMAKE_LINKER)
    get_filename_component(linker ${CMAKE_LINKER} NAME)
    string(REGEX REPLACE "([][+.*?()^$])" "\\\\\\1" linker "${linker}")
  endif()
  set(startfile "CMAKE_LINK_STARTFILE-NOTFOUND")
  if(CMAKE_LINK_STARTFILE)
    set(startfile "${CMAKE_LINK_STARTFILE}")
  endif()
  # Construct a regex to match linker lines.  It must match both the
  # whole line and just the command (argv[0]).
  set(linker_regex "^( *|.*[/\\])(${linker}|${startfile}|([^/\\]+-)?ld|collect2)[^/\\]*( |$)")
  set(linker_exclude_regex "collect2 version |^[A-Za-z0-9_]+=|/ldfe ")
  string(APPEND log "  link line regex: [${linker_regex}]\n")
  string(REGEX REPLACE "\r?\n" ";" output_lines "${text}")
  foreach(line IN LISTS output_lines)
    set(cmd)
    if("${line}" MATCHES "${linker_regex}" AND
        NOT "${line}" MATCHES "${linker_exclude_regex}")
      if(XCODE)
        # Xcode unconditionally adds a path under the project build tree and
        # on older versions it is not reported with proper quotes.  Remove it.
        string(REGEX REPLACE "([][+.*()^])" "\\\\\\1" _dir_regex "${CMAKE_BINARY_DIR}")
        string(REGEX REPLACE " -[FL]${_dir_regex}/([^ ]| [^-])+( |$)" " " xline "${line}")
        if(NOT "x${xline}" STREQUAL "x${line}")
          string(APPEND log "  reduced line: [${line}]\n            to: [${xline}]\n")
          set(line "${xline}")
        endif()
      endif()
      separate_arguments(args NATIVE_COMMAND "${line}")
      list(GET args 0 cmd)
    else()
      #check to see if the link line is comma-separated instead of space separated
      string(REGEX REPLACE "," " " line "${line}")
      if("${line}" MATCHES "${linker_regex}" AND
        NOT "${line}" MATCHES "${linker_exclude_regex}")
        separate_arguments(args NATIVE_COMMAND "${line}")
        list(GET args 0 cmd)
        if("${cmd}" MATCHES "exec:")
          # ibm xl sometimes has 'exec: ' in-front of the linker
          list(GET args 1 cmd)
        endif()
      endif()
    endif()
    set(is_msvc 0)
    if("${cmd}" MATCHES "${linker_regex}")
      string(APPEND log "  link line: [${line}]\n")
      string(REGEX REPLACE ";-([LYz]);" ";-\\1" args "${args}")
      set(skip_value_of "")
      foreach(arg IN LISTS args)
        if(skip_value_of)
          string(APPEND log "    arg [${arg}] ==> skip value of ${skip_value_of}\n")
          set(skip_value_of "")
        elseif("${arg}" MATCHES "^-L(.:)?[/\\]")
          # Unix search path.
          string(REGEX REPLACE "^-L" "" dir "${arg}")
          list(APPEND implicit_dirs_tmp ${dir})
          string(APPEND log "    arg [${arg}] ==> dir [${dir}]\n")
        elseif("${arg}" MATCHES "^[-/](LIBPATH|libpath):(.+)")
          # MSVC search path.
          set(dir "${CMAKE_MATCH_2}")
          list(APPEND implicit_dirs_tmp ${dir})
          string(APPEND log "    arg [${arg}] ==> dir [${dir}]\n")
        elseif(is_msvc AND "${arg}" STREQUAL "-link")
          string(APPEND log "    arg [${arg}] ==> ignore MSVC cl option\n")
        elseif(is_msvc AND "${arg}" MATCHES "^(.*\\.[Ll][Ii][Bb])$")
          set(lib "${CMAKE_MATCH_1}")
          list(APPEND implicit_libs_tmp ${lib})
          string(APPEND log "    arg [${arg}] ==> lib [${lib}]\n")
        elseif("${arg}" STREQUAL "-lto_library")
          # ld argument "-lto_library <path>"
          set(skip_value_of "${arg}")
          string(APPEND log "    arg [${arg}] ==> ignore, skip following value\n")
        elseif("${arg}" MATCHES "^-l([^:].*)$")
          # Unix library.
          set(lib "${CMAKE_MATCH_1}")
          list(APPEND implicit_libs_tmp ${lib})
          string(APPEND log "    arg [${arg}] ==> lib [${lib}]\n")
        elseif("${arg}" MATCHES "^(.:)?[/\\].*\\.a$")
          # Unix library full path.
          list(APPEND implicit_libs_tmp ${arg})
          string(APPEND log "    arg [${arg}] ==> lib [${arg}]\n")
        elseif("${arg}" MATCHES "^[-/](DEFAULTLIB|defaultlib):(.+)")
          # Windows library.
          set(lib "${CMAKE_MATCH_2}")
          list(APPEND implicit_libs_tmp ${lib})
          string(APPEND log "    arg [${arg}] ==> lib [${lib}]\n")
        elseif("${arg}" MATCHES "^(.:)?[/\\].*\\.o$"
            AND obj_regex AND "${arg}" MATCHES "${obj_regex}")
          # Object file full path.
          list(APPEND implicit_libs_tmp ${arg})
          string(APPEND log "    arg [${arg}] ==> obj [${arg}]\n")
        elseif("${arg}" MATCHES "^-Y(P,)?[^0-9]")
          # Sun search path ([^0-9] avoids conflict with Mac -Y<num>).
          string(REGEX REPLACE "^-Y(P,)?" "" dirs "${arg}")
          string(REPLACE ":" ";" dirs "${dirs}")
          list(APPEND implicit_dirs_tmp ${dirs})
          string(APPEND log "    arg [${arg}] ==> dirs [${dirs}]\n")
        elseif("${arg}" MATCHES "^-l:")
          # HP named library.
          list(APPEND implicit_libs_tmp ${arg})
          string(APPEND log "    arg [${arg}] ==> lib [${arg}]\n")
        elseif("${arg}" MATCHES "^-z(all|default|weak)extract")
          # Link editor option.
          list(APPEND implicit_libs_tmp ${arg})
          string(APPEND log "    arg [${arg}] ==> opt [${arg}]\n")
        elseif("${arg}" STREQUAL "cl.exe")
          string(APPEND log "    arg [${arg}] ==> recognize MSVC cl\n")
          set(is_msvc 1)
        else()
          string(APPEND log "    arg [${arg}] ==> ignore\n")
        endif()
      endforeach()
      break()
    elseif("${line}" MATCHES "LPATH(=| is:? *)(.*)$")
      string(APPEND log "  LPATH line: [${line}]\n")
      # HP search path.
      string(REPLACE ":" ";" paths "${CMAKE_MATCH_2}")
      list(APPEND implicit_dirs_tmp ${paths})
      string(APPEND log "    dirs [${paths}]\n")
    else()
      string(APPEND log "  ignore line: [${line}]\n")
    endif()
  endforeach()

  # Look for library search paths reported by linker.
  if("${output_lines}" MATCHES ";Library search paths:((;\t[^;]+)+)")
    string(REPLACE ";\t" ";" implicit_dirs_match "${CMAKE_MATCH_1}")
    string(APPEND log "  Library search paths: [${implicit_dirs_match}]\n")
    list(APPEND implicit_dirs_tmp ${implicit_dirs_match})
  endif()
  if("${output_lines}" MATCHES ";Framework search paths:((;\t[^;]+)+)")
    string(REPLACE ";\t" ";" implicit_fwks_match "${CMAKE_MATCH_1}")
    string(APPEND log "  Framework search paths: [${implicit_fwks_match}]\n")
    list(APPEND implicit_fwks_tmp ${implicit_fwks_match})
  endif()

  # Cleanup list of libraries and flags.
  # We remove items that are not language-specific.
  set(implicit_libs "")
  foreach(lib IN LISTS implicit_libs_tmp)
    if("x${lib}" MATCHES "^x(crt.*\\.o|gcc_eh.*|.*libgcc_eh.*|System.*|.*libclang_rt.*|msvcrt.*|libvcruntime.*|libucrt.*|libcmt.*)$")
      string(APPEND log "  remove lib [${lib}]\n")
    elseif(IS_ABSOLUTE "${lib}")
      get_filename_component(abs "${lib}" ABSOLUTE)
      if(NOT "x${lib}" STREQUAL "x${abs}")
        string(APPEND log "  collapse lib [${lib}] ==> [${abs}]\n")
      endif()
      list(APPEND implicit_libs "${abs}")
    else()
      list(APPEND implicit_libs "${lib}")
    endif()
  endforeach()

  # Cleanup list of library and framework directories.
  set(desc_dirs "library")
  set(desc_fwks "framework")
  foreach(t dirs fwks)
    set(implicit_${t} "")
    foreach(d IN LISTS implicit_${t}_tmp)
      get_filename_component(dir "${d}" ABSOLUTE)
      string(FIND "${dir}" "${CMAKE_FILES_DIRECTORY}/" pos)
      if(NOT pos LESS 0)
        set(msg ", skipping non-system directory")
      else()
        set(msg "")
        list(APPEND implicit_${t} "${dir}")
      endif()
      string(APPEND log "  collapse ${desc_${t}} dir [${d}] ==> [${dir}]${msg}\n")
    endforeach()
    list(REMOVE_DUPLICATES implicit_${t})
  endforeach()

  # Log results.
  string(APPEND log "  implicit libs: [${implicit_libs}]\n")
  string(APPEND log "  implicit dirs: [${implicit_dirs}]\n")
  string(APPEND log "  implicit fwks: [${implicit_fwks}]\n")

  # Return results.
  set(${lib_var} "${implicit_libs}" PARENT_SCOPE)
  set(${dir_var} "${implicit_dirs}" PARENT_SCOPE)
  set(${fwk_var} "${implicit_fwks}" PARENT_SCOPE)
  set(${log_var} "${log}" PARENT_SCOPE)
endfunction()

cmake_policy(POP)
