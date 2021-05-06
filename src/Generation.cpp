struct Chunk {
    Vec3i coord;
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

void generate(Vulkan& vk, Vec3i& coord, Chunk& chunk) {
    chunk.coord = coord;
    // Init & execute compute shader.
    VulkanBuffer computeBuffer = {};
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
            {coord.x * 32.f, coord.y * 32.f, coord.z * 32.f, 0}
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