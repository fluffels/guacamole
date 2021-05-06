struct Chunk {
    Vec3i chunkCoord;
    VulkanBuffer vertexBuffer;
    u32 vertexCount;
};

void generate(Vulkan& vk, Chunk& chunk) {
    Vec3i chunkCoord = {0, 0, 0};
    // Init & execute compute shader.
    VulkanBuffer computeBuffer = {};
    const u32 computeWidth = 32;
    const u32 computeHeight = computeWidth;
    const u32 computeDepth = computeWidth;
    const u32 computeCount = computeWidth * computeHeight * computeDepth;
    const u32 computeVerticesPerExecution = 15;
    const u32 computeVertexCount = computeVerticesPerExecution * computeCount;
    const u32 computeVertexWidth = sizeof(Vertex);
    const int computeSize = computeVertexCount * computeVertexWidth;
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
            computeBuffer
        );
        updateStorageBuffer(
            vk.device,
            pipeline.descriptorSet,
            0,
            computeBuffer.handle
        );
        Params params = {
            {chunkCoord.x * 32.f, chunkCoord.y * 32.f, chunkCoord.z * 32.f, 0}
        };
        dispatchCompute(
            vk,
            pipeline,
            computeWidth, computeHeight, computeDepth,
            sizeof(params), &params
        );
        vkQueueWaitIdle(vk.computeQueue);
    }

    createVertexBuffer(
        vk.device,
        vk.memories,
        vk.queueFamily,
        computeSize,
        chunk.vertexBuffer
    );

    chunk.vertexCount = 0;
    {
        auto src = (Vertex*)mapMemory(vk.device, computeBuffer.memory);
        auto dst = (Vertex*)mapMemory(vk.device, chunk.vertexBuffer.memory);
        for (int it = 0; it < computeVertexCount; it++) {
            if ((src->position.x != 0.f) ||
                (src->position.y != 0.f) ||
                (src->position.z != 0.f)) {
                *dst = *src;
                dst++;
                chunk.vertexCount++;
            }
            src++;
        }
        unMapMemory(vk.device, computeBuffer.memory);
        unMapMemory(vk.device, chunk.vertexBuffer.memory);
        destroyBuffer(vk, computeBuffer);
    }
}