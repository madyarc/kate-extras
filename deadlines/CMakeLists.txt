add_library(katedeadlinesplugin MODULE
    katedeadlinesplugin.cpp
    deadlinestore.cpp
    deadlinedialog.cpp
    deadlinewidget.cpp
)

target_compile_definitions(katedeadlinesplugin PRIVATE TRANSLATION_DOMAIN="katedeadlines")

target_link_libraries(katedeadlinesplugin PRIVATE
    Qt6::Widgets
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
    KF6::XmlGui
)

install(TARGETS katedeadlinesplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
