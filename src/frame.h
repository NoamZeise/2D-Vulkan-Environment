#ifndef VK_ENV_FRAME
#define VK_ENV_FRAME

#include <volk.h>
struct Frame {
    Frame(VkDevice device,  uint32_t graphicsQueueIndex);
    ~Frame();

    VkResult startFrame(VkCommandBuffer *pCmdBuff);
    
    VkDevice device;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapchainImageReady;
    VkSemaphore presentReady;
    VkFence frameFinished;
};

#endif
