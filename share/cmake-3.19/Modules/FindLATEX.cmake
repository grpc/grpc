# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLATEX
---------

Find LaTeX

This module finds an installed LaTeX and determines the location
of the compiler.  Additionally the module looks for Latex-related
software like BibTeX.

This module sets the following result variables::

  LATEX_FOUND:          whether found Latex and requested components
  LATEX_<component>_FOUND:  whether found <component>
  LATEX_COMPILER:       path to the LaTeX compiler
  PDFLATEX_COMPILER:    path to the PdfLaTeX compiler
  XELATEX_COMPILER:     path to the XeLaTeX compiler
  LUALATEX_COMPILER:    path to the LuaLaTeX compiler
  BIBTEX_COMPILER:      path to the BibTeX compiler
  BIBER_COMPILER:       path to the Biber compiler
  MAKEINDEX_COMPILER:   path to the MakeIndex compiler
  XINDY_COMPILER:       path to the xindy compiler
  DVIPS_CONVERTER:      path to the DVIPS converter
  DVIPDF_CONVERTER:     path to the DVIPDF converter
  PS2PDF_CONVERTER:     path to the PS2PDF converter
  PDFTOPS_CONVERTER:    path to the pdftops converter
  LATEX2HTML_CONVERTER: path to the LaTeX2Html converter
  HTLATEX_COMPILER:     path to the htlatex compiler

Possible components are::

  PDFLATEX
  XELATEX
  LUALATEX
  BIBTEX
  BIBER
  MAKEINDEX
  XINDY
  DVIPS
  DVIPDF
  PS2PDF
  PDFTOPS
  LATEX2HTML
  HTLATEX

Example Usages::

  find_package(LATEX)
  find_package(LATEX COMPONENTS PDFLATEX)
  find_package(LATEX COMPONENTS BIBTEX PS2PDF)
#]=======================================================================]

if (WIN32)
  # Try to find the MikTex binary path (look for its package manager).
  find_path(MIKTEX_BINARY_PATH mpm.exe
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MiK\\MiKTeX\\CurrentVersion\\MiKTeX;Install Root]/miktex/bin"
    DOC
    "Path to the MikTex binary directory."
  )
  mark_as_advanced(MIKTEX_BINARY_PATH)

  # Try to find the GhostScript binary path (look for gswin32).
  get_filename_component(GHOSTSCRIPT_BINARY_PATH_FROM_REGISTERY_8_00
     "[HKEY_LOCAL_MACHINE\\SOFTWARE\\AFPL Ghostscript\\8.00;GS_DLL]" PATH
  )

  get_filename_component(GHOSTSCRIPT_BINARY_PATH_FROM_REGISTERY_7_04
     "[HKEY_LOCAL_MACHINE\\SOFTWARE\\AFPL Ghostscript\\7.04;GS_DLL]" PATH
  )

  find_path(GHOSTSCRIPT_BINARY_PATH gswin32.exe
    ${GHOSTSCRIPT_BINARY_PATH_FROM_REGISTERY_8_00}
    ${GHOSTSCRIPT_BINARY_PATH_FROM_REGISTERY_7_04}
    DOC "Path to the GhostScript binary directory."
  )
  mark_as_advanced(GHOSTSCRIPT_BINARY_PATH)

  find_path(GHOSTSCRIPT_LIBRARY_PATH ps2pdf13.bat
    "${GHOSTSCRIPT_BINARY_PATH}/../lib"
    DOC "Path to the GhostScript library directory."
  )
  mark_as_advanced(GHOSTSCRIPT_LIBRARY_PATH)
endif ()

# try to find Latex and the related programs
find_program(LATEX_COMPILER
  NAMES latex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)

