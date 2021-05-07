struct Chunk {
    Vec3i coord;
    VulkanBuffer computeBuffer;
    VulkanBuffer vertexBuffer;
    u32 vertexCount;
};

const u32 computeWidth = 32;
const u32 computeHeight = computeWidth;
const u32 computeDepth = computeWidth;
const u32 computeCount = computeWidth * computeHeight * computeDepth;
const u32 computeVerticesPerExecution = 15;
const u32 computeVertexCount = computeVerticesPerExecution * computeCount;
const u32 computeVertexWidth = sizeof(Vertex);
const int computeSize = computeVertexCount * computeVertexWidth;

void chunkTriangulate(Vulkan& vk, Chunk& chunk) {
    INFO(
        "Triangulating chunk (%dx %dy %dz)",
        chunk.coord.x, chunk.coord.y, chunk.coord.z
    );
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
            {chunk.coord.x * 32.f, chunk.coord.y * 32.f, chunk.coord.z * 32.f, 0}
        };
        dispatchCompute(
            vk,
            pipeline,
            computeWidth, computeHeight, computeDepth,
            sizeof(params), &params
        );
        vkQueueWaitIdle(vk.computeQueue);
    }
    INFO(
        "Triangulated chunk (%dx %dy %dz)",
        chunk.coord.x, chunk.coord.y, chunk.coord.z
    );
}

void chunkPack(
    Vulkan& vk,
    Chunk& chunk
) {
    INFO(
        "Packing chunk (%dx %dy %dz)",
        chunk.coord.x, chunk.coord.y, chunk.coord.z
    );
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
        for (int it = 0; it < computeVertexCount; it++) {
            if ((src->position.x != 0.f) ||
                (src->position.y != 0.f) ||
                (src->position.z != 0.f)) {
                *dst = *src;
                dst++;
                vertexCount++;
            }
            src++;
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
}

struct PackParams {
    Vulkan* vk;
    Chunk* chunk;
};

DWORD WINAPI PackThread(LPVOID param) {
    auto params = (PackParams*)param;
    auto& chunk = *params->chunk;
    INFO(
        "PackThread: chunk (%dx %dy %dz), params = %p, vk.handle = %d",
        chunk.coord.x, chunk.coord.y, chunk.coord.z, param, params->vk->handle
    );
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

    chunkTriangulate(vk, chunk);
#if 1
    auto params = new PackParams;
    params->vk = &vk;
    params->chunk = &chunk;
    INFO(
        "generate: chunk (%dx %dy %dz), params = %p, vk.handle = %d",
        params->chunk->coord.x, params->chunk->coord.y, params->chunk->coord.z, params, params->vk->handle);
    CreateThread(
        NULL,
        0,
        PackThread,
        params,
        0,
        NULL
    );
#else
    chunkPack(vk, chunk);
#endif
}
