add_library(kateresmonplugin MODULE
    kateresmonplugin.cpp
    resourcesampler.cpp
    resourcemeter.cpp
)

target_compile_definitions(kateresmonplugin PRIVATE TRANSLATION_DOMAIN="kateresmon")

target_link_libraries(kateresmonplugin PRIVATE
    Qt6::Widgets
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
)

install(TARGETS kateresmonplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
