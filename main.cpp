// Dear ImGui: standalone example application for Glfw + Vulkan

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <stdio.h>          // printf, fprintf
#include <stdlib.h>         // abort
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wincodec.h>
#include <wrl/client.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

// Volk headers
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#define VOLK_IMPLEMENTATION
#include <volk.h>
#endif

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

//#define APP_USE_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
#endif

// Data
static VkAllocationCallbacks*   g_Allocator = nullptr;
static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t                 g_MinImageCount = 2;
static bool                     g_SwapChainRebuild = false;

static VkCommandPool            g_UploadCommandPool = VK_NULL_HANDLE;
static VkCommandBuffer          g_UploadCommandBuffer = VK_NULL_HANDLE;
static VkFence                  g_UploadFence = VK_NULL_HANDLE;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix; // Unused arguments
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // APP_USE_VULKAN_DEBUG_REPORT

static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

static uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &mem_properties);
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++)
        if ((type_filter & (1u << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    fprintf(stderr, "[vulkan] Failed to find a suitable memory type\n");
    abort();
}

static void CreateUploadContext()
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = g_QueueFamily;
    VkResult err = vkCreateCommandPool(g_Device, &pool_info, g_Allocator, &g_UploadCommandPool);
    check_vk_result(err);

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = g_UploadCommandPool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    err = vkAllocateCommandBuffers(g_Device, &alloc_info, &g_UploadCommandBuffer);
    check_vk_result(err);

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    err = vkCreateFence(g_Device, &fence_info, g_Allocator, &g_UploadFence);
    check_vk_result(err);
}

static void DestroyUploadContext()
{
    if (g_UploadFence)
        vkDestroyFence(g_Device, g_UploadFence, g_Allocator);
    if (g_UploadCommandPool)
        vkDestroyCommandPool(g_Device, g_UploadCommandPool, g_Allocator);
    g_UploadFence = VK_NULL_HANDLE;
    g_UploadCommandBuffer = VK_NULL_HANDLE;
    g_UploadCommandPool = VK_NULL_HANDLE;
}

template <typename RecordFn>
static void SubmitImmediate(RecordFn&& record)
{
    VkResult err = vkWaitForFences(g_Device, 1, &g_UploadFence, VK_TRUE, UINT64_MAX);
    check_vk_result(err);
    err = vkResetFences(g_Device, 1, &g_UploadFence);
    check_vk_result(err);
    err = vkResetCommandPool(g_Device, g_UploadCommandPool, 0);
    check_vk_result(err);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(g_UploadCommandBuffer, &begin_info);
    check_vk_result(err);
    record(g_UploadCommandBuffer);
    err = vkEndCommandBuffer(g_UploadCommandBuffer);
    check_vk_result(err);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &g_UploadCommandBuffer;
    err = vkQueueSubmit(g_Queue, 1, &submit_info, g_UploadFence);
    check_vk_result(err);
    err = vkWaitForFences(g_Device, 1, &g_UploadFence, VK_TRUE, UINT64_MAX);
    check_vk_result(err);
}

static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult err = vkCreateBuffer(g_Device, &buffer_info, g_Allocator, &buffer);
    check_vk_result(err);

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(g_Device, buffer, &mem_requirements);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, properties);
    err = vkAllocateMemory(g_Device, &alloc_info, g_Allocator, &memory);
    check_vk_result(err);
    err = vkBindBufferMemory(g_Device, buffer, memory, 0);
    check_vk_result(err);
}

struct LoadedImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

static std::vector<uint32_t> ReadBinaryFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open " + path);
    size_t size = (size_t)file.tellg();
    std::vector<uint32_t> data((size + 3) / 4);
    file.seekg(0);
    file.read((char*)data.data(), size);
    return data;
}

#ifdef _WIN32
static std::wstring ToWide(const std::string& text)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
    wide.pop_back();
    return wide;
}

