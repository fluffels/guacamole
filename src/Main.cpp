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
struct Uniforms {
    float proj[16];
    float ortho[16];
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

#include "jcwk/Timer.h"
#include "Text.cpp"
#include "PerfGraph.cpp"
#include "Generation.cpp"

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

    initText(vk);
    graphInit(vk);
    initGenerate();

    // FIXME: This has a static size at the moment which is not optimal because
    // ideally we'd like to have an infinitely expanding world. The reason it
    // can't arbitrarily expand is because that relocates some of the pointers
    // used in the threads spawned by the generation code.
    u32 nextChunkIdx = 0;
    vector<Chunk> chunks(1 << 10);

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

    // Generate first chunk.
    GenerateWorkItem workItem = {};
    workItem.vk = &vk;
    workItem.coord = {0, 0, 0};
    workItem.chunk = &chunks[0];
    nextChunkIdx++;
    generatePushWorkItem(workItem);

    // Initialize DirectInput.
    DirectInput directInput(instance);
    auto mouse = directInput.mouse;

    // Initialize state.
    float rotY = 0;
    float rotX = 0;
    Uniforms uniforms = {};
    quaternionInit(uniforms.rotation);
    matrixProjection(
        screenWidth,
        screenHeight,
        toRadians(45.f),
        10.f, .1f,
        uniforms.proj
    );
    matrixOrtho(
        screenWidth,
        screenHeight,
        uniforms.ortho
    );
    updateUniforms(vk, &uniforms, sizeof(uniforms));

    // Main loop.
    LARGE_INTEGER firstFrame = {};
    QueryPerformanceCounter(&firstFrame);
    LARGE_INTEGER frameStart = {};
    LARGE_INTEGER frameEnd = {};
    Vec3i currentChunkCoord = {};
    float frameTime = 0;
    float averageFrameTime = 0;
    float frameCount = 0;
    auto frameTimes = new float[graph.barCount];
    u32 lastFrameTimeIdx = 0;
    u32 frameTimeIdx = 0;
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

        currentChunkCoord.x = (i32)floor(uniforms.eye.x / computeWidth);
        currentChunkCoord.y = (i32)floor(uniforms.eye.y / computeHeight);
        currentChunkCoord.z = (i32)floor(uniforms.eye.z / computeDepth);

        vector<Vec3i> requestedChunkCoords;
        {
            const i32 range = 4;
            const i32 coordCount = (range+1)*(range+1)*(range+1);
            requestedChunkCoords.resize(coordCount);

            u32 i = 0;
            const i32 rangeMin = 0 - range/2;
            const i32 rangeMax = 0 + range/2;
            for (i32 x = rangeMin; x <= rangeMax; x++) {
                for (i32 y = rangeMin; y <= rangeMax; y++) {
                    for (i32 z = rangeMin; z <= rangeMax; z++) {
                        auto& coord = requestedChunkCoords[i];
                        coord.x = currentChunkCoord.x + x;
                        coord.y = currentChunkCoord.y + y;
                        coord.z = currentChunkCoord.z + z;
                        i++;
                    }
                }
            }
        }

