include_directories(${CMAKE_SOURCE_DIR}/test/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party/catch)
include_directories(${CMAKE_SOURCE_DIR}/third_party)
include_directories(${DUCKDB_MODULE_BASE_DIR}/third_party/httplib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../src/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(unittest_httpfs EXCLUDE_FROM_ALL main.cpp)
add_subdirectory(http)
add_subdirectory(s3)

set_target_properties(unittest_httpfs PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                 ${CMAKE_BINARY_DIR}/test)

target_link_libraries(unittest_httpfs test_helpers duckdb_static)
# The statically linked extension list is only resolved after every extension is configured, so name them here
duckdb_link_extensions(unittest_httpfs core_functions json parquet httpfs)
link_threads(unittest_httpfs "")
