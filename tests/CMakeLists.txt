include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(nam_tests
    test_main.cpp
    test_scaffold.cpp)          # later tasks append their test files here
target_compile_features(nam_tests PRIVATE cxx_std_17)
target_include_directories(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(nam_tests PRIVATE NeuralAudio Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(nam_tests)
