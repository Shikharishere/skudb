include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v.3.4.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(subDB_tests test.cpp)

target_link_libraries(skuDB_tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(skuDB_tests)