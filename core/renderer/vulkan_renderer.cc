//
// Created by rplaz on 2023-12-14.
//

#include "vulkan_renderer.h"

#include <SDL2/SDL_vulkan.h>
#include <lib/assert.h>

namespace NycaTech::Renderer {

constexpr const char* extensions[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

#ifdef DEBUG
constexpr const char* layers[] = {
  "VK_LAYER_KHRONOS_validation",
};
#endif

VulkanRenderer::VulkanRenderer(SDL_Window* window)
{
  Assert(CreateInstance(window), "unable to create window");
  Assert(SDL_Vulkan_CreateSurface(window, instance, &surface), "unable to init surface");
  Assert(ChoosePhysicalDevice(), "unable to choose a physical device");
  Assert(CreateDevice(), "unable to create a logical Device");
  Assert(CreateSwapChain(), "unable to create swapchain");
  Assert(SetupImageViews(), "unable to create image views");
  Assert(CreateRenderPass(), "unable to create render pass");
  Assert(CreateCommandPool(), "unable to create command pool");
}

bool VulkanRenderer::AttachShader(Shader* shader)
{
  return shaders.Insert(shader);
}

bool VulkanRenderer::CreateRenderPipeline()
{
  Vector<VkPipelineShaderStageCreateInfo> shaderStages;
  for (const auto& shader : shaders) {
    VkPipelineShaderStageCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    info.stage = shader->type == Shader::Type::VERTEX ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    info.module = shader->module;
    info.pName = "main";
    info.pSpecializationInfo = nullptr;
    shaderStages.Insert(info);
  }

  auto modelBinding = ObjModel::GetVkVertexInputBindingDescription();
  auto attributeBinding = ObjModel::GetVkVertexInputAttributeDescription();

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &modelBinding;
  vertexInputInfo.vertexAttributeDescriptionCount = 1;
  vertexInputInfo.pVertexAttributeDescriptions = &attributeBinding;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.sampleShadingEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.colorWriteMask
      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  VkDynamicState                   dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
  dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(VkDynamicState);
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    return false;
  }

  VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
  info.stageCount = shaderStages.Count();
  info.pStages = shaderStages.Data();
  info.pVertexInputState = &vertexInputInfo;
  info.pInputAssemblyState = &inputAssembly;
  info.pTessellationState = nullptr;
  info.pViewportState = &viewportState;
  info.pRasterizationState = &rasterizer;
  info.pMultisampleState = &multisampling;
  info.pDepthStencilState = nullptr;
  info.pColorBlendState = &colorBlending;
  info.pDynamicState = &dynamicState;
  info.layout = pipelineLayout;
  info.renderPass = renderPass;
  info.subpass = 0;
  info.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
    return false;
  }

  return true;
}

bool VulkanRenderer::DrawFrame()
{
  vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
  Uint32 imageIndex;

  switch (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageMutex, VK_NULL_HANDLE, &imageIndex)) {
    case VK_SUBOPTIMAL_KHR:
      RecreateSwapChain();
      return true;
    case VK_SUCCESS:
      break;
    default:
      return false;
  }

  vkResetFences(device, 1, &inFlightFence);
  vkResetCommandBuffer(command, 0);
  if (!RecordCommandBuffer(command, imageIndex)) {
    return false;
  }

  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  VkSubmitInfo         info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &imageMutex;
  info.pWaitDstStageMask = waitStages;
  info.commandBufferCount = 1;
  info.pCommandBuffers = &command;
  info.signalSemaphoreCount = 1;
  info.pSignalSemaphores = &renderMutex;

  if (vkQueueSubmit(graphicsQueue, 1, &info, inFlightFence) != VK_SUCCESS) {
    return false;
  }

  VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderMutex;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchain;
  presentInfo.pImageIndices = &imageIndex;

  switch (vkQueuePresentKHR(presentQueue, &presentInfo)) {
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR:
      CreateSwapChain();
    case VK_SUCCESS:
      currentFrame = (currentFrame + 1) % 3;
      return true;
    default:
      return false;
  }
}

bool VulkanRenderer::CreateInstance(SDL_Window* window)
{
  Uint32 sdlExtensionCount;
  if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr)) {
    return false;
  }

  Vector<const char*> names(sdlExtensionCount);
  if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, names.Data())) {
    return false;
  }

  VkApplicationInfo appInfo{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "NycaTech Demo",
    .applicationVersion = VK_MAKE_VERSION(0, 2, 0),
    .pEngineName = "NycaTech",
    .apiVersion = VK_API_VERSION_1_3,
  };

  VkInstanceCreateInfo createInfo{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &appInfo,
#ifdef DEBUG
    .enabledLayerCount = sizeof(layers) / sizeof(const char*),
    .ppEnabledLayerNames = layers,
#else
    .enabledLayerCount = 0 .ppEnabledLayerNames = nullptr,
#endif
    .enabledExtensionCount = names.Count(),
    .ppEnabledExtensionNames = names.Data(),
  };

  return vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS;
}

