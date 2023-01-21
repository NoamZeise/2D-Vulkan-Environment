#ifndef PARTS_DESCRIPTORS_H
#define PARTS_DESCRIPTORS_H

#include "../volk.h"

#include <vector>

#include "../descriptor_structs.h"
//#include "../render_structs.h"
struct Base;

namespace part
{
    namespace create
    {
        void DescriptorSetLayout(VkDevice device, DS::DescriptorSet *ds, std::vector<DS::Binding*> bindings, VkShaderStageFlagBits stageFlags);
        void DescriptorPoolAndSet(VkDevice device, VkDescriptorPool* pool, std::vector<DS::DescriptorSet*> descriptorSets, uint32_t frameCount);
        void PrepareShaderBufferSets(Base base,	std::vector<DS::Binding*> ds, VkBuffer* buffer, VkDeviceMemory* memory);
    }
}

#endif