static LoadedImage LoadImageRGBA(const std::string& path)
{
    using Microsoft::WRL::ComPtr;
    static bool co_initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    (void)co_initialized;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        throw std::runtime_error("Failed to create WIC factory");

    ComPtr<IWICBitmapDecoder> decoder;
    std::wstring wide_path = ToWide(path);
    hr = factory->CreateDecoderFromFilename(wide_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
        throw std::runtime_error("Failed to load image " + path);

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);
    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);

    ComPtr<IWICFormatConverter> converter;
    factory->CreateFormatConverter(&converter);
    converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    LoadedImage image;
    image.width = (int)width;
    image.height = (int)height;
    image.rgba.resize((size_t)width * (size_t)height * 4);
    hr = converter->CopyPixels(nullptr, width * 4, (UINT)image.rgba.size(), image.rgba.data());
    if (FAILED(hr))
        throw std::runtime_error("Failed to decode image " + path);
    return image;
}
#elif defined(__APPLE__)
static LoadedImage LoadImageRGBA(const std::string& path)
{
    CFStringRef cf_path = CFStringCreateWithCString(kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
    if (!cf_path)
        throw std::runtime_error("Failed to convert image path " + path);

    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cf_path, kCFURLPOSIXPathStyle, false);
    CFRelease(cf_path);
    if (!url)
        throw std::runtime_error("Failed to create image URL " + path);

    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source)
        throw std::runtime_error("Failed to load image " + path);

    CGImageRef source_image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!source_image)
        throw std::runtime_error("Failed to decode image " + path);

    const size_t width = CGImageGetWidth(source_image);
    const size_t height = CGImageGetHeight(source_image);
    if (width == 0 || height == 0 || width > (size_t)std::numeric_limits<int>::max() || height > (size_t)std::numeric_limits<int>::max()) {
        CGImageRelease(source_image);
        throw std::runtime_error("Unsupported image dimensions in " + path);
    }

    LoadedImage image;
    image.width = (int)width;
    image.height = (int)height;
    image.rgba.resize(width * height * 4);

    CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!color_space)
        color_space = CGColorSpaceCreateDeviceRGB();
    if (!color_space) {
        CGImageRelease(source_image);
        throw std::runtime_error("Failed to create color space for " + path);
    }

    const CGBitmapInfo bitmap_info = static_cast<CGBitmapInfo>(
        static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    CGContextRef context = CGBitmapContextCreate(image.rgba.data(), width, height, 8, width * 4, color_space, bitmap_info);
    CGColorSpaceRelease(color_space);
    if (!context) {
        CGImageRelease(source_image);
        throw std::runtime_error("Failed to create bitmap context for " + path);
    }

    CGContextDrawImage(context, CGRectMake(0.0, 0.0, (CGFloat)width, (CGFloat)height), source_image);
    CGContextRelease(context);
    CGImageRelease(source_image);
    return image;
}
#else
static LoadedImage LoadImageRGBA(const std::string&)
{
    throw std::runtime_error("PNG loading currently uses Windows WIC on this template");
}
#endif

