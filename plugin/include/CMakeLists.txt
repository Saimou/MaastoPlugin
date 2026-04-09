target_sources( ${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/MaastoAction.h
        ${CMAKE_CURRENT_LIST_DIR}/qMaastoPlugin.h
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
