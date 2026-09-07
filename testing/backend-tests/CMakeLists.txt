cmake_minimum_required(VERSION 3.15)
project(DolphinSvnPluginBackendTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Xml Test Widgets)

enable_testing()

# code/svn/ ist READ-ONLY fuer diese Testumgebung. Die Quellen werden per
# Pfadangabe direkt kompiliert (nicht kopiert), damit sie garantiert nicht
# vom Plugin-Build abweichen.
set(SVN_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../svn")

add_library(svnmanager_under_test STATIC
    "${SVN_BACKEND_DIR}/svnmanager.cpp"
    "${SVN_BACKEND_DIR}/svnrevrange.cpp"
)
target_include_directories(svnmanager_under_test PUBLIC "${SVN_BACKEND_DIR}")
target_link_libraries(svnmanager_under_test PUBLIC Qt6::Core Qt6::Xml)

add_library(testfixtures STATIC
    fixtures/TestFixtures.cpp
)
target_include_directories(testfixtures PUBLIC fixtures)
target_link_libraries(testfixtures PUBLIC Qt6::Core Qt6::Test svnmanager_under_test)

# Registriert ein QTest-Testtarget als CTest-Test.
function(add_backend_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test svnmanager_under_test testfixtures)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

add_backend_test(tst_smoke)
add_backend_test(tst_parsestatus)
add_backend_test(tst_parselog)
add_backend_test(tst_conflicts)
add_backend_test(tst_merge)
add_backend_test(tst_diffexport)
add_backend_test(tst_asyncops)
add_backend_test(tst_repoops)
add_backend_test(tst_syncmisc)
add_backend_test(tst_revrange)
add_backend_test(tst_mergemessage)
add_backend_test(tst_remotestatus)
add_backend_test(tst_patch)

# Widget-Logik-Test: die Checkbox-Kaskade (plugin/svntreecheck.cpp) braucht nur
# QtWidgets, kein KF6 und keinen SvnManager. Läuft headless über die
# offscreen-Plattform, damit kein X/Wayland nötig ist.
set(PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../plugin")
add_executable(tst_treecheck
    tests/tst_treecheck.cpp
    "${PLUGIN_DIR}/svntreecheck.cpp")
target_include_directories(tst_treecheck PRIVATE "${PLUGIN_DIR}")
target_link_libraries(tst_treecheck PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME tst_treecheck COMMAND tst_treecheck)
set_tests_properties(tst_treecheck PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Commit-tree structure logic (plugin/svncommittree.cpp): nesting, grouping,
# missing directories, collapsing. KF6-free, QtWidgets only. It reuses
# initTreeCheckStates from svntreecheck.cpp and SvnFileStatus from svn/svntypes.h.
add_executable(tst_committree
    tests/tst_committree.cpp
    "${PLUGIN_DIR}/svncommittree.cpp"
    "${PLUGIN_DIR}/svntreecheck.cpp")
target_include_directories(tst_committree PRIVATE "${PLUGIN_DIR}" "${SVN_BACKEND_DIR}")
target_link_libraries(tst_committree PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME tst_committree COMMAND tst_committree)
set_tests_properties(tst_committree PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Settings accessors (plugin/svnsettings.h) are header-only over QSettings, no
# KF6. The test isolates the settings location via QStandardPaths test mode.
add_executable(tst_settings tests/tst_settings.cpp)
target_include_directories(tst_settings PRIVATE "${PLUGIN_DIR}")
target_link_libraries(tst_settings PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_settings COMMAND tst_settings)
set_tests_properties(tst_settings PROPERTIES TIMEOUT 60)
