struct Graph {
    struct Vertex {
        Vec2 position;
    };

    VulkanPipeline pipeline = {};
    VulkanBuffer vertexBuffer = {};
    VulkanBuffer indexBuffer = {};

    const u32 barCount = 500;
    const u32 vertexCount = (barCount+1) * 4;
    const u32 indexCount = (barCount+1) * 6;
    const u32 vertexBufferSize = vertexCount * sizeof(Vertex);
    const u32 indexBufferSize = indexCount * sizeof(u16);
} graph;

void graphInit(
    Vulkan& vk
) {
    initVKPipelineNoCull(
        vk,
        "graph",
        graph.pipeline
    );
    updateUniformBuffer(
        vk.device,
        graph.pipeline.descriptorSet,
        0,
        vk.uniforms.handle
    );
    createVertexBuffer(
        vk.device,
        vk.memories,
        vk.queueFamily,
        graph.vertexBufferSize,
        graph.vertexBuffer
    );
    createIndexBuffer(
        vk.device,
        vk.memories,
        vk.queueFamily,
        graph.indexBufferSize,
        graph.indexBuffer
    );
}

void graphDraw(
    Vulkan& vk,
    VkCommandBuffer cmd,
    float* frameTimes,
    u32 frameIdx
) {
    auto vertex = (Graph::Vertex*)mapMemory(vk.device, graph.vertexBuffer.memory);
    auto index = (u16*)mapMemory(vk.device, graph.indexBuffer.memory);

    for (u32 i = 0; i < graph.barCount; i++) {
        u32 idx = (frameIdx + i) % graph.barCount;
        float height = ((frameTimes[idx] * 1000) / 32) * 1000.f;

        // TODO: Fix hardcoded vals
        vertex->position.x = float(i)*2 + 0.f;
        vertex->position.y = 1080.f;
        vertex++;

        vertex->position.x = float(i)*2 + 0.f;
        vertex->position.y = 1080.f - height;
        vertex++;

        vertex->position.x = float(i)*2 + 1.f;
        vertex->position.y = 1080.f - height;
        vertex++;

        vertex->position.x = float(i)*2 + 1.f;
        vertex->position.y = 1080.f;
        vertex++;

        u32 baseIdx = i * 4;
        *index++ = baseIdx + 0;
        *index++ = baseIdx + 1;
        *index++ = baseIdx + 2;
        *index++ = baseIdx + 2;
        *index++ = baseIdx + 3;
        *index++ = baseIdx + 0;
    }

    {
        float height = (16.f / 32.f) * 1000.f;
        vertex->position.x = 0.f;
        vertex->position.y = 1080.f - height;
        vertex++;

        vertex->position.x = graph.barCount * 2;
        vertex->position.y = 1080.f - height;
        vertex++;

        vertex->position.x = graph.barCount * 2;
        vertex->position.y = 1080.f - height + 1;
        vertex++;

        vertex->position.x = 0.f;
        vertex->position.y = 1080.f - height + 1;
        vertex++;

        u32 baseIdx = graph.barCount * 4;
        *index++ = baseIdx + 0;
        *index++ = baseIdx + 1;
        *index++ = baseIdx + 2;
        *index++ = baseIdx + 2;
        *index++ = baseIdx + 3;
        *index++ = baseIdx + 0;
    }

    unMapMemory(vk.device, graph.vertexBuffer.memory);
    unMapMemory(vk.device, graph.indexBuffer.memory);

    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graph.pipeline.handle
    );
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graph.pipeline.layout,
        0, 1, &graph.pipeline.descriptorSet,
        0, nullptr
    );
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(
        cmd,
        0, 1,
        &graph.vertexBuffer.handle,
        offsets
    );
    vkCmdBindIndexBuffer(
        cmd,
        graph.indexBuffer.handle,
        0,
        VK_INDEX_TYPE_UINT16
    );
    vkCmdDrawIndexed(
        cmd,
        graph.indexCount,
        1, 0, 0, 0
    );
}
