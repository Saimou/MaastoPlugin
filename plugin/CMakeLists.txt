option( PLUGIN_STANDARD_QMAASTOPLUGIN "Install MaastoPlugin" OFF )

if ( PLUGIN_STANDARD_QMAASTOPLUGIN )
    project( QMAASTO_PLUGIN )

    AddPlugin( NAME ${PROJECT_NAME} )

    add_subdirectory( include )
    add_subdirectory( src )
endif()
