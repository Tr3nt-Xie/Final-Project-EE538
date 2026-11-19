# Applies a strict warning set to an INTERFACE target.
function(aegis_apply_warnings target)
  set(gcc_clang_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wunused)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND gcc_clang_warnings
      -Wduplicated-cond
      -Wduplicated-branches
      -Wlogical-op
      -Wuseless-cast)
  endif()

  if(AEGIS_WARNINGS_AS_ERRORS)
    list(APPEND gcc_clang_warnings -Werror)
  endif()

  target_compile_options(${target} INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${gcc_clang_warnings}>)
endfunction()
