add_library(katespotifyplugin MODULE
    katespotifyplugin.cpp
    spotifywidget.cpp
)

target_compile_definitions(katespotifyplugin PRIVATE TRANSLATION_DOMAIN="katespotify")

target_link_libraries(katespotifyplugin PRIVATE
    Qt6::Widgets
    Qt6::DBus
    Qt6::Network
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
    KF6::XmlGui
)

install(TARGETS katespotifyplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
