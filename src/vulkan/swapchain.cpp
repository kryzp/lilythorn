#include "swapchain.h"

#include "util.h"
#include "core.h"

#include "core/platform.h"

#include "math/colour.h"

using namespace llt;

Swapchain::Swapchain()
	: GenericRenderTarget()
    , m_swapChain(VK_NULL_HANDLE)
    , m_swapChainImages()
    , m_swapChainImageViews()
    , m_surface(VK_NULL_HANDLE)
    , m_currSwapChainImageIdx(0)
	, m_colour()
	, m_depth()
	, m_renderFinishedSemaphores()
	, m_imageAvailableSemaphores()
{
	m_type = RENDER_TARGET_TYPE_SWAPCHAIN;
}

Swapchain::~Swapchain()
{
	cleanUp();
}

void Swapchain::finalise()
{
	createSwapChain();
	acquireNextImage();
}

void Swapchain::createSurface()
{
    if (bool result = g_platform->vkCreateSurface(g_vkCore->m_instance, &m_surface); !result) {
        LLT_ERROR("Failed to create surface: %d", result);
    }
}

void Swapchain::createColourResources()
{
	m_colour.setSize(m_width, m_height);
	m_colour.setProperties(g_vkCore->m_swapChainImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_VIEW_TYPE_2D);
	m_colour.setSampleCount(g_vkCore->m_maxMsaaSamples);
	m_colour.setTransient(false);
	m_colour.createInternalResources();

	m_colour.transitionLayoutSingle(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_renderInfo.addColourAttachmentWithResolve(
		VK_ATTACHMENT_LOAD_OP_CLEAR,
		m_colour.getStandardView(),
		getCurrentSwapchainImageView()
	);

	LLT_LOG("Created colour resources!");
}

void Swapchain::createDepthResources()
{
    VkFormat format = vkutil::findDepthFormat(g_vkCore->m_physicalData.device);

	m_depth.setSize(m_width, m_height);
	m_depth.setProperties(format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_VIEW_TYPE_2D);
	m_depth.setSampleCount(g_vkCore->m_maxMsaaSamples);
	m_depth.createInternalResources();

	m_depth.transitionLayoutSingle(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

	m_renderInfo.addDepthAttachment(VK_ATTACHMENT_LOAD_OP_CLEAR, m_depth.getStandardView());

    LLT_LOG("Created depth resources!");
}

void Swapchain::beginRendering(CommandBuffer &cmd)
{
	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	
	barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	barrier.image = getCurrentSwapchainImage();
	
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	cmd.pipelineBarrier(
		0,
		{}, {}, { barrier }
	);

	m_colour.transitionLayout(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	m_depth.transitionLayout(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	m_renderInfo.getColourAttachment(0).resolveImageView = getCurrentSwapchainImageView().getHandle();
}

void Swapchain::endRendering(CommandBuffer &cmd)
{
	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

	barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	
	barrier.image = getCurrentSwapchainImage();
	
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	cmd.pipelineBarrier(
		0,
		{}, {}, { barrier }
	);

	m_colour.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_depth.transitionLayout(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

void Swapchain::cleanUp()
{
	// if our surface exists, destroy it
	if (m_surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(g_vkCore->m_instance, m_surface, nullptr);
		m_surface = VK_NULL_HANDLE;
	}
}

void Swapchain::cleanUpTextures()
{
	m_depth.cleanUp();
	m_colour.cleanUp();
}

void Swapchain::cleanUpSwapChain()
{
	cleanUpTextures();

	// destroy all of our semaphores
	for (int i = 0; i < mgc::FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(g_vkCore->m_device, m_renderFinishedSemaphores[i], nullptr);
		vkDestroySemaphore(g_vkCore->m_device, m_imageAvailableSemaphores[i], nullptr);
	}

	m_renderFinishedSemaphores.clear();
	m_imageAvailableSemaphores.clear();

	// destroy all image views for our swapchain
    for (auto &view : m_swapChainImageViews) {
        vkDestroyImageView(g_vkCore->m_device, view, nullptr);
    }

	// finally destroy the actual swapchain
    vkDestroySwapchainKHR(g_vkCore->m_device, m_swapChain, nullptr);
}

void Swapchain::acquireNextImage()
{
	// try to get the next image
	// if it is deemed out of date then rebuild the swap chain
	// otherwise this is an unknown issue and throw an error

	VkAcquireNextImageInfoKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR;
	info.swapchain = m_swapChain;
	info.timeout = UINT64_MAX;
	info.semaphore = getImageAvailableSemaphoreSubmitInfo().semaphore;
	info.fence = VK_NULL_HANDLE;
	info.deviceMask = 1;

	if (VkResult result = vkAcquireNextImage2KHR(g_vkCore->m_device, &info, &m_currSwapChainImageIdx); result == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuildSwapChain();
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LLT_ERROR("Failed to acquire next image in swap chain: %d", result);
    }
}

void Swapchain::swapBuffers()
{
	VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo = getRenderFinishedSemaphoreSubmitInfo();

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphoreSubmitInfo.semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_currSwapChainImageIdx;
    presentInfo.pResults = nullptr;

	// queue a new present info
	// if it returns as being out of date or suboptimal
	// rebuild the swap chain
    if (VkResult result = vkQueuePresentKHR(g_vkCore->m_graphicsQueue.getQueue(), &presentInfo); result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		rebuildSwapChain();
	} else if (result != VK_SUCCESS) {
        LLT_ERROR("Failed to present swap chain image: %d", result);
    }
}

Texture *Swapchain::getAttachment(int idx)
{
	return &m_colour;
}

Texture *Swapchain::getDepthAttachment()
{
	return &m_depth;
}

VkImage Swapchain::getCurrentSwapchainImage() const
{
	return m_swapChainImages[m_currSwapChainImageIdx];
}

TextureView Swapchain::getCurrentSwapchainImageView() const
{
	return TextureView(m_swapChainImageViews[m_currSwapChainImageIdx], g_vkCore->m_swapChainImageFormat);
}

void Swapchain::createSwapChain()
{
    SwapChainSupportDetails details = vkutil::querySwapChainSupport(g_vkCore->m_physicalData.device, m_surface);

	// get the surface settings
    auto surfaceFormat = vkutil::chooseSwapSurfaceFormat(details.surfaceFormats);
    auto presentMode = vkutil::chooseSwapPresentMode(details.presentModes, true); // temporary, we just enable vsync regardless of config
    auto extent = vkutil::chooseSwapExtent(details.capabilities);

	// make sure our image count can't go above the maximum image count
	// but is as high as possible.
    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.queueFamilyIndexCount = 0;
	createInfo.pQueueFamilyIndices = nullptr;
    createInfo.preTransform = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	// create the swapchain!
	LLT_VK_CHECK(
		vkCreateSwapchainKHR(g_vkCore->m_device, &createInfo, nullptr, &m_swapChain),
		"Failed to create swap chain"
	);

    vkGetSwapchainImagesKHR(g_vkCore->m_device, m_swapChain, &imageCount, nullptr);

    if (!imageCount) {
        LLT_ERROR("Failed to find any images in swap chain!");
    }

    m_swapChainImages.resize(imageCount);

	// now that we know for sure that we must have swap chain images (and the number of them)
	// actually get the swap chain images array
    Vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(g_vkCore->m_device, m_swapChain, &imageCount, images.data());

    for (int i = 0; i < images.size(); i++) {
        m_swapChainImages[i] = images[i];
    }

	g_vkCore->m_swapChainImageFormat = surfaceFormat.format;

	createSwapChainSyncObjects();
	createSwapChainImageViews();

	VkExtent2D ext = vkutil::chooseSwapExtent(details.capabilities);

	m_width = ext.width;
	m_height = ext.height;

	createDepthResources();
	createColourResources();

	m_renderInfo.setClearColour(0, { { 0.0f, 0.0f, 0.0f, 1.0f } });
	m_renderInfo.setClearDepth({ { 1.0f, 0 } });
	m_renderInfo.setSize(m_width, m_height);
	m_renderInfo.setMSAA(g_vkCore->m_maxMsaaSamples);

    LLT_LOG("Created the swap chain!");
}

void Swapchain::createSwapChainSyncObjects()
{
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	// go through all our frames in flight and create the semaphores for each frame
	for (int i = 0; i < mgc::FRAMES_IN_FLIGHT; i++)
	{
		LLT_VK_CHECK(
			vkCreateSemaphore(g_vkCore->m_device, &semaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[i]),
			"Failed to create image available semaphore"
		);

		LLT_VK_CHECK(
			vkCreateSemaphore(g_vkCore->m_device, &semaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[i]),
			"Failed to create render finished semaphore"
		);
	}

	LLT_LOG("Created swapchain sync objects!");
}

void Swapchain::createSwapChainImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());

	// create an image view for each swap chain image
    for (uint64_t i = 0; i < m_swapChainImages.size(); i++)
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapChainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = g_vkCore->m_swapChainImageFormat;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		LLT_VK_CHECK(
			vkCreateImageView(g_vkCore->m_device, &viewInfo, nullptr, &m_swapChainImageViews[i]),
			"Failed to create texture image view"
		);
    }

    LLT_LOG("Created swap chain image views!");
}

void Swapchain::onWindowResize(int width, int height)
{
	if (width == 0 || height == 0)
		return;

	if (m_width == width && m_height == height)
		return;

	m_width = width;
	m_height = height;

	// we have to rebuild the swapchain to reflect this
	// also get the next image in the queue to move away from our out-of-date current one
	rebuildSwapChain();
	acquireNextImage();
}

void Swapchain::rebuildSwapChain()
{
	g_vkCore->syncStall();

	cleanUpSwapChain();
	createSwapChain();
}

void Swapchain::setClearColour(int idx, const Colour &colour)
{
	VkClearValue value = {};
	colour.getPremultiplied().exportToFloat(value.color.float32);
	m_renderInfo.setClearColour(0, value);
}

void Swapchain::setDepthStencilClear(float depth, uint32_t stencil)
{
	VkClearValue value = {};
	value.depthStencil = { depth, stencil };
	m_renderInfo.setClearColour(1, value);
}

VkSurfaceKHR Swapchain::getSurface() const
{
	return m_surface;
}

const VkSemaphoreSubmitInfo &Swapchain::getRenderFinishedSemaphoreSubmitInfo() const
{
	VkSemaphoreSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	submitInfo.semaphore = m_renderFinishedSemaphores[g_vkCore->getCurrentFrameIdx()];

	return submitInfo;
}

const VkSemaphoreSubmitInfo &Swapchain::getImageAvailableSemaphoreSubmitInfo() const
{
	VkSemaphoreSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	submitInfo.semaphore = m_imageAvailableSemaphores[g_vkCore->getCurrentFrameIdx()];
	submitInfo.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	return submitInfo;
}
