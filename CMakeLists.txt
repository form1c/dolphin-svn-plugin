cmake_minimum_required(VERSION 3.16)
# The single source of truth for the plugin version. It flows to the compiled
# code (shown in the Settings dialog), the .deb file name and the developer
# documentation. To release a new version, change it here and nowhere else.
project(dolphin-svn-plugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# Qt6 must be found before KDEInstallDirs6 so ECMQueryQt can locate qtpaths
find_package(Qt6 REQUIRED COMPONENTS Core CoreTools Xml)

include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(KF6 REQUIRED COMPONENTS
    CoreAddons
    KIO
    I18n
    Notifications
)

add_subdirectory(svn)
add_subdirectory(plugin)
add_subdirectory(overlays)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
