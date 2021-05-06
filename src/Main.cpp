#include <Windows.h>

#include <cstdio>
#include <cstdint>

#include "jcwk/Logging.h"
#include "jcwk/MathLib.cpp"
#include "jcwk/Types.h"

#pragma pack(push, 1)
struct Vertex {
    Vec4 position;
    Vec4 normal;
};
struct Params {
    Vec4 baseOffset;
};
struct Uniforms {
    float proj[16];
    Vec4 eye;
    Quaternion rotation;
};
#pragma pack(pop)

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
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#ifdef WIN32
#include "jcwk/FileSystem.cpp"
#include "jcwk/Win32/DirectInput.cpp"
#include "jcwk/Win32/Controller.cpp"
#include "jcwk/Win32/Mouse.cpp"
#define VULKAN_COMPUTE
#define VK_USE_PLATFORM_WIN32_KHR
#include "jcwk/Vulkan.cpp"
#endif

const float DELTA_MOVE_PER_S = 10.f;
const float MOUSE_SENSITIVITY = 0.1f;
const float JOYSTICK_SENSITIVITY = 5;
bool keyboard[VK_OEM_CLEAR] = {};

LRESULT __stdcall
VKAPI_CALL WindowProc(
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
        default:
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
    initLogging();

    // NOTE: Create window.
    int screenWidth;
    int screenHeight;
    HWND window;
    {
        WNDCLASSEX windowClassProperties = {};
        windowClassProperties.cbSize = sizeof(windowClassProperties);
        windowClassProperties.style = CS_HREDRAW | CS_VREDRAW;
        windowClassProperties.lpfnWndProc = (WNDPROC)WindowProc;
        windowClassProperties.hInstance = instance;
        windowClassProperties.lpszClassName = "MainWindowClass";
        ATOM windowClass = RegisterClassEx(&windowClassProperties);
        CHECK(windowClass, "Could not create window class")

        window = CreateWindowEx(
            0,
            "MainWindowClass",
            "guacamole",
            WS_POPUP | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            800,
            nullptr,
            nullptr,
            instance,
            nullptr
        );
        CHECK(window, "Could not create window")

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

        INFO("Window created")
    }

    // Create Vulkan instance.
    Vulkan vk;
    vk.extensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    createVKInstance(vk);
    INFO("Vulkan instance created")

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
        VKCHECK(result, "could not create win32 surface")
        INFO("Surface created")
    }

    // Initialize Vulkan.
    initVK(vk);
    INFO("Vulkan initialized")

    // Load fonts.
    stbtt_bakedchar bakedChars[96];
    {
        // auto fontFile = openFile("./FiraCode-Bold.tff", "r");
        auto fontFile = openFile("c:/windows/fonts/times.ttf", "r");
        auto ttfBuffer = new u8[1 << 20];
        fread(ttfBuffer, 1, 1<<20, fontFile);
        const u32 fontWidth = 512;
        const u32 fontHeight = 512;
        u8 bitmap[fontWidth * fontHeight];
        stbtt_BakeFontBitmap(
            ttfBuffer,
            0,
            32.f,
            bitmap,
            fontWidth,
            fontHeight,
            32, 96,
            bakedChars
        );
        delete[] ttfBuffer;
        VulkanSampler sampler = {};
        uploadTexture(
            vk,
            fontWidth,
            fontHeight,
            VK_FORMAT_R8_UNORM,
            bitmap,
            fontWidth * fontHeight,
            sampler
        );
    }

    // Init & execute compute shader.
    const size_t computedBufferCount = 3 * 3 * 3;
    VulkanBuffer computedBuffers[computedBufferCount] = {};
    const u32 computeWidth = 32;
    const u32 computeHeight = computeWidth;
    const u32 computeDepth = computeWidth;
    const u32 computeCount = computeWidth * computeHeight * computeDepth;
    const u32 computeVerticesPerExecution = 15;
    const u32 computedVertexCount = computeVerticesPerExecution * computeCount;
    const u32 computedVertexWidth = sizeof(Vertex);
    const int computeSize = computedVertexCount * computedVertexWidth;
    {
        VulkanPipeline pipeline;
        initVKPipelineCompute(
            vk,
            "cs",
            pipeline
        );
        for (int i = 0; i < computedBufferCount; i++) {
            createComputeToVertexBuffer(
                vk.device,
                vk.memories,
                vk.computeQueueFamily,
                computeSize,
                computedBuffers[i]
            );
        }
        auto bufferIdx = 0;
        for (int x = 0; x < 3; x++) {
            for (int y = 0; y < 3; y++) {
                for (int z = 0; z < 3; z++) {
                    updateStorageBuffer(
                        vk.device,
                        pipeline.descriptorSet,
                        0,
                        computedBuffers[bufferIdx].handle
                    );
                    Params params = {
                        { x * 32.f, y * 32.f, z * 32.f, 0 }
                    };
                    dispatchCompute(
                        vk,
                        pipeline,
                        computeWidth, computeHeight, computeDepth,
                        sizeof(params), &params
                    );
                    bufferIdx++;
                }
            }
        }
        // Have to wait here before we transfer ownership of the buffer.
        vkQueueWaitIdle(vk.computeQueue);
        for (int i = 0; i < computedBufferCount; i++) {
            transferBufferOwnership(
                vk.device,
                vk.cmdPoolComputeTransient,
                vk.cmdPoolTransient,
                vk.computeQueue,
                vk.queue,
                computedBuffers[i].handle,
                vk.computeQueueFamily,
                vk.queueFamily,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT
            );
        }
    }
    // Wait for transfer to complete.
    vkDeviceWaitIdle(vk.device);

    // Setup pipelines.
    VulkanPipeline defaultPipeline;
    {
        initVKPipeline(
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
    }

    // Initialize DirectInput.
    DirectInput directInput(instance);
    auto mouse = directInput.mouse;

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

        // Acquire swap image.
        uint32_t swapImageIndex = 0;
        auto result = vkAcquireNextImageKHR(
            vk.device,
            vk.swap.handle,
            std::numeric_limits<uint64_t>::max(),
            vk.swap.imageReady,
            VK_NULL_HANDLE,
            &swapImageIndex
        );
        if ((result == VK_SUBOPTIMAL_KHR) ||
            (result == VK_ERROR_OUT_OF_DATE_KHR)) {
            // TODO(jan): implement resize
            ERR("could not acquire next image");
        } else if (result != VK_SUCCESS) {
            ERR("could not acquire next image");
        }

        // Render.
        VkCommandBuffer cmd;
        createCommandBuffers(vk.device, vk.cmdPool, 1, &cmd);
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
        beginInfo.framebuffer = vk.swap.framebuffers[swapImageIndex];
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
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            defaultPipeline.layout,
            0, 1,
            &defaultPipeline.descriptorSet,
            0, nullptr
        );
        for (auto & computedBuffer: computedBuffers) {
            vkCmdBindVertexBuffers(
                cmd,
                0, 1,
                &computedBuffer.handle,
                offsets
            );
            vkCmdDraw(
                cmd,
                computedVertexCount,
                1,
                0,
                0
            );
        }
        vkCmdEndRenderPass(cmd);
        VKCHECK(vkEndCommandBuffer(cmd))

        // Present
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &vk.swap.imageReady;
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &vk.swap.cmdBufferDone;
        vkQueueSubmit(vk.queue, 1, &submitInfo, VK_NULL_HANDLE);
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &vk.swap.handle;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &vk.swap.cmdBufferDone;
        presentInfo.pImageIndices = &swapImageIndex;
        VKCHECK(vkQueuePresentKHR(vk.queue, &presentInfo));
        vkDeviceWaitIdle(vk.device); //@perf

        vkFreeCommandBuffers(
            vk.device,
            vk.cmdPool,
            1,
            &cmd
        );

        // Frame rate independent movement stuff.
        QueryPerformanceCounter(&frameEnd);
        float frameTime = (float)(frameEnd.QuadPart - frameStart.QuadPart) /
            (float)counterFrequency.QuadPart;
        float moveDelta = DELTA_MOVE_PER_S * frameTime;

        // Mouse.
        Vec2i mouseDelta = mouse->getDelta();
        auto mouseDeltaX = (float)mouseDelta.x * MOUSE_SENSITIVITY;
        rotY -= mouseDeltaX;
        auto mouseDeltaY = (float)mouseDelta.y * MOUSE_SENSITIVITY;
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

    return errorCode;
}