# find pdflatex
find_program(PDFLATEX_COMPILER
  NAMES pdflatex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (PDFLATEX_COMPILER)
  set(LATEX_PDFLATEX_FOUND TRUE)
else()
  set(LATEX_PDFLATEX_FOUND FALSE)
endif()

# find xelatex
find_program(XELATEX_COMPILER
  NAMES xelatex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (XELATEX_COMPILER)
  set(LATEX_XELATEX_FOUND TRUE)
else()
  set(LATEX_XELATEX_FOUND FALSE)
endif()

# find lualatex
find_program(LUALATEX_COMPILER
  NAMES lualatex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (LUALATEX_COMPILER)
  set(LATEX_LUALATEX_FOUND TRUE)
else()
  set(LATEX_LUALATEX_FOUND FALSE)
endif()

# find bibtex
find_program(BIBTEX_COMPILER
  NAMES bibtex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (BIBTEX_COMPILER)
  set(LATEX_BIBTEX_FOUND TRUE)
else()
  set(LATEX_BIBTEX_FOUND FALSE)
endif()

# find biber
find_program(BIBER_COMPILER
  NAMES biber
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (BIBER_COMPILER)
  set(LATEX_BIBER_FOUND TRUE)
else()
  set(LATEX_BIBER_FOUND FALSE)
endif()

# find makeindex
find_program(MAKEINDEX_COMPILER
  NAMES makeindex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (MAKEINDEX_COMPILER)
  set(LATEX_MAKEINDEX_FOUND TRUE)
else()
  set(LATEX_MAKEINDEX_FOUND FALSE)
endif()

# find xindy
find_program(XINDY_COMPILER
  NAMES xindy
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (XINDY_COMPILER)
  set(LATEX_XINDY_FOUND TRUE)
else()
  set(LATEX_XINDY_FOUND FALSE)
endif()

# find dvips
find_program(DVIPS_CONVERTER
  NAMES dvips
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (DVIPS_CONVERTER)
  set(LATEX_DVIPS_FOUND TRUE)
else()
  set(LATEX_DVIPS_FOUND FALSE)
endif()

# find dvipdf
find_program(DVIPDF_CONVERTER
  NAMES dvipdfm dvipdft dvipdf
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (DVIPDF_CONVERTER)
  set(LATEX_DVIPDF_FOUND TRUE)
else()
  set(LATEX_DVIPDF_FOUND FALSE)
endif()

# find ps2pdf
if (WIN32)
  find_program(PS2PDF_CONVERTER
    NAMES ps2pdf14.bat ps2pdf14 ps2pdf
    PATHS ${GHOSTSCRIPT_LIBRARY_PATH}
          ${MIKTEX_BINARY_PATH}
  )
else ()
  find_program(PS2PDF_CONVERTER
    NAMES ps2pdf14 ps2pdf
  )
endif ()
if (PS2PDF_CONVERTER)
  set(LATEX_PS2PDF_FOUND TRUE)
else()
  set(LATEX_PS2PDF_FOUND FALSE)
endif()

# find pdftops
find_program(PDFTOPS_CONVERTER
  NAMES pdftops
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (PDFTOPS_CONVERTER)
  set(LATEX_PDFTOPS_FOUND TRUE)
else()
  set(LATEX_PDFTOPS_FOUND FALSE)
endif()

# find latex2html
find_program(LATEX2HTML_CONVERTER
  NAMES latex2html
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (LATEX2HTML_CONVERTER)
  set(LATEX_LATEX2HTML_FOUND TRUE)
else()
  set(LATEX_LATEX2HTML_FOUND FALSE)
endif()

# find htlatex
find_program(HTLATEX_COMPILER
  NAMES htlatex
  PATHS ${MIKTEX_BINARY_PATH}
        /usr/bin
)
if (HTLATEX_COMPILER)
  set(LATEX_HTLATEX_FOUND TRUE)
else()
  set(LATEX_HTLATEX_FOUND FALSE)
endif()


mark_as_advanced(
  LATEX_COMPILER
  PDFLATEX_COMPILER
  XELATEX_COMPILER
  LUALATEX_COMPILER
  BIBTEX_COMPILER
  BIBER_COMPILER
  MAKEINDEX_COMPILER
  XINDY_COMPILER
  DVIPS_CONVERTER
  DVIPDF_CONVERTER
  PS2PDF_CONVERTER
  PDFTOPS_CONVERTER
  LATEX2HTML_CONVERTER
  HTLATEX_COMPILER
)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(LATEX
  REQUIRED_VARS LATEX_COMPILER
  HANDLE_COMPONENTS
)
