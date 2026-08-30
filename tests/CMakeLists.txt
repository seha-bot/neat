find_package(Catch2 3 REQUIRED)

function(register_test name path)
  add_executable(${name} ${path})
  target_link_libraries(${name} PRIVATE
    neat_lib
    Catch2::Catch2WithMain
  )
  add_test(NAME ${name} COMMAND ${name})
endfunction()

register_test(typechecker_test typechecker/typechecker_test.cpp)
register_test(type_storage_test common/type_storage_test.cpp)
