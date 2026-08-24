# Unit tests for farmcore (Qt Test + CTest). Run: ctest --preset msvc-x64  (or scripts\test.bat)
find_package(Qt6 REQUIRED COMPONENTS Test)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FARM_TEST_ARCH x64)
else()
    set(FARM_TEST_ARCH x86)
endif()
set(FARM_TEST_OUTPUT "${CMAKE_SOURCE_DIR}/output/${FARM_TEST_ARCH}/${CMAKE_BUILD_TYPE}/tests")

function(farm_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE farmcore Qt6::Test)
    set_target_properties(${name} PROPERTIES
        AUTOMOC ON
        RUNTIME_OUTPUT_DIRECTORY "${FARM_TEST_OUTPUT}/$<0:>")
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3 /WX /utf-8)
    endif()
    add_test(NAME ${name} COMMAND ${name} -silent)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

farm_add_test(tst_ipv4)
farm_add_test(tst_adbparsers)
farm_add_test(tst_allocator)
farm_add_test(tst_database)
farm_add_test(tst_registry)
farm_add_test(tst_batchjob)
farm_add_test(tst_expression)
farm_add_test(tst_workflow)
farm_add_test(tst_scheduler)
farm_add_test(tst_imagematcher)
farm_add_test(tst_uihierarchy)

# The test binaries need the Qt runtime beside them: deploy once per build.
add_custom_target(deploy_tests ALL
    COMMAND "${QT6_INSTALL_PREFIX}/bin/windeployqt.exe" --no-translations --no-compiler-runtime "${FARM_TEST_OUTPUT}/tst_registry.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/QtScrcpy/QtScrcpyCore/src/third_party/ffmpeg/bin/${FARM_TEST_ARCH}/avcodec-58.dll" "${FARM_TEST_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/QtScrcpy/QtScrcpyCore/src/third_party/ffmpeg/bin/${FARM_TEST_ARCH}/avformat-58.dll" "${FARM_TEST_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/QtScrcpy/QtScrcpyCore/src/third_party/ffmpeg/bin/${FARM_TEST_ARCH}/avutil-56.dll" "${FARM_TEST_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/QtScrcpy/QtScrcpyCore/src/third_party/ffmpeg/bin/${FARM_TEST_ARCH}/swscale-5.dll" "${FARM_TEST_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/QtScrcpy/QtScrcpyCore/src/third_party/ffmpeg/bin/${FARM_TEST_ARCH}/swresample-3.dll" "${FARM_TEST_OUTPUT}"
    DEPENDS tst_ipv4 tst_adbparsers tst_allocator tst_database tst_registry tst_batchjob tst_expression tst_workflow tst_scheduler tst_imagematcher tst_uihierarchy
    COMMENT "Deploying Qt runtime for tests"
    VERBATIM)
