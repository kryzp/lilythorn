#ifndef VK_BUFFER_H_
#define VK_BUFFER_H_

#include "third_party/volk.h"
#include "third_party/vk_mem_alloc.h"

#include "core/common.h"

#include "rendering/bindless_resource_mgr.h"

#include "command_buffer.h"

namespace llt
{
	class Texture;
	class VulkanCore;

	class GPUBuffer
	{
	public:
		GPUBuffer(VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
		~GPUBuffer();

		void create(uint64_t size);
		void cleanUp();

		void readDataFromMe(void *dst, uint64_t length, uint64_t offset) const;
		void writeDataToMe(const void *src, uint64_t length, uint64_t offset) const;

		void writeToBuffer(const GPUBuffer *other, uint64_t length, uint64_t srcOffset, uint64_t dstOffset);

		void writeToTextureSingle(const Texture *texture, uint64_t size, uint64_t offset = 0, uint32_t baseArrayLayer = 0);
		void writeToTexture(CommandBuffer &commandBuffer, const Texture *texture, uint64_t size, uint64_t offset = 0, uint32_t baseArrayLayer = 0);

		VkDescriptorBufferInfo getDescriptorInfo(uint32_t offset = 0) const;
		VkDescriptorBufferInfo getDescriptorInfoRange(uint32_t range, uint32_t offset = 0) const;

		VkBuffer getHandle() const;
		VkBufferUsageFlags getUsage() const;

		VmaMemoryUsage getMemoryUsage() const;

		uint64_t getSize() const;

		const BindlessResourceHandle &getBindlessHandle() const;

	private:
		VkBuffer m_buffer;
		
		VmaAllocation m_allocation;
		VmaAllocationInfo m_allocationInfo;

		VkBufferUsageFlags m_usage;
		VmaMemoryUsage m_memoryUsage;

		uint64_t m_size;

		BindlessResourceHandle m_bindlessHandle;
	};
}

#endif // VK_BUFFER_H_
