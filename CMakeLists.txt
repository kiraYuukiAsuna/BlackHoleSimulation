cmake_minimum_required(VERSION 3.20)

project(ImguiFramework)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(ThirdParty/glfw)
add_subdirectory(ThirdParty/glm)
add_subdirectory(ThirdParty/Imgui)

set(PROJECT_SOURCES
    main.cpp
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
set(SHADER_SOURCES
    shader_vk/fullscreen.vert
    shader_vk/blackhole_main.frag
    shader_vk/bloom_brightness_pass.frag
    shader_vk/bloom_downsample.frag
    shader_vk/bloom_upsample.frag
    shader_vk/bloom_composite.frag
    shader_vk/tonemapping.frag
    shader_vk/passthrough.frag
)
set(SHADER_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${CMAKE_BINARY_DIR}/shader_vk/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shader_vk
        COMMAND ${GLSLANG_VALIDATOR} -V -I${CMAKE_CURRENT_SOURCE_DIR}/shader_vk ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} ${CMAKE_CURRENT_SOURCE_DIR}/shader_vk/blackhole_common.glsl
        VERBATIM)
    list(APPEND SHADER_OUTPUTS ${SPV})
endforeach()
add_custom_target(BlackholeShaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(${PROJECT_NAME} BlackholeShaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    ThirdParty
    ThirdParty/Imgui/Imgui
    ThirdParty/Imgui/ImguiNodeEditor
    ThirdParty/Imgui/ImGuizmo
    ThirdParty/Imgui/ImPlot
    ThirdParty/Imgui/ImPlot3D
    ThirdParty/Imgui/RenderBackend
    ThirdParty/glfw/include
    ThirdParty/glm
    ThirdParty/VulkanMemoryAllocator
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Imgui
    glfw
    glm
)
if (WIN32)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ThirdParty/Vulkan/Win/vk_video
        ThirdParty/Vulkan/Win
        ThirdParty/imrefl
    )
    target_link_directories(${PROJECT_NAME} PRIVATE
        ThirdParty/Vulkan/Win/Lib
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        vulkan-1
        windowscodecs
        ole32
    )
elseif (UNIX AND NOT APPLE)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ThirdParty/Vulkan/Linux/vk_video
        ThirdParty/Vulkan/Linux
        ThirdParty/imrefl
    )
    target_link_directories(${PROJECT_NAME} PRIVATE
        ThirdParty/Vulkan/Linux/Lib
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        libvulkan.so
    )
elseif (APPLE)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ThirdParty/Vulkan/Mac/vk_video
        ThirdParty/Vulkan/Mac
    )
    target_link_directories(${PROJECT_NAME} PRIVATE
        ThirdParty/Vulkan/Mac/Lib
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        libvulkan.dylib
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Font/SourceHanSansSC-Regular.otf
        ${CMAKE_BINARY_DIR}/Font/SourceHanSansSC-Regular.otf
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
)
