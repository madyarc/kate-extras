add_library(katenotebookplugin MODULE
    katenotebookplugin.cpp
    notebookwidget.cpp
    cellwidget.cpp
    outputview.cpp
    kernel.cpp
    notebook.cpp
    notebook.qrc
)

target_compile_definitions(katenotebookplugin PRIVATE TRANSLATION_DOMAIN="katenotebook")

target_link_libraries(katenotebookplugin PRIVATE
    Qt6::Widgets
    Qt6::Svg
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
    KF6::XmlGui
    KF6::WidgetsAddons
    KF6::ConfigCore
)

kate_extras_plugin(katenotebookplugin)

install(TARGETS katenotebookplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