struct GpuImage
{
    int width = 0;
    int height = 0;
    uint32_t layers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

struct BlackholeControls
{
    int resolution_index = 2;
    int render_scale_index = 0;
    int bloomIterations = 8;
    bool gravatationalLensing = true;
    bool renderBlackHole = true;
    bool mouseControl = true;
    bool frontView = false;
    bool topView = false;
    bool adiskEnabled = true;
    bool adiskParticle = true;
    bool tonemappingEnabled = true;
    float cameraRoll = 0.0f;
    float fovScale = 1.0f;
    float adiskDensityV = 2.0f;
    float adiskDensityH = 4.0f;
    float adiskHeight = 0.55f;
    float adiskLit = 0.25f;
    float adiskNoiseLOD = 5.0f;
    float adiskNoiseScale = 0.8f;
    float adiskSpeed = 0.5f;
    float bloomStrength = 0.1f;
    float gamma = 2.5f;
};

struct BlackholePushConstants
{
    float resolution[2] = {};
    float time = 0.0f;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float frontView = 0.0f;
    float topView = 0.0f;
    float cameraRoll = 0.0f;
    float gravatationalLensing = 1.0f;
    float renderBlackHole = 1.0f;
    float mouseControl = 1.0f;
    float fovScale = 1.0f;
    float adiskEnabled = 1.0f;
    float adiskParticle = 1.0f;
    float adiskHeight = 0.55f;
    float adiskLit = 0.25f;
    float adiskDensityV = 2.0f;
    float adiskDensityH = 4.0f;
    float adiskNoiseScale = 0.8f;
    float adiskNoiseLOD = 5.0f;
    float adiskSpeed = 0.5f;
    float bloomStrength = 0.1f;
    float tonemappingEnabled = 1.0f;
    float gamma = 2.5f;
};

struct BlackholeVulkanApp
{
    static constexpr int MaxBloom = 8;
    BlackholeControls controls;
    int width = 0;
    int height = 0;
    double start_time = 0.0;
    VkFormat hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkSampler linear_sampler = VK_NULL_HANDLE;
    VkPipeline p_blackhole = VK_NULL_HANDLE;
    VkPipeline p_brightness = VK_NULL_HANDLE;
    VkPipeline p_down = VK_NULL_HANDLE;
    VkPipeline p_up = VK_NULL_HANDLE;
    VkPipeline p_composite = VK_NULL_HANDLE;
    VkPipeline p_tone = VK_NULL_HANDLE;
    GpuImage color_map;
    GpuImage galaxy;
    GpuImage dummy2d;
    GpuImage blackhole;
    GpuImage brightness;
    std::array<GpuImage, MaxBloom> down;
    std::array<GpuImage, MaxBloom> up;
    GpuImage bloom_final;
    GpuImage tonemapped;
    VkDescriptorSet ds_blackhole = VK_NULL_HANDLE;
    VkDescriptorSet ds_brightness = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MaxBloom> ds_down = {};
    std::array<VkDescriptorSet, MaxBloom> ds_up = {};
    VkDescriptorSet ds_composite = VK_NULL_HANDLE;
    VkDescriptorSet ds_tone = VK_NULL_HANDLE;
    VkDescriptorSet imgui_texture = VK_NULL_HANDLE;
};

static BlackholeVulkanApp g_Blackhole;

static void ResolutionForIndex(int index, int& width, int& height)
{
    static const int sizes[][2] = {
        {960, 540},
        {1280, 720},
        {1600, 900},
        {1920, 1080},
        {2560, 1440},
        {3200, 1800},
        {3840, 2160},
    };
    index = std::clamp(index, 0, (int)IM_ARRAYSIZE(sizes) - 1);
    width = sizes[index][0];
    height = sizes[index][1];
}

static float RenderScaleForIndex(int index)
{
    static const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    return scales[std::clamp(index, 0, (int)IM_ARRAYSIZE(scales) - 1)];
}

static void RenderResolutionForControls(const BlackholeControls& controls, int& width, int& height)
{
    ResolutionForIndex(controls.resolution_index, width, height);
    const float scale = RenderScaleForIndex(controls.render_scale_index);
    width = std::max(1, (int)std::round((float)width * scale));
    height = std::max(1, (int)std::round((float)height * scale));
}

static void DestroyGpuImage(GpuImage& image)
{
    if (image.framebuffer)
        vkDestroyFramebuffer(g_Device, image.framebuffer, g_Allocator);
    if (image.view)
        vkDestroyImageView(g_Device, image.view, g_Allocator);
    if (image.image)
        vkDestroyImage(g_Device, image.image, g_Allocator);
    if (image.memory)
        vkFreeMemory(g_Device, image.memory, g_Allocator);
    image = {};
}

static void CreateGpuImage(GpuImage& out, int width, int height, VkFormat format, VkImageUsageFlags usage, uint32_t layers = 1, bool cube = false)
{
    DestroyGpuImage(out);
    out.width = width;
    out.height = height;
    out.layers = layers;
    out.format = format;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = {(uint32_t)width, (uint32_t)height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = layers;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult err = vkCreateImage(g_Device, &image_info, g_Allocator, &out.image);
    check_vk_result(err);

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(g_Device, out.image, &mem_requirements);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    err = vkAllocateMemory(g_Device, &alloc_info, g_Allocator, &out.memory);
    check_vk_result(err);
    err = vkBindImageMemory(g_Device, out.image, out.memory, 0);
    check_vk_result(err);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out.image;
    view_info.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = layers;
    err = vkCreateImageView(g_Device, &view_info, g_Allocator, &out.view);
    check_vk_result(err);
}

static void CreateFramebufferFor(GpuImage& image, VkRenderPass render_pass)
{
    if (image.framebuffer)
        vkDestroyFramebuffer(g_Device, image.framebuffer, g_Allocator);
    VkFramebufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = render_pass;
    info.attachmentCount = 1;
    info.pAttachments = &image.view;
    info.width = (uint32_t)image.width;
    info.height = (uint32_t)image.height;
    info.layers = 1;
    VkResult err = vkCreateFramebuffer(g_Device, &info, g_Allocator, &image.framebuffer);
    check_vk_result(err);
}

static void UploadPixelsToImage(GpuImage& image, const uint8_t* pixels, size_t byte_count, uint32_t layers = 1, uint32_t layer_size = 0)
{
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    CreateBuffer((VkDeviceSize)byte_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_memory);
    void* mapped = nullptr;
    VkResult err = vkMapMemory(g_Device, staging_memory, 0, (VkDeviceSize)byte_count, 0, &mapped);
    check_vk_result(err);
    memcpy(mapped, pixels, byte_count);
    vkUnmapMemory(g_Device, staging_memory);

    SubmitImmediate([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier to_transfer = {};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = image.image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.layerCount = layers;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);

        std::vector<VkBufferImageCopy> regions;
        regions.reserve(layers);
        for (uint32_t layer = 0; layer < layers; layer++) {
            VkBufferImageCopy region = {};
            region.bufferOffset = (VkDeviceSize)layer * (VkDeviceSize)layer_size;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {(uint32_t)image.width, (uint32_t)image.height, 1};
            regions.push_back(region);
        }
        vkCmdCopyBufferToImage(cmd, staging_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());

        VkImageMemoryBarrier to_shader = to_transfer;
        to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_shader);
    });

