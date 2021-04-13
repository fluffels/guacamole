#include <Windows.h>

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

LARGE_INTEGER counterEpoch;
LARGE_INTEGER counterFrequency;
FILE* logFile;

float GetElapsed() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    auto result =
        (t.QuadPart - counterEpoch.QuadPart)
        / (float)counterFrequency.QuadPart;
    return result;
}

#define TIME()\
    fprintf(logFile, "[%f]", GetElapsed());

#define LOC()\
    fprintf(logFile, "[%s:%d]", __FILE__, __LINE__);

#define LOG(level, ...)\
    LOC()\
    TIME()\
    fprintf(logFile, "[%s] ", level);\
    fprintf(logFile, __VA_ARGS__); \
    fprintf(logFile, "\n"); \
    fflush(logFile);

#define FATAL(...)\
    LOG("FATAL", __VA_ARGS__);\
    exit(1);

#define WARN(...) LOG("WARN", __VA_ARGS__);

#define ERR(...) LOG("ERROR", __VA_ARGS__);

#define INFO(...)\
    LOG("INFO", __VA_ARGS__)

#define CHECK(x, ...)\
    if (!x) { FATAL(__VA_ARGS__) }

#define LERROR(x) \
    if (x) { \
        char buffer[1024]; \
        strerror_s(buffer, errno); \
        FATAL(buffer); \
    }

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

#include "jcwk/MathLib.cpp"

#pragma pack(push, 1)
struct Uniforms {
    float proj[16];
    Vec4 eye;
    Quaternion rotation;
};
#pragma pack(pop)

#define READ(buffer, type, offset) (type*)(buffer + offset)

#define STB_DS_IMPLEMENTATION
#include "stb/stb_ds.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG 
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb/stb_image.h"

#ifdef WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include "jcwk/FileSystem.cpp"
#include "jcwk/Win32/DirectInput.cpp"
#include "jcwk/Win32/Controller.cpp"
#include "jcwk/Win32/Mouse.cpp"
#define VULKAN_COMPUTE
#include "jcwk/Vulkan.cpp"
#include <vulkan/vulkan_win32.h>
#endif

const float DELTA_MOVE_PER_S = 100.f;
const float MOUSE_SENSITIVITY = 0.1f;
const float JOYSTICK_SENSITIVITY = 5;
bool keyboard[VK_OEM_CLEAR] = {};