bool VulkanRenderer::ChoosePhysicalDevice()
{
  auto hasValidExtensions = [](VkPhysicalDevice device) {
    Uint32 extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    Vector<VkExtensionProperties> deviceExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, deviceExtensions.Data());
    for (auto requiredExtension : extensions) {
      if (!deviceExtensions.Contains([&requiredExtension](const VkExtensionProperties& other) {
            return strcmp(other.extensionName, requiredExtension) == 0;
          })) {
        return false;
      }
    }
    return true;
  };
  auto canPresent = [&](VkPhysicalDevice device, Uint32 index) {
    VkBool32 present;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
    return present;
  };
  auto isDiscreteGPU = [](VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  };
  auto hasGeometryShader = [](VkPhysicalDevice device) {
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(device, &features);
    return features.geometryShader || features.fillModeNonSolid;
  };
  auto hasAValidSwapChain = [](VkPhysicalDevice device, VkSurfaceKHR surface) {
    Uint32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    Vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, formats.Data());
    Uint32 presentCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount, nullptr);
    Vector<VkPresentModeKHR> modes(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount, modes.Data());
    return formats.Count() > 0 && modes.Count() > 0;
  };

  Uint32 pdCount;
  vkEnumeratePhysicalDevices(instance, &pdCount, nullptr);
  Vector<VkPhysicalDevice> devices(pdCount);
  vkEnumeratePhysicalDevices(instance, &pdCount, devices.Data());

  for (Uint32 i = 0; i < pdCount; i++) {
    physicalDevice = devices[i];
    if (isDiscreteGPU(physicalDevice) && hasGeometryShader(physicalDevice) && canPresent(physicalDevice, i)
        && hasValidExtensions(physicalDevice) && hasAValidSwapChain(physicalDevice, surface)) {
      return true;
    }
  }
  return false;
}

bool VulkanRenderer::CreateDevice()
{
  if (!SuitableQueuesFound()) {
    return false;
  }

  VkPhysicalDeviceFeatures        dFeatures{ .fillModeNonSolid = VK_TRUE };
  Float32                         queuePriority = 1.0f;
  Vector<VkDeviceQueueCreateInfo> infos;
  VkDeviceQueueCreateInfo         queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
  queueInfo.queueFamilyIndex = graphicsQueueIndex, queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &queuePriority;
  infos.Insert(queueInfo);
  if (graphicsQueueIndex != presentQueueIndex) {
    VkDeviceQueueCreateInfo queue2Info{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queue2Info.queueFamilyIndex = presentQueueIndex;
    queue2Info.queueCount = 1;
    queue2Info.pQueuePriorities = &queuePriority;
    infos.Insert(queue2Info);
  }

  VkDeviceCreateInfo dcInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  dcInfo.queueCreateInfoCount = infos.Count();
  dcInfo.pQueueCreateInfos = infos.Data();
  dcInfo.enabledLayerCount = 0;
  dcInfo.ppEnabledLayerNames = nullptr;
  dcInfo.enabledExtensionCount = sizeof(extensions) / sizeof(const char*);
  dcInfo.ppEnabledExtensionNames = extensions;
  dcInfo.pEnabledFeatures = &dFeatures;

  if (vkCreateDevice(physicalDevice, &dcInfo, nullptr, &device) != VK_SUCCESS) {
    return false;
  }

  vkGetDeviceQueue(device, graphicsQueueIndex, 0, &graphicsQueue);
  vkGetDeviceQueue(device, presentQueueIndex, 0, &presentQueue);

  return true;
}

bool VulkanRenderer::SuitableQueuesFound()
{
  Uint32 qfCount;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
  Vector<VkQueueFamilyProperties> familyProperties(qfCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, familyProperties.Data());

  bool graphicsQueueFound = false, presentationQueueFound = false;
  for (Int32 i = 0; i < qfCount; i++) {
    VkBool32 canPresent;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &canPresent);

    if (familyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && canPresent) {
      graphicsQueueIndex = i;
      presentQueueIndex = i;
      return true;
    }

    if (familyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      graphicsQueueFound = true;
      graphicsQueueIndex = i;
      continue;
    }

    if (canPresent) {
      presentationQueueFound = true;
      presentQueueIndex = i;
    }
  }
  return graphicsQueueFound && presentationQueueFound;
}

