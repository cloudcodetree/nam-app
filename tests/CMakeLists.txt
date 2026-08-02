include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(nam_tests
    test_main.cpp
    test_scaffold.cpp          # later tasks append their test files here
    test_nammodel.cpp
    test_gain.cpp
    test_toneengine.cpp
    test_modelhost.cpp
    test_noisegate.cpp
    test_toneeq.cpp
    test_ircab.cpp
    test_irloader.cpp
    test_librarystore.cpp
    test_libraryimporter.cpp
    test_pkce.cpp
    ${CMAKE_SOURCE_DIR}/Source/model/IrLoader.cpp
    ${CMAKE_SOURCE_DIR}/Source/model/LibraryStore.cpp
    ${CMAKE_SOURCE_DIR}/Source/model/LibraryImporter.cpp
    ${CMAKE_SOURCE_DIR}/Source/net/Pkce.cpp)
target_compile_features(nam_tests PRIVATE cxx_std_17)
target_include_directories(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source ${CMAKE_SOURCE_DIR}/extern/dr_wav
    ${CMAKE_SOURCE_DIR}/extern/NeuralAudio/deps/NeuralAmpModelerCore/Dependencies/nlohmann
    ${CMAKE_SOURCE_DIR}/extern/picosha2)
target_link_libraries(nam_tests PRIVATE NeuralAudio Catch2::Catch2WithMain)
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/model/NamModel.cpp)
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/dsp/ToneEngine.cpp)
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/model/ModelHost.cpp)
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/dsp/ToneEq.cpp)
target_sources(nam_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source/dsp/IrCab.cpp)
target_compile_definitions(nam_tests PRIVATE
    NAM_FIXTURE_A2="${CMAKE_SOURCE_DIR}/tests/fixtures/example_a2.nam"
    # A real A2 amp capture (SlimmableContainer architecture) for auditioning
    # and for regression coverage of the production A2 format.
    NAM_MODEL_A2_REAL="${CMAKE_SOURCE_DIR}/models/A2.nam")

include(CTest)
include(Catch)
catch_discover_tests(nam_tests)