        vector<Vec3i> chunksToGenerate;
        for (auto& coord: requestedChunkCoords) {
            bool chunkAvailable = false;

            for (auto& chunk: chunks) {
                // FIXME: this search is probably kinda slow.
                if (vectorEquals(chunk.coord, coord)) {
                    chunkAvailable = true;
                    break;
                }
            }

            if (!chunkAvailable) {
                auto& chunk = chunks[nextChunkIdx];
                chunk.coord = coord;
                nextChunkIdx++;

                GenerateWorkItem workItem = {};
                workItem.vk = &vk;
                workItem.coord = coord;
                workItem.chunk = &chunk;
                generatePushWorkItem(workItem);
            }
        }

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
        auto drawCallCount = 0;
        auto drawnVertexCount = 0;
        VkCommandBuffer cmd;
        {
            createCommandBuffers(vk.device, vk.cmdPool, 1, &cmd);
            beginFrameCommandBuffer(cmd);

            VkClearValue colorClear;
            colorClear.color = {};
            VkClearValue depthClear;
            depthClear.depthStencil = {1.f, 0};
            VkClearValue clears[] = {colorClear, depthClear};

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
            for (u32 chunkIdx = 0; chunkIdx < nextChunkIdx; chunkIdx++) {
                auto& chunk = chunks[chunkIdx];
                if (chunk.vertexCount) {
                    Vec3 corners[8] = {
                        { chunk.min.x, chunk.min.y, chunk.min.z },
                        { chunk.min.x, chunk.min.y, chunk.max.z },
                        { chunk.min.x, chunk.max.y, chunk.min.z },
                        { chunk.min.x, chunk.max.y, chunk.max.z },
                        { chunk.max.x, chunk.min.y, chunk.min.z },
                        { chunk.max.x, chunk.min.y, chunk.max.z },
                        { chunk.max.x, chunk.max.y, chunk.min.z },
                        { chunk.max.x, chunk.max.y, chunk.max.z }
                    };
                    Vec3 eye = { -uniforms.eye.x, -uniforms.eye.y, -uniforms.eye.z };
                    bool insideViewFrustum = false;
                    for (auto& corner: corners) {
                        vectorAdd(corner, eye, corner);
                        rotatePoint(uniforms.rotation, corner, corner);
                        matrixMultiplyPoint(uniforms.proj, corner, corner);
                        if (corner.z >= 0) {
                            insideViewFrustum = true;
                            break;
                        }
                    }
                    if (!insideViewFrustum) continue;

                    drawCallCount++;
                    drawnVertexCount += chunk.vertexCount;
                    vkCmdBindVertexBuffers(
                        cmd,
                        0, 1,
                        &chunk.vertexBuffer.handle,
                        offsets
                    );
                    vkCmdDraw(
                        cmd,
                        chunk.vertexCount,
                        1,
                        0,
                        0
                    );
                }
            }

            startText();
            display("%.4fms (%.2f Hz)", frameTime * 1000, 1.f / frameTime);
            display("%.4fms (%.2f Hz)", averageFrameTime * 1000, 1.f / averageFrameTime);
            display(
                "%.4fx %.4fy %.4fz",
                uniforms.eye.x, uniforms.eye.y, uniforms.eye.z
            );
            display(
                "%dx %dy %dz (%d chunks)",
                currentChunkCoord.x, currentChunkCoord.y, currentChunkCoord.z, nextChunkIdx
            );
            display(
                "%d vertices in %d calls",
                drawnVertexCount, drawCallCount
            );
            display(
                "%.4fx %.4fy %.4fz %.4fw",
                uniforms.rotation.x,
                uniforms.rotation.y,
                uniforms.rotation.z,
                uniforms.rotation.w
            );
            endText(vk, cmd);

            graphDraw(vk, cmd, frameTimes, lastFrameTimeIdx);

            vkCmdEndRenderPass(cmd);
            VKCHECK(vkEndCommandBuffer(cmd))
        }

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
        frameCount++;
        QueryPerformanceCounter(&frameEnd);
        frameTime = (float)(frameEnd.QuadPart - frameStart.QuadPart) /
            (float)counterFrequency.QuadPart;
        frameTimes[frameTimeIdx] = frameTime;
        lastFrameTimeIdx = frameTimeIdx;
        frameTimeIdx = (frameTimeIdx+1) % graph.barCount;
        averageFrameTime = averageFrameTime * 0.99 + frameTime * 0.01;
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

    INFO("Average frame time: %.2fms", averageFrameTime * 1000);
    INFO("Average triangulation time: %.2fms", (triangulationTime / chunksTriangulated) * 1000);
    INFO("Average pack time: %.2fms", (packTime / chunksPacked) * 1000);

    return errorCode;
}
