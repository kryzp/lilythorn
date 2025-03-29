#include "gpu_buffer.h"

#include "texture.h"
#include "core.h"
#include "util.h"

using namespace llt;

GPUBuffer::GPUBuffer(VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
	: m_buffer(VK_NULL_HANDLE)
	, m_allocation()
	, m_allocationInfo()
	, m_usage(usage)
	, m_memoryUsage(memoryUsage)
	, m_size(0)
	, m_bindlessHandle(BindlessResourceHandle::INVALID)
{
}

GPUBuffer::~GPUBuffer()
{
    cleanUp();
}

void GPUBuffer::create(uint64_t size)
{
	this->m_size = size;

	// todo
//	Vector<uint32_t> queueFamilyIndices = {
//		g_vkCore->m_graphicsQueue.getFamilyIdx().value(),
//		g_vkCore->m_transferQueues[0].getFamilyIdx().value()
//	};

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = m_usage;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.queueFamilyIndexCount = 0;//queueFamilyIndices.size();
	bufferCreateInfo.pQueueFamilyIndices = nullptr;//queueFamilyIndices.data();

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = m_memoryUsage;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	LLT_VK_CHECK(
		vmaCreateBuffer(g_vkCore->m_vmaAllocator, &bufferCreateInfo, &vmaAllocInfo, &m_buffer, &m_allocation, &m_allocationInfo),
		"Failed to create buffer"
	);

	if (m_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		m_bindlessHandle = g_bindlessResources->registerBuffer(this);
}

void GPUBuffer::cleanUp()
{
    if (m_buffer == VK_NULL_HANDLE) {
        return;
    }

	vmaDestroyBuffer(g_vkCore->m_vmaAllocator, m_buffer, m_allocation);

    m_buffer = VK_NULL_HANDLE;
}

void GPUBuffer::readDataFromMe(void *dst, uint64_t length, uint64_t offset) const
{
	vmaCopyAllocationToMemory(g_vkCore->m_vmaAllocator, m_allocation, offset, dst, length);
}

void GPUBuffer::writeDataToMe(const void *src, uint64_t length, uint64_t offset) const
{
	vmaCopyMemoryToAllocation(g_vkCore->m_vmaAllocator, src, m_allocation, offset, length);
}

void GPUBuffer::writeToBuffer(const GPUBuffer *other, uint64_t length, uint64_t srcOffset, uint64_t dstOffset)
{
	CommandBuffer commandBuffer = vkutil::beginSingleTimeCommands(g_vkCore->getTransferCommandPool());

	VkBufferCopy region = {};
	region.srcOffset = srcOffset;
	region.dstOffset = dstOffset;
	region.size = length;

	commandBuffer.copyBufferToBuffer(
		m_buffer, other->m_buffer,
		{ region }
	);

	vkutil::endSingleTimeTransferCommands(commandBuffer);
}

void GPUBuffer::writeToTextureSingle(const Texture *texture, uint64_t size, uint64_t offset, uint32_t baseArrayLayer)
{
	CommandBuffer commandBuffer = vkutil::beginSingleTimeCommands(g_vkCore->getTransferCommandPool());
	writeToTexture(commandBuffer, texture, size, offset, baseArrayLayer);
	vkutil::endSingleTimeTransferCommands(commandBuffer);
}

void GPUBuffer::writeToTexture(CommandBuffer &commandBuffer, const Texture *texture, uint64_t size, uint64_t offset, uint32_t baseArrayLayer)
{
	VkBufferImageCopy region = {};
	region.bufferOffset = offset;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = baseArrayLayer;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { texture->getWidth(), texture->getHeight(), 1 };

	commandBuffer.copyBufferToImage(
		m_buffer, texture->getImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		{ region }
	);
}

VkDescriptorBufferInfo GPUBuffer::getDescriptorInfo(uint32_t offset) const
{
	return {
		.buffer = m_buffer,
		.offset = offset,
		.range = m_size
	};
}

VkDescriptorBufferInfo GPUBuffer::getDescriptorInfoRange(uint32_t range, uint32_t offset) const
{
	return {
		.buffer = m_buffer,
		.offset = offset,
		.range = range
	};
}

VkBuffer GPUBuffer::getHandle() const
{
	return m_buffer;
}

VkBufferUsageFlags GPUBuffer::getUsage() const
{
	return m_usage;
}

VmaMemoryUsage GPUBuffer::getMemoryUsage() const
{
	return m_memoryUsage;
}

uint64_t GPUBuffer::getSize() const
{
	return m_size;
}

const BindlessResourceHandle &GPUBuffer::getBindlessHandle() const
{
	return m_bindlessHandle;
}
