kcoreaddons_add_plugin(svnoverlayplugin
    SOURCES svnoverlayplugin.cpp
    INSTALL_NAMESPACE "kf6/overlayicon"
)

# svnsettings.h ist header-only (keine neue Link-Abhängigkeit auf das Plugin-Target).
target_include_directories(svnoverlayplugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin
)

target_link_libraries(svnoverlayplugin
    Qt6::Core
    Qt6::Concurrent
    KF6::CoreAddons
    KF6::KIOCore
    svnmanager
)
