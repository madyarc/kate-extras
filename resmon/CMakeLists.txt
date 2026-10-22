add_library(kateresmonplugin MODULE
    kateresmonplugin.cpp
    resourcesampler.cpp
    resourcemeter.cpp
    resourcestatusitem.h # for AUTOMOC: the macOS source has a different name
)

if(APPLE)
    target_sources(kateresmonplugin PRIVATE resourcesampler_macos.mm resourcestatusitem_macos.mm)
    target_link_libraries(kateresmonplugin PRIVATE "-framework AppKit" "-framework IOKit")
else()
    target_sources(kateresmonplugin PRIVATE resourcesampler_linux.cpp resourcestatusitem.cpp)
endif()

target_compile_definitions(kateresmonplugin PRIVATE TRANSLATION_DOMAIN="kateresmon")

target_link_libraries(kateresmonplugin PRIVATE
    Qt6::Widgets
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
)

kate_extras_plugin(kateresmonplugin)

install(TARGETS kateresmonplugin DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