LRESULT
WindowProc(
    HWND    window,
    UINT    message,
    WPARAM  wParam,
    LPARAM  lParam
) {
    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            else keyboard[(uint16_t)wParam] = true;
            break;
        case WM_KEYUP:
            keyboard[(uint16_t)wParam] = false;
            break;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

int __stdcall
WinMain(
    HINSTANCE instance,
    HINSTANCE prevInstance,
    LPSTR commandLine,
    int showCommand
) {
    // NOTE: Initialize logging.
    {
        auto error = fopen_s(&logFile, "LOG", "w");
        if (error) return 1;

        QueryPerformanceCounter(&counterEpoch);
        QueryPerformanceFrequency(&counterFrequency);
    }

    // NOTE: Create window.
    int screenWidth;
    int screenHeight;
    HWND window = NULL;
    {
        WNDCLASSEX windowClassProperties = {};
        windowClassProperties.cbSize = sizeof(windowClassProperties);
        windowClassProperties.style = CS_HREDRAW | CS_VREDRAW;
        windowClassProperties.lpfnWndProc = (WNDPROC)WindowProc;
        windowClassProperties.hInstance = instance;
        windowClassProperties.lpszClassName = "MainWindowClass";
        ATOM windowClass = RegisterClassEx(&windowClassProperties);
        CHECK(windowClass, "Could not create window class");

        window = CreateWindowEx(
            0,
            "MainWindowClass",
            "guacamole",
            WS_POPUP | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            800,
            NULL,
            NULL,
            instance,
            NULL
        );
        CHECK(window, "Could not create window");

        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(
            window,
            HWND_TOP,
            0,
            0,
            screenWidth,
            screenHeight,
            SWP_FRAMECHANGED
        );
        ShowCursor(FALSE);

        INFO("Window created");
    }

    // Create Vulkan instance.
    Vulkan vk;
    vk.extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    createVKInstance(vk);
    INFO("Vulkan instance created");

    // Create Windows surface.
    {
        VkWin32SurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hinstance = instance;
        createInfo.hwnd = window;

        auto result = vkCreateWin32SurfaceKHR(
            vk.handle,
            &createInfo,
            nullptr,
            &vk.swap.surface
        );
        VKCHECK(result, "could not create win32 surface");
        INFO("Surface created");
    }

    // Initialize Vulkan.
    initVK(vk);
    INFO("Vulkan initialized");

    // Init & execute compute shader.
    VulkanBuffer computedBuffer;
    const u32 computeWidth = 32;
    const u32 computeHeight = computeWidth;
    const u32 computeDepth = computeWidth;
    const u32 computeCount = computeWidth * computeHeight * computeDepth;
    const u32 computeVerticesPerExecution = 15;
    const u32 computedVertexCount = computeVerticesPerExecution * computeCount;
    const u32 computedVertexWidth = 4;
    const int computeSize = computedVertexCount * computedVertexWidth * sizeof(float);
    {
        VulkanPipeline pipeline;
        initVKPipelineCompute(
            vk,
            "cs",
            pipeline
        );
        createComputeToVertexBuffer(
            vk.device,
            vk.memories,
            vk.computeQueueFamily,
            computeSize,
            computedBuffer
        );
        updateStorageBuffer(
            vk.device,
            pipeline.descriptorSet,
            0,
            computedBuffer.handle
        );
        dispatchCompute(
            vk,
            pipeline,
            computeWidth, computeHeight, computeDepth
        );
        // Have to wait here before we transfer ownership of the buffer.
        vkQueueWaitIdle(vk.computeQueue);
        transferBufferOwnership(
            vk.device,
            vk.cmdPoolComputeTransient,
            vk.cmdPoolTransient,
            vk.computeQueue,
            vk.queue,
            computedBuffer.handle,
            vk.computeQueueFamily,
            vk.queueFamily,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT
        );
    }

    // Record command buffers.
    VkCommandBuffer* cmds = NULL;
    {
        VulkanPipeline defaultPipeline;
        initVKPipelineNoCull(
            vk,
            "default",
            defaultPipeline
        );
        updateUniformBuffer(
            vk.device,
            defaultPipeline.descriptorSet,
            0,
            vk.uniforms.handle
        );

        u32 framebufferCount = vk.swap.images.size();
        arrsetlen(cmds, framebufferCount);
        createCommandBuffers(vk.device, vk.cmdPool, framebufferCount, cmds);
        for (size_t swapIdx = 0; swapIdx < framebufferCount; swapIdx++) {
            auto& cmd = cmds[swapIdx];
            beginFrameCommandBuffer(cmd);

            VkClearValue colorClear;
            colorClear.color = {};
            VkClearValue depthClear;
            depthClear.depthStencil = { 1.f, 0 };
            VkClearValue clears[] = { colorClear, depthClear };

            VkRenderPassBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            beginInfo.clearValueCount = 2;
            beginInfo.pClearValues = clears;
            beginInfo.framebuffer = vk.swap.framebuffers[swapIdx];
            beginInfo.renderArea.extent = vk.swap.extent;
            beginInfo.renderArea.offset = {0, 0};
            beginInfo.renderPass = vk.renderPass;

            vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                defaultPipeline.handle
            );
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(
                cmd,
                0, 1,
                &computedBuffer.handle,
                offsets
            );
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                defaultPipeline.layout,
                0, 1,
                &defaultPipeline.descriptorSet,
                0, nullptr
            );
            vkCmdDraw(
                cmd,
                computedVertexCount,
                1,
                0,
                0
            );

            vkCmdEndRenderPass(cmd);

            VKCHECK(vkEndCommandBuffer(cmd));
        }
    }

    // Initialize DirectInput.
    DirectInput directInput(instance);
    auto mouse = directInput.mouse;
    auto controller = directInput.controller;

    // Initialize state.
    float rotY = 0;
    float rotX = 0;
    Uniforms uniforms = {};
    matrixProjection(
        screenWidth,
        screenHeight,
        toRadians(45.f),
        10.f, .1f,
        uniforms.proj
    );
    uniforms.eye.z = -2.f;

    // Main loop.
    LARGE_INTEGER frameStart = {};
    LARGE_INTEGER frameEnd = {};
    i64 frameDelta = 0;
    BOOL done = false;
    int errorCode = 0;
    while (!done) {
        QueryPerformanceCounter(&frameStart);

        MSG msg;
        BOOL messageAvailable; 
        do {
            messageAvailable = PeekMessage(
                &msg,
                (HWND)nullptr,
                0, 0,
                PM_REMOVE
            );
            TranslateMessage(&msg); 
            if (msg.message == WM_QUIT) {
                done = true;
                errorCode = (int)msg.wParam;
            }
            DispatchMessage(&msg); 
        } while(!done && messageAvailable);

        // Render frame.
        present(vk, cmds, 1);
        
        // Frame rate independent movement stuff.
        QueryPerformanceCounter(&frameEnd);
        float frameTime = (frameEnd.QuadPart - frameStart.QuadPart) /
            (float)counterFrequency.QuadPart;
        float moveDelta = DELTA_MOVE_PER_S * frameTime;

        // Mouse.
        Vec2i mouseDelta = mouse->getDelta();
        auto mouseDeltaX = mouseDelta.x * MOUSE_SENSITIVITY;
        rotY -= mouseDeltaX;
        auto mouseDeltaY = mouseDelta.y * MOUSE_SENSITIVITY;
        rotX += mouseDeltaY;
        quaternionInit(uniforms.rotation);
        rotateQuaternionY(rotY, uniforms.rotation);
        rotateQuaternionX(rotX, uniforms.rotation);

        // Keyboard.
        if (keyboard['W']) {
            moveAlongQuaternion(moveDelta, uniforms.rotation, uniforms.eye);
        }
        if (keyboard['S']) {
            moveAlongQuaternion(-moveDelta, uniforms.rotation, uniforms.eye);
        }
        if (keyboard['A']) {
            movePerpendicularToQuaternion(-moveDelta, uniforms.rotation, uniforms.eye);
        }
        if (keyboard['D']) {
            movePerpendicularToQuaternion(moveDelta, uniforms.rotation, uniforms.eye);
        }

        updateUniforms(vk, &uniforms, sizeof(uniforms));
    }
    arrfree(cmds);

    return errorCode;
}
