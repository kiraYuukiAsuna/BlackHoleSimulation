file(GLOB Imgui_SOURCE Imgui/*.h Imgui/*.cpp)
file(GLOB ImguiNodeEditor_SOURCE ImguiNodeEditor/*.h ImguiNodeEditor/*.cpp ImguiNodeEditor/*.inl)
file(GLOB ImGuizmo_SOURCE ImGuizmo/*.h ImGuizmo/*.cpp)
file(GLOB ImPlot_SOURCE ImPlot/*.h ImPlot/*.cpp)
file(GLOB ImPlot3D_SOURCE ImPlot3D/*.h ImPlot3D/*.cpp)
file(GLOB RenderBackend_SOURCE RenderBackend/*.h RenderBackend/*.cpp)

add_library(Imgui STATIC ${Imgui_SOURCE} ${ImguiNodeEditor_SOURCE} ${ImGuizmo_SOURCE} ${ImPlot_SOURCE} ${ImPlot3D_SOURCE} ${RenderBackend_SOURCE})

target_include_directories(Imgui PRIVATE
    Imgui
    ImguiNodeEditor
    ImGuizmo
    ImPlot
    ImPlot3D
    RenderBackend
    ../glfw/include
)
if (WIN32)
    target_include_directories(Imgui PRIVATE
        ../Vulkan/Win
    )
elseif (UNIX AND NOT APPLE)
    target_include_directories(Imgui PRIVATE
        ../Vulkan/Linux
    )
elseif (APPLE)
    target_include_directories(Imgui PRIVATE
        ../Vulkan/Mac
    )
endif()
