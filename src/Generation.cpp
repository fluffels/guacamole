#include <queue>

#pragma pack(push, 1)
struct Params {
    Vec4 baseOffset;
    Vec4i dimensions;
};
#pragma pack(pop)

struct Chunk {
    Vec3i coord;
    VulkanBuffer computeBuffer;
    VulkanBuffer vertexBuffer;
    u32 vertexCount;
};

struct GenerateWorkItem {
    Vulkan* vk;
    Vec3i coord;
    Chunk* chunk;
};

HANDLE generateWorkQueueMutex;
std::queue<GenerateWorkItem> generateWorkQueue;
HANDLE generateWorkSemaphore;

u32 chunksTriangulated = 0;
float triangulationTime = 0.f;
u32 chunksPacked = 0;
float packTime = 0.f;

const u32 computeWidth = 16;
const u32 computeHeight = computeWidth;
const u32 computeDepth = computeWidth;
const u32 computeCount = computeWidth * computeHeight * computeDepth;
const u32 computeVerticesPerExecution = 15;
const u32 computeVertexCount = computeVerticesPerExecution * computeCount;
const u32 computeVertexWidth = sizeof(Vertex);
const int computeSize = computeVertexCount * computeVertexWidth;

void generatePushWorkItem(GenerateWorkItem &workItem) {
    switch (WaitForSingleObject(generateWorkQueueMutex, 1000)) {
        case WAIT_ABANDONED:
            FATAL("generate thread crashed");
        case WAIT_OBJECT_0:
            generateWorkQueue.push(workItem);
            // TODO: error handling
            ReleaseMutex(generateWorkQueueMutex);
            // TODO: error handling
            ReleaseSemaphore(generateWorkSemaphore, 1, nullptr);
            break;
        case WAIT_TIMEOUT:
            FATAL("generate thread hung");
        case WAIT_FAILED:
            // TODO: Call GetLastError here and FormatMessage for a more
            // descriptive error message.
            FATAL("unknown error");
    }
}

GenerateWorkItem generatePopWorkItem() {
    switch (WaitForSingleObject(generateWorkQueueMutex, 1000)) {
        case WAIT_ABANDONED: FATAL("generate thread crashed");
        case WAIT_OBJECT_0: {
            auto workItem = generateWorkQueue.front();
            generateWorkQueue.pop();
            ReleaseMutex(generateWorkQueueMutex);
            return workItem;
        }
        case WAIT_TIMEOUT: FATAL("generate thread hung");
        // TODO: Call GetLastError here and FormatMessage for a more
        // descriptive error message.
        case WAIT_FAILED: FATAL("unknown error");
    }
}

void chunkTriangulate(Vulkan& vk, Chunk& chunk) {
    START_TIMER(Triangulate);
    // Init & execute compute shader.
    {
        VulkanPipeline pipeline;
        initVKPipelineCompute(
            vk,
            "cs",
            pipeline
        );
        createStorageBuffer(
            vk.device,
            vk.memories,
            vk.computeQueueFamily,
            computeSize,
            chunk.computeBuffer
        );
        updateStorageBuffer(
            vk.device,
            pipeline.descriptorSet,
            0,
            chunk.computeBuffer.handle
        );
        Params params = {
            {
                chunk.coord.x * (float)computeWidth,
                chunk.coord.y * (float)computeHeight,
                chunk.coord.z * (float)computeDepth,
                0
            },
            {
                computeWidth,
                computeHeight,
                computeDepth,
                0
            }
        };
        dispatchCompute(
            vk,
            pipeline,
            computeWidth, computeHeight, computeDepth,
            sizeof(params), &params
        );
        vkQueueWaitIdle(vk.computeQueue);
    }
    END_TIMER(Triangulate);
    triangulationTime += DELTA(Triangulate);
    chunksTriangulated++;
}

