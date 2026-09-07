add_library(svnmanager STATIC
    svnmanager.cpp
    svnrevrange.cpp
)

# Wird in Shared Modules (Dolphin-Plugins) verlinkt -> muss PIC sein.
set_target_properties(svnmanager PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(svnmanager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(svnmanager PUBLIC
    Qt6::Core
    Qt6::Xml
)
