kcoreaddons_add_plugin(dolphinsvnplugin
    SOURCES
        svnplugin.cpp
        svnadddialog.cpp
        svncommitdialog.cpp
        svncreatepatchdialog.cpp
        svnapplypatchdialog.cpp
        svndiffwindow.cpp
        svnrevisiondiffdialog.cpp
        svnlogdialog.cpp
        svnrevertdialog.cpp
        svnsettingsdialog.cpp
        svnstatusdialog.cpp
        svnprogressdialog.cpp
        svnupdatedialog.cpp
        svncheckoutdialog.cpp
        svnrenamedialog.cpp
        svnlockdialog.cpp
        svnexportdialog.cpp
        svnpropertiesdialog.cpp
        svnblamedialog.cpp
        svnbranchtagdialog.cpp
        svnmergedialog.cpp
        svnlogpickdialog.cpp
        svnconflictdialog.cpp
        svnrepobrowserdialog.cpp
        svnswitchdialog.cpp
        svnrelocatedialog.cpp
        svnimportdialog.cpp
        svntreecheck.cpp
        svncommittree.cpp
        svnuihelpers.cpp
    INSTALL_NAMESPACE "kf6/kfileitemaction"
)

target_link_libraries(dolphinsvnplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::CoreAddons
    KF6::KIOWidgets
    KF6::I18n
    KF6::Notifications
    svnmanager
)

# Pass the project version (defined once in the top-level CMakeLists.txt) into
# the code so the Settings dialog can display it.
target_compile_definitions(dolphinsvnplugin
    PRIVATE DOLPHIN_SVN_PLUGIN_VERSION="${PROJECT_VERSION}")

install(FILES dolphinsvnplugin.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