bool VulkanRenderer::CreateSwapChain()
{
  Uint32 formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
  Vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.Data());

  Uint32 presentCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentCount, nullptr);
  Vector<VkPresentModeKHR> modes(presentCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentCount, modes.Data());

  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

  auto swapChainSurfaceFormat = [&formats]() -> VkSurfaceFormatKHR {
    for (const auto& format : formats) {
      if (format.format == VK_FORMAT_B8G8R8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }
    return formats[0];
  };
  auto swapChainPresentMode = [&modes]() -> VkPresentModeKHR {
    for (const auto& mode : modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return mode;
      }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
  };
  auto swapChanExtent = [&capabilities]() -> VkExtent2D {
    if (capabilities.currentExtent.width != UINT32_MAX) {
      return capabilities.currentExtent;
    }
    return VkExtent2D{ 1600, 900 };
  };

  auto surfaceFormat = swapChainSurfaceFormat();
  auto surfaceExtent = swapChanExtent();

  Uint32                   indices[] = { presentQueueIndex, graphicsQueueIndex };
  VkSwapchainCreateInfoKHR info{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext = nullptr,
    .flags = 0,
    .surface = surface,
    .minImageCount = capabilities.minImageCount + 1,
    .imageFormat = surfaceFormat.format,
    .imageColorSpace = surfaceFormat.colorSpace,
    .imageExtent = surfaceExtent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode
    = (graphicsQueueIndex == presentQueueIndex) ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
    .queueFamilyIndexCount = (graphicsQueueIndex == presentQueueIndex) ? 0u : 2u,
    .pQueueFamilyIndices = (graphicsQueueIndex == presentQueueIndex) ? nullptr : indices,
    .preTransform = capabilities.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = swapChainPresentMode(),
    .clipped = true,
    .oldSwapchain = nullptr,
  };

  if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS) {
    return false;
  }

  imageFormat = surfaceFormat.format;
  extent = surfaceExtent;
  Uint32 imageCount;
  vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
  queuedFrames.Resize(imageCount);
  queuedFrames.OverrideCount(imageCount);

  return vkGetSwapchainImagesKHR(device, swapchain, &imageCount, queuedFrames.Data()) == VK_SUCCESS;
}

bool VulkanRenderer::SetupImageViews()
{
  queuedFrameViews.Resize(queuedFrames.Count());
  queuedFrameViews.OverrideCount(queuedFrames.Count());
  for (Uint32 i = 0; i < queuedFrames.Count(); i++) {
    VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = queuedFrames[i];
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = imageFormat;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &info, nullptr, &queuedFrameViews[i]) != VK_SUCCESS) {
      return false;
    }
  }
  return true;
}

bool VulkanRenderer::CreateFrameBuffers()
{
  frameBuffer.Resize(queuedFrameViews.Count());
  frameBuffer.OverrideCount(queuedFrameViews.Count());
  for (Uint32 i = 0; i < queuedFrameViews.Count(); i++) {
    VkFramebufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    bufferInfo.renderPass = renderPass;
    bufferInfo.attachmentCount = 1;
    bufferInfo.pAttachments = &queuedFrameViews[i];
    bufferInfo.width = extent.width;
    bufferInfo.height = extent.height;
#ifdef DEBUG
    bufferInfo.layers = 1;
#endif
    if (vkCreateFramebuffer(device, &bufferInfo, nullptr, &frameBuffer[i]) != VK_SUCCESS) {
      return false;
    }
  }
  return true;
}

bool VulkanRenderer::CreateSynch()
{
  VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  VkFenceCreateInfo     fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

  return vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) == VK_SUCCESS
         && vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageMutex) == VK_SUCCESS
         && vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderMutex) == VK_SUCCESS;
}

bool VulkanRenderer::CreateCommandPool()
{
  VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = graphicsQueueIndex;

  return vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS;
}

bool VulkanRenderer::CreateCommandBuffers()
{
  VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  return vkAllocateCommandBuffers(device, &allocInfo, &command) == VK_SUCCESS;
}

bool VulkanRenderer::RecreateSwapChain()
{
  return vkDeviceWaitIdle(device) && CreateSwapChain() && SetupImageViews() && CreateFrameBuffers();
}

bool VulkanRenderer::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
  VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkBufferCopy copy{ 0, 0, size };
  vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copy);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    return false;
  }

  VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
    return false;
  }

  if (vkQueueWaitIdle(graphicsQueue) != VK_SUCCESS) {
    return false;
  }

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

  return true;
}