void chunkPack(
    Vulkan& vk,
    Chunk& chunk
) {
    START_TIMER(Pack);
    createVertexBuffer(
        vk.device,
        vk.memories,
        vk.queueFamily,
        computeSize, //TODO: this is way too big
        chunk.vertexBuffer
    );

    u32 vertexCount = 0;
    {
        auto src = (Vertex*)mapMemory(vk.device, chunk.computeBuffer.memory);
        auto dst = (Vertex*)mapMemory(vk.device, chunk.vertexBuffer.memory);
        for (int i = 0; i < computeCount; i++) {
            for (int j = 0; j < computeVerticesPerExecution; j++) {
                if ((src->position.x == 0.f) &&
                        (src->position.y == 0.f) &&
                        (src->position.z == 0.f)) {
                    src += computeVerticesPerExecution - j;
                    break;
                } else {
                    *dst = *src;
                    dst++;
                    vertexCount++;
                    src++;
                }
            }
        }
        unMapMemory(vk.device, chunk.computeBuffer.memory);
        unMapMemory(vk.device, chunk.vertexBuffer.memory);
        destroyBuffer(vk, chunk.computeBuffer);
        chunk.computeBuffer = {};
    }

    chunk.vertexCount = vertexCount;

    INFO(
        "Packed chunk (%dx %dy %dz)",
        chunk.coord.x, chunk.coord.y, chunk.coord.z
    );

    END_TIMER(Pack);
    packTime += DELTA(Pack);
    chunksPacked++;
}

struct PackParams {
    Vulkan* vk;
    Chunk* chunk;
};

DWORD WINAPI PackThread(LPVOID param) {
    auto params = (PackParams*)param;
    auto& chunk = *params->chunk;
    chunkPack(*params->vk, *params->chunk);
    delete params;
    return 0;
}

void generateChunk(
    Vulkan& vk,
    Vec3i chunkCoord,
    Chunk& chunk
) {
    chunk = {};
    chunk.coord = chunkCoord;

    INFO(
        "Generating chunk (%dx %dy %dz)",
        chunk.coord.x, chunk.coord.y, chunk.coord.z
    );
    chunkTriangulate(vk, chunk);
    INFO(
        "Triangulated chunk (%dx %dy %dz)",
        chunk.coord.x, chunk.coord.y, chunk.coord.z
    );
    auto params = new PackParams;
    params->vk = &vk;
    params->chunk = &chunk;
    CreateThread(
        NULL,
        0,
        PackThread,
        params,
        0,
        NULL
    );
}

[[noreturn]] DWORD WINAPI GenerateThread(LPVOID param) {
    while (true) {
        // NOTE: Wait for work to be enqueued so the thread doesn't just spin.
        switch (WaitForSingleObject(generateWorkSemaphore, INFINITE)) {
            case WAIT_ABANDONED: FATAL("semaphore abandoned");
            case WAIT_OBJECT_0: {
                while (!generateWorkQueue.empty()) {
                    auto workItem = generatePopWorkItem();
                    generateChunk(
                        *workItem.vk,
                        workItem.coord,
                        *workItem.chunk
                    );
                }
            }
            break;
            case WAIT_TIMEOUT: FATAL("semaphore timeout");
                // TODO: Call GetLastError here and FormatMessage for a more
                // descriptive error message.
            case WAIT_FAILED: FATAL("unknown error");
        }
    }
}

void initGenerate() {
    generateWorkQueueMutex = CreateMutex(
        nullptr,
        false,
        "generateWorkQueue"
    );
    CHECK(generateWorkQueueMutex, "Could not create mutex");

    generateWorkSemaphore = CreateSemaphore(
        nullptr,
        0,
        // TODO: bogus value for lMaximumCount.
        1 < 10,
        "generateWork"
    );
    CHECK(generateWorkSemaphore, "Could not create semaphore");

    CreateThread(
        nullptr,
        0,
        GenerateThread,
        nullptr,
        0,
        nullptr
    );
}