    vkDestroyBuffer(g_Device, staging_buffer, g_Allocator);
    vkFreeMemory(g_Device, staging_memory, g_Allocator);
}

static void LoadTexture2D(GpuImage& image, const std::string& path)
{
    LoadedImage loaded = LoadImageRGBA(path);
    CreateGpuImage(image, loaded.width, loaded.height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    UploadPixelsToImage(image, loaded.rgba.data(), loaded.rgba.size(), 1, (uint32_t)loaded.rgba.size());
}

static void LoadCubemap(GpuImage& image, const std::string& dir)
{
    const char* names[] = {"right", "left", "top", "bottom", "front", "back"};
    std::vector<LoadedImage> faces;
    for (const char* name : names)
        faces.push_back(LoadImageRGBA(dir + "/" + name + ".png"));
    int width = faces[0].width;
    int height = faces[0].height;
    std::vector<uint8_t> pixels((size_t)width * (size_t)height * 4 * 6);
    uint32_t layer_size = (uint32_t)((size_t)width * (size_t)height * 4);
    for (int i = 0; i < 6; i++)
        memcpy(pixels.data() + (size_t)i * layer_size, faces[i].rgba.data(), layer_size);
    CreateGpuImage(image, width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 6, true);
    UploadPixelsToImage(image, pixels.data(), pixels.size(), 6, layer_size);
}

static VkShaderModule CreateShaderModule(const std::string& path)
{
    std::vector<uint32_t> code = ReadBinaryFile(path);
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult err = vkCreateShaderModule(g_Device, &info, g_Allocator, &module);
    check_vk_result(err);
    return module;
}

static VkPipeline CreateFullscreenPipeline(VkRenderPass render_pass, VkPipelineLayout layout, const std::string& frag_spv)
{
    VkShaderModule vert = CreateShaderModule("shader_vk/fullscreen.vert.spv");
    VkShaderModule frag = CreateShaderModule(frag_spv);
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment = {};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    info.renderPass = render_pass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult err = vkCreateGraphicsPipelines(g_Device, g_PipelineCache, 1, &info, g_Allocator, &pipeline);
    check_vk_result(err);
    vkDestroyShaderModule(g_Device, vert, g_Allocator);
    vkDestroyShaderModule(g_Device, frag, g_Allocator);
    return pipeline;
}

static void WriteDescriptor(VkDescriptorSet set, const GpuImage& tex0, const GpuImage& tex1, const GpuImage& color_map, const GpuImage& galaxy, VkSampler sampler)
{
    VkDescriptorImageInfo infos[4] = {
        {sampler, tex0.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler, tex1.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler, color_map.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler, galaxy.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet writes[4] = {};
    for (uint32_t i = 0; i < 4; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(g_Device, 4, writes, 0, nullptr);
}

static VkDescriptorSet AllocateDescriptorSet(BlackholeVulkanApp& app)
{
    VkDescriptorSetAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool = app.descriptor_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &app.descriptor_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult err = vkAllocateDescriptorSets(g_Device, &info, &set);
    check_vk_result(err);
    return set;
}

static void DestroyBlackholeRenderTargets(BlackholeVulkanApp& app)
{
    if (app.imgui_texture) {
        ImGui_ImplVulkan_RemoveTexture(app.imgui_texture);
        app.imgui_texture = VK_NULL_HANDLE;
    }
    DestroyGpuImage(app.blackhole);
    DestroyGpuImage(app.brightness);
    for (GpuImage& image : app.down)
        DestroyGpuImage(image);
    for (GpuImage& image : app.up)
        DestroyGpuImage(image);
    DestroyGpuImage(app.bloom_final);
    DestroyGpuImage(app.tonemapped);
}

static void RecreateBlackholeRenderTargets(BlackholeVulkanApp& app)
{
    vkDeviceWaitIdle(g_Device);
    DestroyBlackholeRenderTargets(app);
    RenderResolutionForControls(app.controls, app.width, app.height);

    auto create_target = [&](GpuImage& image, int w, int h) {
        CreateGpuImage(image, std::max(1, w), std::max(1, h), app.hdr_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        CreateFramebufferFor(image, app.render_pass);
    };

    create_target(app.blackhole, app.width, app.height);
    create_target(app.brightness, app.width, app.height);
    for (int i = 0; i < BlackholeVulkanApp::MaxBloom; i++) {
        create_target(app.down[i], app.width >> (i + 1), app.height >> (i + 1));
        create_target(app.up[i], app.width >> i, app.height >> i);
    }
    create_target(app.bloom_final, app.width, app.height);
    create_target(app.tonemapped, app.width, app.height);

    WriteDescriptor(app.ds_blackhole, app.dummy2d, app.dummy2d, app.color_map, app.galaxy, app.linear_sampler);
    WriteDescriptor(app.ds_brightness, app.blackhole, app.dummy2d, app.color_map, app.galaxy, app.linear_sampler);
    for (int i = 0; i < BlackholeVulkanApp::MaxBloom; i++) {
        WriteDescriptor(app.ds_down[i], i == 0 ? app.brightness : app.down[i - 1], app.dummy2d, app.color_map, app.galaxy, app.linear_sampler);
        WriteDescriptor(app.ds_up[i], i == BlackholeVulkanApp::MaxBloom - 1 ? app.down[i] : app.up[i + 1], i == 0 ? app.brightness : app.down[i - 1], app.color_map, app.galaxy, app.linear_sampler);
    }
    WriteDescriptor(app.ds_composite, app.blackhole, app.up[0], app.color_map, app.galaxy, app.linear_sampler);
    WriteDescriptor(app.ds_tone, app.bloom_final, app.dummy2d, app.color_map, app.galaxy, app.linear_sampler);
    app.imgui_texture = ImGui_ImplVulkan_AddTexture(app.linear_sampler, app.tonemapped.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

static void InitBlackholeVulkanApp()
{
    BlackholeVulkanApp& app = g_Blackhole;
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 1.0f;
    VkResult err = vkCreateSampler(g_Device, &sampler_info, g_Allocator, &app.linear_sampler);
    check_vk_result(err);

    VkAttachmentDescription attachment = {};
    attachment.format = app.hdr_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 2;
    rp_info.pDependencies = deps;
    err = vkCreateRenderPass(g_Device, &rp_info, g_Allocator, &app.render_pass);
    check_vk_result(err);

    VkDescriptorSetLayoutBinding bindings[4] = {};
    for (uint32_t i = 0; i < 4; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsl_info = {};
    dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_info.bindingCount = 4;
    dsl_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(g_Device, &dsl_info, g_Allocator, &app.descriptor_layout);
    check_vk_result(err);

    VkPushConstantRange range = {};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.size = sizeof(BlackholePushConstants);
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &app.descriptor_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    err = vkCreatePipelineLayout(g_Device, &layout_info, g_Allocator, &app.pipeline_layout);
    check_vk_result(err);

    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 64;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &app.descriptor_pool);
    check_vk_result(err);

    app.p_blackhole = CreateFullscreenPipeline(app.render_pass, app.pipeline_layout, "shader_vk/blackhole_main.frag.spv");
    app.p_brightness = CreateFullscreenPipeline(app.render_pass, app.pipeline_layout, "shader_vk/bloom_brightness_pass.frag.spv");
    app.p_down = CreateFullscreenPipeline(app.render_pass, app.pipeline_layout, "shader_vk/bloom_downsample.frag.spv");
    app.p_up = CreateFullscreenPipeline(app.render_pass, app.pipeline_layout, "shader_vk/bloom_upsample.frag.spv");
    app.p_composite = CreateFullscreenPipeline(app.render_pass, app.pipeline_layout, "shader_vk/bloom_composite.frag.spv");
    app.p_tone = CreateFullscreenPipeline(app.render_pass, app.pipeline_layout, "shader_vk/tonemapping.frag.spv");

    LoadTexture2D(app.color_map, "assets/blackhole/color_map.png");
    LoadCubemap(app.galaxy, "assets/blackhole/skybox_nebula_dark");
    uint8_t white[4] = {255, 255, 255, 255};
    CreateGpuImage(app.dummy2d, 1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    UploadPixelsToImage(app.dummy2d, white, sizeof(white), 1, sizeof(white));

    app.ds_blackhole = AllocateDescriptorSet(app);
    app.ds_brightness = AllocateDescriptorSet(app);
    for (int i = 0; i < BlackholeVulkanApp::MaxBloom; i++) {
        app.ds_down[i] = AllocateDescriptorSet(app);
        app.ds_up[i] = AllocateDescriptorSet(app);
    }
    app.ds_composite = AllocateDescriptorSet(app);
    app.ds_tone = AllocateDescriptorSet(app);
    RecreateBlackholeRenderTargets(app);
}

static void ShutdownBlackholeVulkanApp()
{
    BlackholeVulkanApp& app = g_Blackhole;
    vkDeviceWaitIdle(g_Device);
    DestroyBlackholeRenderTargets(app);
    DestroyGpuImage(app.color_map);
    DestroyGpuImage(app.galaxy);
    DestroyGpuImage(app.dummy2d);
    if (app.p_blackhole) vkDestroyPipeline(g_Device, app.p_blackhole, g_Allocator);
    if (app.p_brightness) vkDestroyPipeline(g_Device, app.p_brightness, g_Allocator);
    if (app.p_down) vkDestroyPipeline(g_Device, app.p_down, g_Allocator);
    if (app.p_up) vkDestroyPipeline(g_Device, app.p_up, g_Allocator);
    if (app.p_composite) vkDestroyPipeline(g_Device, app.p_composite, g_Allocator);
    if (app.p_tone) vkDestroyPipeline(g_Device, app.p_tone, g_Allocator);
    if (app.descriptor_pool) vkDestroyDescriptorPool(g_Device, app.descriptor_pool, g_Allocator);
    if (app.pipeline_layout) vkDestroyPipelineLayout(g_Device, app.pipeline_layout, g_Allocator);
    if (app.descriptor_layout) vkDestroyDescriptorSetLayout(g_Device, app.descriptor_layout, g_Allocator);
    if (app.render_pass) vkDestroyRenderPass(g_Device, app.render_pass, g_Allocator);
    if (app.linear_sampler) vkDestroySampler(g_Device, app.linear_sampler, g_Allocator);
    app = {};
}

static BlackholePushConstants BuildPushConstants(const BlackholeVulkanApp& app, int pass_width, int pass_height)
{
    BlackholePushConstants pc;
    pc.resolution[0] = (float)pass_width;
    pc.resolution[1] = (float)pass_height;
    pc.time = (float)(ImGui::GetTime() - app.start_time);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    pc.mouseX = mouse.x / std::max(1.0f, ImGui::GetMainViewport()->WorkSize.x) * (float)app.width;
    pc.mouseY = mouse.y / std::max(1.0f, ImGui::GetMainViewport()->WorkSize.y) * (float)app.height;
    pc.frontView = app.controls.frontView ? 1.0f : 0.0f;
    pc.topView = app.controls.topView ? 1.0f : 0.0f;
    pc.cameraRoll = app.controls.cameraRoll;
    pc.gravatationalLensing = app.controls.gravatationalLensing ? 1.0f : 0.0f;
    pc.renderBlackHole = app.controls.renderBlackHole ? 1.0f : 0.0f;
    pc.mouseControl = app.controls.mouseControl ? 1.0f : 0.0f;
    pc.fovScale = app.controls.fovScale;
    pc.adiskEnabled = app.controls.adiskEnabled ? 1.0f : 0.0f;
    pc.adiskParticle = app.controls.adiskParticle ? 1.0f : 0.0f;
    pc.adiskHeight = app.controls.adiskHeight;
    pc.adiskLit = app.controls.adiskLit;
    pc.adiskDensityV = app.controls.adiskDensityV;
    pc.adiskDensityH = app.controls.adiskDensityH;
    pc.adiskNoiseScale = app.controls.adiskNoiseScale;
    pc.adiskNoiseLOD = app.controls.adiskNoiseLOD;
    pc.adiskSpeed = app.controls.adiskSpeed;
    pc.bloomStrength = app.controls.bloomStrength;
    pc.tonemappingEnabled = app.controls.tonemappingEnabled ? 1.0f : 0.0f;
    pc.gamma = app.controls.gamma;
    return pc;
}

static void RecordPass(VkCommandBuffer cmd, BlackholeVulkanApp& app, GpuImage& target, VkPipeline pipeline, VkDescriptorSet set)
{
    VkClearValue clear = {};
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = app.render_pass;
    rp.framebuffer = target.framebuffer;
    rp.renderArea.extent = {(uint32_t)target.width, (uint32_t)target.height};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0.0f, 0.0f, (float)target.width, (float)target.height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)target.width, (uint32_t)target.height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline_layout, 0, 1, &set, 0, nullptr);
    BlackholePushConstants pc = BuildPushConstants(app, target.width, target.height);
    vkCmdPushConstants(cmd, app.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

static void RenderBlackholeGpuFrame()
{
    BlackholeVulkanApp& app = g_Blackhole;
    int bloom_count = std::clamp(app.controls.bloomIterations, 1, BlackholeVulkanApp::MaxBloom);
    for (int i = 0; i < bloom_count; i++) {
        const bool top_level = i == bloom_count - 1;
        WriteDescriptor(app.ds_up[i], top_level ? app.down[i] : app.up[i + 1], i == 0 ? app.brightness : app.down[i - 1], app.color_map, app.galaxy, app.linear_sampler);
    }
    SubmitImmediate([&](VkCommandBuffer cmd) {
        RecordPass(cmd, app, app.blackhole, app.p_blackhole, app.ds_blackhole);
        RecordPass(cmd, app, app.brightness, app.p_brightness, app.ds_brightness);
        for (int i = 0; i < bloom_count; i++)
            RecordPass(cmd, app, app.down[i], app.p_down, app.ds_down[i]);
        for (int i = bloom_count - 1; i >= 0; i--)
            RecordPass(cmd, app, app.up[i], app.p_up, app.ds_up[i]);
        RecordPass(cmd, app, app.bloom_final, app.p_composite, app.ds_composite);
        RecordPass(cmd, app, app.tonemapped, app.p_tone, app.ds_tone);
    });
}

static void DrawBlackHoleApp()
{
    BlackholeVulkanApp& app = g_Blackhole;
    if (app.start_time == 0.0)
        app.start_time = ImGui::GetTime();

    int desired_w = 0, desired_h = 0;
    RenderResolutionForControls(app.controls, desired_w, desired_h);
    if (desired_w != app.width || desired_h != app.height)
        RecreateBlackholeRenderTargets(app);

    RenderBlackholeGpuFrame();

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
    ImGui::Begin("Black Hole Simulation", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float target_aspect = (float)app.width / (float)app.height;
    ImVec2 image_size = avail;
    if (image_size.x / image_size.y > target_aspect)
        image_size.x = image_size.y * target_aspect;
    else
        image_size.y = image_size.x / target_aspect;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - image_size.x) * 0.5f);
    ImGui::Image(ImTextureRef((ImTextureID)app.imgui_texture), image_size);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(385.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Blackhole Controls");
    const char* resolutions[] = {"960 x 540", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3200 x 1800", "3840 x 2160"};
    const char* render_scales[] = {"1.00x", "1.25x", "1.50x", "2.00x"};
    ImGui::Combo("resolution", &app.controls.resolution_index, resolutions, IM_ARRAYSIZE(resolutions));
    ImGui::Combo("renderScale", &app.controls.render_scale_index, render_scales, IM_ARRAYSIZE(render_scales));
    ImGui::Checkbox("gravatationalLensing", &app.controls.gravatationalLensing);
    ImGui::Checkbox("renderBlackHole", &app.controls.renderBlackHole);
    ImGui::Checkbox("mouseControl", &app.controls.mouseControl);
    ImGui::SliderFloat("cameraRoll", &app.controls.cameraRoll, -180.0f, 180.0f);
    ImGui::SliderFloat("fovScale", &app.controls.fovScale, 0.35f, 2.0f);
    ImGui::Checkbox("frontView", &app.controls.frontView);
    ImGui::Checkbox("topView", &app.controls.topView);
    ImGui::Checkbox("adiskEnabled", &app.controls.adiskEnabled);
    ImGui::Checkbox("adiskParticle", &app.controls.adiskParticle);
    ImGui::SliderFloat("adiskDensityV", &app.controls.adiskDensityV, 0.0f, 10.0f);
    ImGui::SliderFloat("adiskDensityH", &app.controls.adiskDensityH, 0.0f, 10.0f);
    ImGui::SliderFloat("adiskHeight", &app.controls.adiskHeight, 0.01f, 1.0f);
    ImGui::SliderFloat("adiskLit", &app.controls.adiskLit, 0.0f, 4.0f);
    ImGui::SliderFloat("adiskNoiseLOD", &app.controls.adiskNoiseLOD, 1.0f, 12.0f);
    ImGui::SliderFloat("adiskNoiseScale", &app.controls.adiskNoiseScale, 0.0f, 10.0f);
    ImGui::SliderFloat("adiskSpeed", &app.controls.adiskSpeed, 0.0f, 1.0f);
    ImGui::SliderInt("bloomIterations", &app.controls.bloomIterations, 1, BlackholeVulkanApp::MaxBloom);
    ImGui::SliderFloat("bloomStrength", &app.controls.bloomStrength, 0.0f, 1.0f);
    ImGui::Checkbox("tonemappingEnabled", &app.controls.tonemappingEnabled);
    ImGui::SliderFloat("gamma", &app.controls.gamma, 1.0f, 4.0f);
    ImGui::Separator();
    ImGui::Text("GPU render target: %d x %d", app.width, app.height);
    ImGui::Text("Supersampling: %.2fx", RenderScaleForIndex(app.controls.render_scale_index));
    ImGui::Text("Display FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
}

static void SetupVulkan(ImVector<const char*> instance_extensions)
{
    VkResult err;
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkInitialize();
#endif

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        // Enumerate available extensions
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        check_vk_result(err);

        // Enable required extensions
        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        instance_extensions.push_back("VK_EXT_debug_report");
#endif

        // Create Vulkan Instance
        create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
        create_info.ppEnabledExtensionNames = instance_extensions.Data;
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
        volkLoadInstance(g_Instance);
#endif

        // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
#endif
    }

    // Select Physical Device (GPU)
    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

    // Select graphics queue family
    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    IM_ASSERT(g_QueueFamily != (uint32_t)-1);

    // Create Logical Device (with 1 queue)
    {
        ImVector<const char*> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = { 1.0f };
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = g_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;
        err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
        check_vk_result(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    // Create Descriptor Pool
    // If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 },
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 256;
        pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        check_vk_result(err);
    }
}

// All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
// Your real engine/app may not use them.
static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
{
    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &res);
    if (res != VK_TRUE)
    {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->Surface = surface;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_COUNTOF(present_modes));
    //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(g_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount, 0);
}

static void CleanupVulkan()
{
    DestroyUploadContext();
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    // Remove the debug report callback
    auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

static void CleanupVulkanWindow(ImGui_ImplVulkanH_Window* wd)
{
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, wd, g_Allocator);
    vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
}

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
    VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd)
{
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

// Main code
int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Create window with Vulkan context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
    GLFWwindow* window = glfwCreateWindow((int)(1400 * main_scale), (int)(860 * main_scale), "Interstellar Black Hole Simulation", nullptr, nullptr);
    if (!glfwVulkanSupported())
    {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }

    ImVector<const char*> extensions;
    uint32_t extensions_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
    for (uint32_t i = 0; i < extensions_count; i++)
        extensions.push_back(glfw_extensions[i]);
    SetupVulkan(extensions);
    CreateUploadContext();

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err = glfwCreateWindowSurface(g_Instance, window, g_Allocator, &surface);
    check_vk_result(err);

    // Create Framebuffers
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    SetupVulkanWindow(wd, surface, w, h);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    //init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.Allocator = g_Allocator;
    init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);
    InitBlackholeVulkanApp();

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    ImVec4 clear_color = ImVec4(0.002f, 0.003f, 0.006f, 1.00f);

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Resize swap chain?
        int fb_width, fb_height;
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
        {
            ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
            g_MainWindowData.FrameIndex = 0;
            g_SwapChainRebuild = false;
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawBlackHoleApp();

        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized)
        {
            wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            wd->ClearValue.color.float32[3] = clear_color.w;
            FrameRender(wd, draw_data);
            FramePresent(wd);
        }
    }

    // Cleanup
    err = vkDeviceWaitIdle(g_Device);
    check_vk_result(err);
    ShutdownBlackholeVulkanApp();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow(&g_MainWindowData);
    CleanupVulkan();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
