include(rpcgen)
rpcgen(
  TARGET demo_gen
  IN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/gen.json
  OUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/simple_service.h
  INCLUDES ${CMAKE_BINARY_DIR}/src/v
  )

add_executable(demo_server server.cc)
target_link_libraries(demo_server PUBLIC demo_gen v::model)
set_property(TARGET demo_server PROPERTY POSITION_INDEPENDENT_CODE ON)

if(CMAKE_BUILD_TYPE MATCHES Release)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ltosupported OUTPUT error)
  if(ltosupported)
    set_property(TARGET demo_server PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()

# client
add_executable(demo_client client.cc)
target_link_libraries(demo_client PUBLIC demo_gen v::model)
set_property(TARGET demo_client PROPERTY POSITION_INDEPENDENT_CODE ON)


rp_test(
  UNIT_TEST
  BINARY_NAME roundtrip_demo_types
  SOURCES type_tests.cc
  LIBRARIES v::seastar_testing_main demo_gen v::model
  ARGS "-- -c 1"
)
