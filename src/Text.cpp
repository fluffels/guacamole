struct TextVertex {
    Vec2 position;
    Vec2 tex;
};

stbtt_bakedchar bakedChars[96];

VulkanPipeline textPipeline;

u32 currentLine;

void initText(
    Vulkan& vk
) {
    // Load fonts.
    VulkanSampler fontAtlas = {};
    {
        auto fontFile = openFile("fonts/FiraCode-Bold.ttf", "r");
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
        uploadTexture(
            vk,
            fontWidth,
            fontHeight,
            VK_FORMAT_R8_UNORM,
            bitmap,
            fontWidth * fontHeight,
            fontAtlas
        );
    }
    initVKPipelineNoCull(
        vk,
        "text",
        textPipeline
    );
    updateUniformBuffer(
        vk.device,
        textPipeline.descriptorSet,
        0,
        vk.uniforms.handle
    );
    updateCombinedImageSampler(
        vk.device,
        textPipeline.descriptorSet,
        1,
        &fontAtlas,
        1
    );
}

void displayLine(
    Vulkan& vk,
    VkCommandBuffer cmd,
    const char* text
) {
    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        textPipeline.handle
    );
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        textPipeline.layout,
        0, 1, &textPipeline.descriptorSet,
        0, nullptr
    );
    auto c = (char *) text;
    size_t textLength = strlen(c);
    float xPos = 16.f;
    float yPos = (float)(currentLine+1) * 32.f;
    size_t textVertexCount = textLength * 4;
    size_t textIndexCount = textLength * 6;
    size_t textVertexSize = textVertexCount * sizeof(TextVertex);
    size_t textIndexSize = textIndexCount * sizeof(u32);
    TextVertex *textVertices = nullptr;
    TextVertex *v = arraddnptr(textVertices, textVertexCount);
    u32 *textIndices = nullptr;
    u32 *i = arraddnptr(textIndices, textIndexCount);
    u32 it = 0;
    while (*c) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(bakedChars, 512, 512, *c - 32, &xPos, &yPos, &q, 1);
        {
            v->tex.x = q.s0;
            v->tex.y = q.t1;
            v->position.x = q.x0;
            v->position.y = q.y1;
            v++;
        }
        {
            v->tex.x = q.s1;
            v->tex.y = q.t1;
            v->position.x = q.x1;
            v->position.y = q.y1;
            v++;
        }
        {
            v->tex.x = q.s1;
            v->tex.y = q.t0;
            v->position.x = q.x1;
            v->position.y = q.y0;
            v++;
        }
        {
            v->tex.x = q.s0;
            v->tex.y = q.t0;
            v->position.x = q.x0;
            v->position.y = q.y0;
            v++;
        }
        i[0] = it + 0;
        i[1] = it + 1;
        i[2] = it + 2;
        i[3] = it + 2;
        i[4] = it + 3;
        i[5] = it + 0;
        i += 6;
        it += 4;
        c++;
    }
    VulkanMesh textMesh = {}; //@leak
    uploadMesh(
        vk,
        textVertices, textVertexSize,
        textIndices, textIndexSize,
        textMesh
    );
    arrfree(textVertices);
    arrfree(textIndices);
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(
        cmd,
        0, 1,
        &textMesh.vBuff.handle,
        offsets
    );
    vkCmdBindIndexBuffer(
        cmd,
        textMesh.iBuff.handle,
        0,
        VK_INDEX_TYPE_UINT32 //FIXME: should be 16
    );
    vkCmdDrawIndexed(
        cmd,
        textIndexCount,
        1, 0, 0, 0
    );
    currentLine++;
}
