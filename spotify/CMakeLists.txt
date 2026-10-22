add_library(katespotifyplugin MODULE
    katespotifyplugin.cpp
    spotifyplayer.cpp
    spotifywidget.cpp
)

if(APPLE)
    target_sources(katespotifyplugin PRIVATE spotifyplayer_macos.mm)
    target_link_libraries(katespotifyplugin PRIVATE "-framework AppKit")
else()
    target_sources(katespotifyplugin PRIVATE spotifyplayer_mpris.cpp)
    target_link_libraries(katespotifyplugin PRIVATE Qt6::DBus)
endif()

target_compile_definitions(katespotifyplugin PRIVATE TRANSLATION_DOMAIN="katespotify")

target_link_libraries(katespotifyplugin PRIVATE
    Qt6::Widgets
    Qt6::Network
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
    KF6::XmlGui
)

kate_extras_plugin(katespotifyplugin)

install(TARGETS katespotifyplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
