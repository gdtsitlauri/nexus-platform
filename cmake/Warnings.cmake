function(nexus_set_warnings target_name)
  if(MSVC)
    target_compile_options("${target_name}" PRIVATE /W4 /permissive-)
  else()
    target_compile_options(
      "${target_name}"
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion)
  endif()
endfunction()
