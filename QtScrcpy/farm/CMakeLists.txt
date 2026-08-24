# farmcore — the UI-independent services of the ADB Device Farm.
# Linked by the QtScrcpy executable (farm UI) and by the unit tests.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Sql Concurrent)

set(FARMCORE_SOURCES
    core/ipv4.h
    core/ipv4.cpp
    core/farmlog.h
    core/farmlog.cpp
    core/farmsettings.h
    core/farmsettings.cpp
    core/activitylog.h
    core/activitylog.cpp
    core/taskexecutor.h
    core/taskexecutor.cpp
    core/batchjob.h
    core/batchjob.cpp
    core/appcontext.h
    core/appcontext.cpp
    adb/adbquote.h
    adb/adbparsers.h
    adb/adbparsers.cpp
    adb/adbexecutor.h
    adb/adbexecutor.cpp
    storage/database.h
    storage/database.cpp
    storage/repositories.h
    storage/repositories.cpp
    devices/devicerecord.h
    devices/devicerecord.cpp
    devices/connectionidallocator.h
    devices/connectionidallocator.cpp
    devices/deviceregistry.h
    devices/deviceregistry.cpp
    devices/deviceservice.h
    devices/deviceservice.cpp
    devices/keepawakemanager.h
    devices/keepawakemanager.cpp
    devices/devicehealthmonitor.h
    devices/devicehealthmonitor.cpp
    discovery/networkscanner.h
    discovery/networkscanner.cpp
    discovery/devicediscoveryservice.h
    discovery/devicediscoveryservice.cpp
    devices/devicecommands.h
    devices/devicecommands.cpp
    performance/perfmonitor.h
    performance/perfmonitor.cpp
    mock/mockdeviceprovider.h
    mock/mockdeviceprovider.cpp
    automation/workflowmodel.h
    automation/workflowmodel.cpp
    automation/nodecatalog.h
    automation/nodecatalog.cpp
    automation/expression.h
    automation/expression.cpp
    automation/imagematcher.h
    automation/imagematcher.cpp
    automation/ocrprovider.h
    automation/ocrprovider.cpp
    automation/uihierarchy.h
    automation/uihierarchy.cpp
    automation/workflowengine.h
    automation/workflowengine.cpp
    automation/macrorecorder.h
    automation/macrorecorder.cpp
    automation/aiprovider.h
    automation/aiprovider.cpp
    scheduler/scheduler.h
    scheduler/scheduler.cpp
)

add_library(farmcore STATIC ${FARMCORE_SOURCES})
set_target_properties(farmcore PROPERTIES AUTOMOC ON)
target_compile_features(farmcore PUBLIC cxx_std_17)
# C++/WinRT (Windows OCR) needs C++20 coroutines inside this library only
target_compile_features(farmcore PRIVATE cxx_std_20)
target_include_directories(farmcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(farmcore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    QtScrcpyCore
)
if(MSVC)
    # C++/WinRT (Windows built-in OCR) needs windowsapp.lib and permissive- off
    target_link_libraries(farmcore PUBLIC windowsapp)
    target_compile_options(farmcore PRIVATE /W3 /WX /utf-8 /permissive- /bigobj)
    target_compile_definitions(farmcore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