VkBuffer VulkanRenderer::CreateBuffer(VkDeviceSize          size,
                                      VkBufferUsageFlags    usage,
                                      VkMemoryPropertyFlags properties,
                                      VkDeviceMemory&       bufferMemory)
{
  VkBuffer           buffer;
  VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    return nullptr;
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  Uint32 index = 0;
  for (Uint32 i = 0; i < memProperties.memoryTypeCount; i++) {
    const bool isMemoryBits = memRequirements.memoryTypeBits & (1 << i);
    const bool hasRequiredProperties = (memProperties.memoryTypes[i].propertyFlags & properties) == properties;
    if (isMemoryBits && hasRequiredProperties) {
      index = i;
      break;
    }
  }
  if (!index) {
    return nullptr;
  }

  VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = index;

  if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
    return nullptr;
  }

  if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS) {
    return nullptr;
  }

  return buffer;
}
bool VulkanRenderer::CreateVertexBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size, void* data)
{
  VkDeviceMemory bufferMemory;
  VkBuffer       stageBuffer = CreateBuffer(size,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      bufferMemory);
  if (!stageBuffer) {
    return false;
  }
  void* dst;
  if (vkMapMemory(device, bufferMemory, 0, size, 0, &dst) != VK_SUCCESS) {
    return false;
  }
  memcpy(dst, data, size);
  vkUnmapMemory(device, bufferMemory);
  buffer = CreateBuffer(size,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        memory);
  if (!buffer) {
    return false;
  }
  if (!CopyBuffer(stageBuffer, buffer, size)) {
    return false;
  }
  vkDestroyBuffer(device, stageBuffer, nullptr);
  vkFreeMemory(device, bufferMemory, nullptr);
  return true;
}

bool VulkanRenderer::CreateIndexBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size, void* data)
{
  VkDeviceMemory bufferMemory;
  VkBuffer       stageBuffer = CreateBuffer(size,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      bufferMemory);
  if (!stageBuffer) {
    return false;
  }
  void* dst;
  if (vkMapMemory(device, bufferMemory, 0, size, 0, &dst) != VK_SUCCESS) {
    return false;
  }
  memcpy(dst, data, size);
  vkUnmapMemory(device, bufferMemory);
  buffer = CreateBuffer(size,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        memory);
  if (!buffer) {
    return false;
  }
  if (!CopyBuffer(stageBuffer, buffer, size)) {
    return false;
  }
  vkDestroyBuffer(device, stageBuffer, nullptr);
  vkFreeMemory(device, bufferMemory, nullptr);
  return true;
}

bool VulkanRenderer::RecordCommandBuffer(VkCommandBuffer command, Uint32 imageIndex)
{
  VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  if (vkBeginCommandBuffer(command, &beginInfo) != VK_SUCCESS) {
    return false;
  }

  VkClearValue          clearColor{ { { 0.0f, 0.0f, 0.0f, 1.0f } } };
  VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = frameBuffer[imageIndex];
  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = extent;
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(command, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkViewport viewport{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.f, 1.f };
  vkCmdSetViewport(command, 0, 1, &viewport);

  VkRect2D scissor{ { 0, 0 }, extent };
  vkCmdSetScissor(command, 0, 1, &scissor);

  VkDeviceSize offsets[] = { 0 };
  for (const auto& model : models) {
    vkCmdBindVertexBuffers(command, 0, 1, &model->vertexBuffer, offsets);
    vkCmdBindIndexBuffer(command, model->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command, model->indices.Count(), 1, 0, 0, 0);
  }

  vkCmdEndRenderPass(command);

  return vkEndCommandBuffer(command) == VK_SUCCESS;
}

bool VulkanRenderer::CreateRenderPass()
{
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = imageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  return vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) == VK_SUCCESS;
}

bool VulkanRenderer::LoadModel(ObjModel* model)
{
  const auto& vertices = model->vertices;
  const auto  vertSize = sizeof(vertices[0]) * vertices.Count();
  if (!CreateVertexBuffer(model->vertexBuffer, model->vertexMemory, vertSize, (void*)vertices.Data())) {
    return false;
  }

  const auto& indices = model->indices;
  const auto  indexSize = sizeof(indices[0]) * indices.Count();
  if (!CreateIndexBuffer(model->indexBuffer, model->indexMemory, indexSize, (void*)indices.Data())) {
    return false;
  }
  return models.Insert(model);
}

bool VulkanRenderer::PrepareRendering()
{
  return CreateRenderPipeline() && CreateFrameBuffers() && CreateCommandBuffers() && CreateSynch();
}

}  // namespace NycaTech::Renderer