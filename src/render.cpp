#include "render.h"

#include "parts/render_style.h"
#include "resources/model_render.h"
#include "resources/font_loader.h"
#include "resources/texture_loader.h"
#include "parts/core.h"
#include "parts/swapchain.h"
#include "parts/command.h"
#include "parts/descriptors.h"
#include "pipeline.h"
#include "pipeline_data.h"
#include "vkhelper.h"
#include "logger.h"

#include <GLFW/glfw3.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <vector>

#include <graphics/glm_helper.h>

///TODO Notes:
// * graphics pipelines assume each descriptor set they contain has the same number of sets (for frames).

namespace vkenv {

bool Render::LoadVulkan()
{
    if(volkInitialize() != VK_SUCCESS) {
      return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return true;
}

void checkVolk() {
    if(volkGetInstanceVersion() == 0) {
	// if user hasn't checked for vulkan support, try loading vulkan first.
	if(!Render::LoadVulkan())
	    throw std::runtime_error("Vulkan has not been loaded! Either the"
				     " graphics devices does not support vulkan, "
				     "or Vulkan drivers aren't installed");		    
    }
 }

  VkFormat getDepthBufferFormat(VkPhysicalDevice physicalDevice) {
    return vkhelper::findSupportedFormat(
	    physicalDevice,
	    {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
	    VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}


Render::Render(GLFWwindow *window, RenderConfig renderConf)
{
    checkVolk();
    this->renderConf = renderConf;
    this->prevRenderConf = renderConf;
    EnabledFeatures features;
    features.sampleRateShading = renderConf.sample_shading;
    manager = new VulkanManager(window, features);
    offscreenDepthFormat = getDepthBufferFormat(manager->deviceState.physicalDevice);
    
    frames = new Frame*[frameCount];
    for(int i = 0; i < frameCount; i++)
	frames[i] = new Frame(manager->deviceState.device,
			      manager->deviceState.queue.graphicsPresentFamilyIndex);
    _initStagingResourceManagers();
}

void Render::_initStagingResourceManagers() {
    _stagingModelLoader = new Resource::ModelRender(manager->deviceState,
						    manager->generalCommandPool);
    _stagingTextureLoader = new Resource::TextureLoader(manager->deviceState,
							manager->generalCommandPool,
							renderConf);
    _stagingFontLoader = new Resource::FontLoader();
    _stagingTextureLoader->LoadTexture("textures/error.png");
}
  
Render::~Render()
{
  vkDeviceWaitIdle(manager->deviceState.device);

  delete _textureLoader;
  delete _stagingTextureLoader;
  delete _modelLoader;
  delete _stagingModelLoader;
  delete _fontLoader;
  delete _stagingFontLoader;
  _destroyFrameResources();
  if(offscreenRenderPass != nullptr || finalRenderPass != nullptr) {
      delete offscreenRenderPass;
      delete finalRenderPass;
      vkFreeMemory(manager->deviceState.device, framebufferMemory, VK_NULL_HANDLE);
  }
  if(offscreenSamplerCreated)
      vkDestroySampler(manager->deviceState.device, _offscreenTextureSampler, nullptr);
  if(textureSamplerCreated)
      vkDestroySampler(manager->deviceState.device, textureSampler, nullptr);
  if(swapchain != nullptr)
      delete swapchain;
  for(int i = 0; i < frameCount; i++)
      delete frames[i];
  delete[] frames;
  delete manager;
}

bool swapchainRecreationRequired(VkResult result) {
    return result == VK_SUBOPTIMAL_KHR ||
	result == VK_ERROR_OUT_OF_DATE_KHR;
}

void Render::_initFrameResources() {
    LOG("Creating Swapchain");
	    
    int winWidth, winHeight;
    winWidth = winHeight = 0;
    glfwGetFramebufferSize(manager->window, &winWidth, &winHeight);
    while(winWidth == 0 || winHeight == 0) {
	LOG("here");
	glfwGetFramebufferSize(manager->window, &winWidth, &winHeight);
	glfwWaitEvents();
    }
    VkExtent2D offscreenBufferExtent = {(uint32_t)winWidth, (uint32_t)winHeight};
    if (renderConf.target_resolution[0] != 0.0 && renderConf.target_resolution[1] != 0.0)
	offscreenBufferExtent = {(uint32_t)renderConf.target_resolution[0],
				 (uint32_t)renderConf.target_resolution[1]};
    VkExtent2D swapchainExtent = {(uint32_t)winWidth, (uint32_t)winHeight};
      
    if(swapchain == nullptr)
	swapchain = new Swapchain(
		manager->deviceState.device,
		manager->deviceState.physicalDevice,
		manager->windowSurface, swapchainExtent, renderConf);
    else
	swapchain->RecreateSwapchain(swapchainExtent, renderConf);

    LOG("Creating Render Passes");

    VkFormat swapchainFormat = swapchain->getFormat();
    VkSampleCountFlagBits sampleCount = vkhelper::getMaxSupportedMsaaSamples(
	    manager->deviceState.device,
	    manager->deviceState.physicalDevice);
    if(!renderConf.multisampling)
	sampleCount = VK_SAMPLE_COUNT_1_BIT;

    if(swapchainFormat != prevSwapchainFormat || sampleCount != prevSampleCount) {
	if(offscreenRenderPass != nullptr) {
	    LOG("not nullptr");
	    delete offscreenRenderPass;
	    delete finalRenderPass;
	}
	std::vector<AttachmentDesc> offscreenAttachments;
	if(renderConf.multisampling) {
	    offscreenAttachments.push_back(AttachmentDesc(0, AttachmentType::Colour,
							  AttachmentUse::TransientAttachment,
							  sampleCount, swapchainFormat));
	    offscreenAttachments.push_back(AttachmentDesc(2, AttachmentType::Resolve,
							  AttachmentUse::ShaderRead,
							  VK_SAMPLE_COUNT_1_BIT, swapchainFormat));
	}
	else
	    offscreenAttachments.push_back(AttachmentDesc(0, AttachmentType::Colour,
							  AttachmentUse::ShaderRead,
							  VK_SAMPLE_COUNT_1_BIT, swapchainFormat));
	offscreenAttachments.push_back(AttachmentDesc(1, AttachmentType::Depth,
						      AttachmentUse::Attachment,
						      sampleCount, offscreenDepthFormat));

	LOG("make new renderpasses");
	offscreenRenderPass = new RenderPass(manager->deviceState.device, offscreenAttachments,
					     renderConf.clear_colour);
        finalRenderPass =
            new RenderPass(manager->deviceState.device,
                           {AttachmentDesc(0, AttachmentType::Colour,
                                           AttachmentUse::PresentSrc,

                                           VK_SAMPLE_COUNT_1_BIT, swapchainFormat)},
	    renderConf.scaled_border_colour);
    }
      
    prevSwapchainFormat = swapchainFormat;
    prevSampleCount = sampleCount;

    std::vector<VkImage>* swapchainImages = swapchain->getSwapchainImages();
    swapchainFrameCount = swapchainImages->size();

    LOG("Creating Framebuffers");

    //TODO: less unnessecary recreation (ie offscreen extent not changing?)
    VkDeviceSize attachmentMemorySize = 0;
    uint32_t attachmentMemoryFlags = 0;
    offscreenRenderPass->createFramebufferImages(swapchainImages, offscreenBufferExtent,
						 &attachmentMemorySize,
						 &attachmentMemoryFlags);

    finalRenderPass->createFramebufferImages(swapchainImages, swapchainExtent,
					     &attachmentMemorySize,
					     &attachmentMemoryFlags);
    
    vkFreeMemory(manager->deviceState.device, framebufferMemory, VK_NULL_HANDLE);
    checkResultAndThrow(
	    vkhelper::allocateMemory(manager->deviceState.device,
				     manager->deviceState.physicalDevice,
				     attachmentMemorySize,
				     &framebufferMemory,
				     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				     attachmentMemoryFlags),
	    "Render Error: Failed to Allocate Memory for Framebuffer Images");

    offscreenRenderPass->createFramebuffers(framebufferMemory);
    finalRenderPass->createFramebuffers(framebufferMemory);

    LOG("Swapchain Image Count: " << swapchainImages->size());
      
      
    LOG("Creating Descriptor Sets");
      
    /// set shader  descripor sets


    //TODO: more recreation here, use max-frame-in-flight instead of swapchain count
      
    /// vertex descripor sets
    descriptor::Descriptor viewProjectionBinding(
	    "view projection struct",
	    descriptor::Type::UniformBuffer,
	    sizeof(shaderStructs::viewProjection), 1);
    descriptor::Descriptor timeBinding(
	    "Time Struct", descriptor::Type::UniformBuffer,
	    sizeof(shaderStructs::timeUbo), 1);
    descriptor::Set VP3D_Set("VP3D", descriptor::ShaderStage::Vertex);
    VP3D_Set.AddDescriptor(viewProjectionBinding);
    VP3D_Set.AddDescriptor(timeBinding);
    VP3D = new DescSet(VP3D_Set, swapchainFrameCount, manager->deviceState.device);

    descriptor::Set VP2D_Set("VP2D", descriptor::ShaderStage::Vertex);
    VP2D_Set.AddDescriptor(viewProjectionBinding);
    VP2D = new DescSet(VP2D_Set, swapchainFrameCount, manager->deviceState.device);

    descriptor::Set Time_Set("Time", descriptor::ShaderStage::Vertex);
    Time_Set.AddDescriptor("Time Struct", descriptor::Type::UniformBuffer,
			   sizeof(shaderStructs::timeUbo), 1);
      

    descriptor::Set PerFrame3D_Set("Per Frame 3D", descriptor::ShaderStage::Vertex);
    PerFrame3D_Set.AddSingleArrayStructDescriptor("3D Instance Array",
						  descriptor::Type::StorageBuffer,
						  sizeof(shaderStructs::PerFrame3D),
						  MAX_3D_INSTANCE);
    perFrame3D = new DescSet(PerFrame3D_Set, swapchainFrameCount, manager->deviceState.device);
      

    descriptor::Set bones_Set("Bones Animation", descriptor::ShaderStage::Vertex);
    bones_Set.AddDescriptor("bones", descriptor::Type::UniformBufferDynamic,
			    sizeof(shaderStructs::Bones), MAX_ANIMATIONS_PER_FRAME);
    bones = new DescSet(bones_Set, swapchainFrameCount, manager->deviceState.device);

    descriptor::Set vert2D_Set("Per Frame 2D Vert", descriptor::ShaderStage::Vertex);
    vert2D_Set.AddSingleArrayStructDescriptor("vert struct",
					      descriptor::Type::StorageBuffer,
					      sizeof(glm::mat4), MAX_2D_INSTANCE);
    perFrame2DVert = new DescSet(vert2D_Set, swapchainFrameCount, manager->deviceState.device);

    descriptor::Set offscreenView_Set("Offscreen Transform", descriptor::ShaderStage::Vertex);
    offscreenView_Set.AddDescriptor("data", descriptor::Type::UniformBuffer,
				    sizeof(glm::mat4), 1);
    offscreenTransform = new DescSet(offscreenView_Set, swapchainFrameCount, manager->deviceState.device);

    // fragment descriptor sets

    descriptor::Set lighting_Set("3D Lighting", descriptor::ShaderStage::Fragment);
    lighting_Set.AddDescriptor("Lighting properties", descriptor::Type::UniformBuffer,
			       sizeof(shaderStructs::Lighting), 1);
    lighting = new DescSet(lighting_Set, swapchainFrameCount, manager->deviceState.device);


    float minMipmapLevel = _textureLoader->getMinMipmapLevel();
    
    if(textureSamplerCreated) {
	if(prevRenderConf.texture_filter_nearest != renderConf.texture_filter_nearest ||
	   prevTexSamplerMinMipmap != minMipmapLevel) {
	    textureSamplerCreated = false;
	    vkDestroySampler(manager->deviceState.device, textureSampler, nullptr);
	}
    }
    
    if(!textureSamplerCreated) {	  
	textureSampler = vkhelper::createTextureSampler(
		manager->deviceState.device,
		manager->deviceState.physicalDevice,
		minMipmapLevel,
		manager->deviceState.features.samplerAnisotropy,
		renderConf.texture_filter_nearest,
		VK_SAMPLER_ADDRESS_MODE_REPEAT);
	prevTexSamplerMinMipmap = minMipmapLevel;
	textureSamplerCreated = true;
    }

    for(int i = 0; i < Resource::MAX_TEXTURES_SUPPORTED; i++) {
	if(i < _textureLoader->getImageCount())
	    textureViews[i] = _textureLoader->getImageView(i);
	else
	    textureViews[i] = _textureLoader->getImageView(0);
    }

    descriptor::Set texture_Set("textures", descriptor::ShaderStage::Fragment);
    texture_Set.AddSamplerDescriptor("sampler", 1, &textureSampler);
    texture_Set.AddImageViewDescriptor("views", descriptor::Type::SampledImage,
				       Resource::MAX_TEXTURES_SUPPORTED,
				       textureViews);
    textures = new DescSet(texture_Set, swapchainFrameCount, manager->deviceState.device);
      
    descriptor::Set frag2D_Set("Per Frame 2D frag", descriptor::ShaderStage::Fragment);
    frag2D_Set.AddSingleArrayStructDescriptor(
	    "Per frag struct",
	    descriptor::Type::StorageBuffer,
	    sizeof(shaderStructs::Frag2DData), MAX_2D_INSTANCE);
    perFrame2DFrag = new DescSet(frag2D_Set, swapchainFrameCount, manager->deviceState.device);

    emptyDS = new DescSet(
	    descriptor::Set("Empty", descriptor::ShaderStage::Vertex),
	    swapchainFrameCount, manager->deviceState.device);
      
    if(!offscreenSamplerCreated) {	  
	_offscreenTextureSampler = vkhelper::createTextureSampler(
		manager->deviceState.device,
		manager->deviceState.physicalDevice, 1.0f,
		false,
		true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
	offscreenSamplerCreated = true;
    }
    std::vector<VkImageView> offscreenViews = offscreenRenderPass->getAttachmentViews(
	    renderConf.multisampling ? 2 : 0);
    descriptor::Set offscreen_Set("offscreen texture", descriptor::ShaderStage::Fragment);
    offscreen_Set.AddSamplerDescriptor("sampler", 1, &_offscreenTextureSampler);
    offscreen_Set.AddImageViewDescriptor("frame", descriptor::Type::SampledImagePerSet,
					 1, offscreenViews.data());
    offscreenTex = new DescSet(offscreen_Set, swapchainFrameCount,
			       manager->deviceState.device); 

    descriptorSets = {
	VP3D, VP2D, perFrame3D, bones, emptyDS, perFrame2DVert,
	perFrame2DFrag, offscreenTransform, lighting,
	textures, offscreenTex};
      
    LOG("Creating Descriptor pool and memory for set bindings");
      
    // create descripor pool

    std::vector<DS::DescriptorSet* > sets(descriptorSets.size());
    std::vector<DS::Binding*> bindings;
    for(int i = 0; i < sets.size(); i++) {
	sets[i] = &descriptorSets[i]->set;
	for(int j = 0; j < descriptorSets[i]->bindings.size(); j++)
	    bindings.push_back(&descriptorSets[i]->bindings[j]);
    }
      
    part::create::DescriptorPoolAndSet(
	    manager->deviceState.device, &_descPool, sets, swapchainFrameCount);
      
    // create memory mapped buffer for all descriptor set bindings
    part::create::PrepareShaderBufferSets(
	    manager->deviceState, bindings,
	    &_shaderBuffer, &_shaderMemory);

    LOG("Creating Graphics Pipelines");

    // create pipeline for each shader set -> 3D, animated 3D, 2D, and final
    part::create::GraphicsPipeline(
	    manager->deviceState.device, &_pipeline3D,
	    sampleCount, offscreenRenderPass->getRenderPass(),
	    {&VP3D->set, &perFrame3D->set, &emptyDS->set, &textures->set, &lighting->set},
	    {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fragPushConstants)}},
	    "shaders/vulkan/3D-lighting.vert.spv", "shaders/vulkan/blinnphong.frag.spv", true,
	    renderConf.multisampling, true,
	    manager->deviceState.features.sampleRateShading, offscreenBufferExtent,
	    VK_CULL_MODE_BACK_BIT, pipeline_inputs::V3D::attributeDescriptions(),
	    pipeline_inputs::V3D::bindingDescriptions());
	    
    part::create::GraphicsPipeline(
	    manager->deviceState.device, &_pipelineAnim3D,
	    sampleCount, offscreenRenderPass->getRenderPass(),
	    {&VP3D->set, &perFrame3D->set, &bones->set, &textures->set, &lighting->set},
	    {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fragPushConstants)}},
	    "shaders/vulkan/3D-lighting-anim.vert.spv", "shaders/vulkan/blinnphong.frag.spv",
	    true, renderConf.multisampling, true,
	    manager->deviceState.features.sampleRateShading, offscreenBufferExtent,
	    VK_CULL_MODE_BACK_BIT, pipeline_inputs::VAnim3D::attributeDescriptions(),
	    pipeline_inputs::VAnim3D::bindingDescriptions());

    part::create::GraphicsPipeline(
	    manager->deviceState.device, &_pipeline2D, sampleCount,
	    offscreenRenderPass->getRenderPass(),
	    {&VP2D->set, &perFrame2DVert->set, &textures->set, &perFrame2DFrag->set}, {},
	    "shaders/vulkan/flat.vert.spv", "shaders/vulkan/flat.frag.spv", true,
	    renderConf.multisampling, true,
	    manager->deviceState.features.sampleRateShading, offscreenBufferExtent,
	    VK_CULL_MODE_BACK_BIT, pipeline_inputs::V2D::attributeDescriptions(),
	    pipeline_inputs::V2D::bindingDescriptions());

    part::create::GraphicsPipeline(                     //TODO: originally max samples, still works?
	    manager->deviceState.device, &_pipelineFinal, VK_SAMPLE_COUNT_1_BIT,
	    finalRenderPass->getRenderPass(),
	    {&offscreenTransform->set, &offscreenTex->set}, {},
	    "shaders/vulkan/final.vert.spv", "shaders/vulkan/final.frag.spv",
	    false, false, false, manager->deviceState.features.sampleRateShading,
	    swapchainExtent, VK_CULL_MODE_NONE, {}, {});

    offscreenTransformData = glmhelper::calcFinalOffset(
	    glm::vec2(offscreenBufferExtent.width, offscreenBufferExtent.height),
	    glm::vec2((float)swapchainExtent.width,
		      (float)swapchainExtent.height));
    LOG("Finished Creating Frame Resources");
    timeData.time = 0;
    prevRenderConf = renderConf;
}

void Render::_destroyFrameResources()
{
  LOG("Destroying frame resources");
  vkDestroyBuffer(manager->deviceState.device, _shaderBuffer, nullptr);
  vkFreeMemory(manager->deviceState.device, _shaderMemory, nullptr);
  for(int i = 0; i < descriptorSets.size(); i++)
      delete descriptorSets[i];
  descriptorSets.clear();
  vkDestroyDescriptorPool(manager->deviceState.device, _descPool, nullptr);

  _pipeline3D.destroy(manager->deviceState.device);
  _pipelineAnim3D.destroy(manager->deviceState.device);
  _pipeline2D.destroy(manager->deviceState.device);
  _pipelineFinal.destroy(manager->deviceState.device);
}

Resource::Texture Render::LoadTexture(std::string filepath) {
  return _stagingTextureLoader->LoadTexture(filepath);
}

Resource::Font Render::LoadFont(std::string filepath) {
    try {
      return _stagingFontLoader->LoadFont(filepath, _stagingTextureLoader);
    } catch (const std::exception &e) {
	std::cout << e.what() << std::endl;
	return Resource::Font();
    }
}

Resource::Model Render::LoadAnimatedModel(
	std::string filepath,
	std::vector<Resource::ModelAnimation> *pGetAnimations) {
    return _stagingModelLoader->loadModel(Resource::ModelType::m3D_Anim, filepath, _stagingTextureLoader, pGetAnimations);
}

Resource::Model Render::LoadAnimatedModel(ModelInfo::Model& model,
					  std::vector<Resource::ModelAnimation> *pGetAnimation) {
    return _stagingModelLoader->loadModel(Resource::ModelType::m3D_Anim, model, _stagingTextureLoader, pGetAnimation);
}

Resource::Model Render::Load2DModel(std::string filepath) {
    return _stagingModelLoader->loadModel(Resource::ModelType::m2D, filepath, _stagingTextureLoader, nullptr);
}

Resource::Model Render::Load2DModel(ModelInfo::Model& model) {
    return _stagingModelLoader->loadModel(Resource::ModelType::m2D, model, _stagingTextureLoader, nullptr);
}

Resource::Model Render::Load3DModel(std::string filepath) {
    return _stagingModelLoader->loadModel(Resource::ModelType::m3D, filepath, _stagingTextureLoader, nullptr);
}

Resource::Model Render::Load3DModel(ModelInfo::Model& model) {
    return _stagingModelLoader->loadModel(Resource::ModelType::m3D, model, _stagingTextureLoader, nullptr);
}

void Render::LoadResourcesToGPU() {
    _stagingTextureLoader->endLoading();
    _stagingModelLoader->endLoading(manager->generalCommandBuffer);
}

void Render::UseLoadedResources() {
    vkDeviceWaitIdle(manager->deviceState.device);
    if(_textureLoader != nullptr)
	_destroyFrameResources();
    delete _textureLoader;
    _textureLoader = _stagingTextureLoader;
    delete _modelLoader;
    _modelLoader = _stagingModelLoader;
    delete _fontLoader;
    _fontLoader = _stagingFontLoader;
    _initStagingResourceManagers();
    _initFrameResources();
}

void Render::_resize() {
    LOG("resizing");
    _framebufferResized = false;
    vkDeviceWaitIdle(manager->deviceState.device);
    
    _destroyFrameResources();
    _initFrameResources();
    _update3DProjectionMatrix();
}

void Render::_startDraw() {
    frameIndex = (frameIndex + 1) % frameCount;
    checkResultAndThrow(frames[frameIndex]->waitForPreviousFrame(),
			"Render Error: failed to wait for previous frame fence");
    VkResult result = swapchain->acquireNextImage(frames[frameIndex]->swapchainImageReady,
						 &swapchainFrameIndex);
    if(result != VK_SUCCESS && !swapchainRecreationRequired(result))
	    checkResultAndThrow(result, "Render Error: failed to begin offscreen render pass!");
    checkResultAndThrow(frames[frameIndex]->startFrame(&currentCommandBuffer),
			"Render Error: Failed to start command buffer.");
    offscreenRenderPass->beginRenderPass(currentCommandBuffer, swapchainFrameIndex);
    _modelLoader->bindBuffers(currentCommandBuffer);
    currentBonesDynamicOffset = 0;
    _begunDraw = true;
}

void Render::Begin3DDraw() {
    if (!_begunDraw)
	_startDraw();
    if (_modelRuns > 0)
	_drawBatch();
    if (_instance2Druns > 0)
	_drawBatch();
    _renderState = RenderState::Draw3D;
    
    VP3D->bindings[0].storeSetData(swapchainFrameIndex, &VP3DData, 0, 0, 0);
    VP3D->bindings[1].storeSetData(swapchainFrameIndex, &timeData, 0, 0, 0);
    lighting->bindings[0].storeSetData(swapchainFrameIndex, &lightingData, 0, 0, 0);
    
    _pipeline3D.begin(currentCommandBuffer, swapchainFrameIndex);
}

void Render::DrawModel(Resource::Model model, glm::mat4 modelMatrix, glm::mat4 normalMat) {
    DrawModel(model, modelMatrix, normalMat, glm::vec4(0.0f));
}


void Render::DrawModel(Resource::Model model, glm::mat4 modelMatrix, glm::mat4 normalMat,
		       glm::vec4 colour) {
    if (_current3DInstanceIndex >= MAX_3D_INSTANCE) {
	LOG("WARNING: ran out of 3D instances!\n");
	return;
    }
    
    if ((_currentColour != colour || _currentModel.ID != model.ID) && _modelRuns != 0)
	_drawBatch();
    
    _currentModel = model;
    _currentColour = colour;
    perFrame3DData[_current3DInstanceIndex + _modelRuns].model = modelMatrix;
    perFrame3DData[_current3DInstanceIndex + _modelRuns].normalMat = normalMat;
    _modelRuns++;
    
    if (_current3DInstanceIndex + _modelRuns == MAX_3D_INSTANCE)
	_drawBatch();
}

void Render::BeginAnim3DDraw()
{
  if (!_begunDraw)
    _startDraw();  VP3D->bindings[1].storeSetData(swapchainFrameIndex, &timeData, 0, 0, 0);
  if (_modelRuns > 0)
    _drawBatch();
  if (_instance2Druns > 0)
    _drawBatch();
  _renderState = RenderState::DrawAnim3D;

  VP3D->bindings[0].storeSetData(swapchainFrameIndex, &VP3DData, 0, 0, 0);
  VP3D->bindings[1].storeSetData(swapchainFrameIndex, &timeData, 0, 0, 0);
  lighting->bindings[0].storeSetData(swapchainFrameIndex, &lightingData, 0, 0, 0);
  _pipelineAnim3D.begin(currentCommandBuffer, swapchainFrameIndex);
}

void Render::DrawAnimModel(Resource::Model model, glm::mat4 modelMatrix,
			   glm::mat4 normalMat, Resource::ModelAnimation *animation) {
    if (_current3DInstanceIndex >= MAX_3D_INSTANCE) {
	LOG("WARNING: Ran out of 3D Anim Instance models!\n");
	return;
    }
    
    if (_currentModel.ID != model.ID && _modelRuns != 0)
	_drawBatch();

    _currentModel = model;
    _currentColour = glm::vec4(0.0f);
    perFrame3DData[_current3DInstanceIndex + _modelRuns].model = modelMatrix;
    perFrame3DData[_current3DInstanceIndex + _modelRuns].normalMat = normalMat;
    _modelRuns++;

    auto animBones = animation->getCurrentBones();
    shaderStructs::Bones bonesData;
    for(int i = 0; i < animBones->size() && i < shaderStructs::MAX_BONES; i++) {
	bonesData.mat[i] = animBones->at(i);
    }
    if(currentBonesDynamicOffset >= MAX_ANIMATIONS_PER_FRAME) {
	LOG("warning, too many animation calls!\n");
	return;
    }
    bones->bindings[0].storeSetData(swapchainFrameIndex,
				    &bonesData, 0, 0, currentBonesDynamicOffset);
    uint32_t offset = static_cast<uint32_t>((currentBonesDynamicOffset) *
					    bones->bindings[0].bufferSize *
					    bones->bindings[0].setCount);
    _pipelineAnim3D.bindDynamicDS(currentCommandBuffer, &bones->set, swapchainFrameIndex,  offset);
    _drawBatch();
    currentBonesDynamicOffset++;
}

void Render::Begin2DDraw()
{
  if (!_begunDraw)
    _startDraw();
  if (_modelRuns > 0)
    _drawBatch();
  if (_instance2Druns > 0)
    _drawBatch();
  _renderState = RenderState::Draw2D;

  VP2DData.proj = glm::ortho(
      0.0f, (float)offscreenRenderPass->getExtent().width*_scale2D, 0.0f,
      (float)offscreenRenderPass->getExtent().height*_scale2D,
      renderConf.depth_range_2D[0], renderConf.depth_range_2D[1]);
  VP2DData.view = glm::mat4(1.0f);

  VP2D->bindings[0].storeSetData(swapchainFrameIndex, &VP2DData, 0, 0, 0);

  _pipeline2D.begin(currentCommandBuffer, swapchainFrameIndex);
}

void Render::DrawQuad(Resource::Texture texture, glm::mat4 modelMatrix, glm::vec4 colour, glm::vec4 texOffset)
{
  if (_current2DInstanceIndex >= MAX_2D_INSTANCE) {
      LOG("WARNING: ran out of 2D instance models!\n");
      return;
  }
   perFrame2DVertData[_current2DInstanceIndex + _instance2Druns] = modelMatrix;
   perFrame2DFragData[_current2DInstanceIndex + _instance2Druns].colour = colour;
   perFrame2DFragData[_current2DInstanceIndex + _instance2Druns].texOffset = texOffset;
   perFrame2DFragData[_current2DInstanceIndex + _instance2Druns].texID = (uint32_t)texture.ID;
  _instance2Druns++;

  if (_current2DInstanceIndex + _instance2Druns == MAX_2D_INSTANCE)
    _drawBatch();
}

void Render::DrawQuad(Resource::Texture texture, glm::mat4 modelMatrix, glm::vec4 colour)
{
  DrawQuad(texture, modelMatrix, colour, glm::vec4(0, 0, 1, 1));
}

void Render::DrawQuad(Resource::Texture texture, glm::mat4 modelMatrix) {
  DrawQuad(texture, modelMatrix, glm::vec4(1), glm::vec4(0, 0, 1, 1));
}

void Render::DrawString(Resource::Font font, std::string text, glm::vec2 position, float size, float depth, glm::vec4 colour, float rotate)
{
  auto draws = _fontLoader->DrawString(font, text, position, size, depth, colour, rotate);
  for (const auto &draw : draws) {
    DrawQuad(draw.tex, draw.model, draw.colour, draw.texOffset);
  }
}
void Render::DrawString(Resource::Font font, std::string text,
			glm::vec2 position, float size, float depth, glm::vec4 colour) {
  DrawString(font, text, position, size, depth, colour, 0.0);
}

float Render::MeasureString(Resource::Font font, std::string text, float size)
{
  return _fontLoader->MeasureString(font, text, size);
}

void Render::_drawBatch() {
    switch(_renderState) {
    case RenderState::DrawAnim3D:
    case RenderState::Draw3D:
	_modelLoader->drawModel(currentCommandBuffer,
				_pipeline3D.layout, _currentModel, _modelRuns,
				_current3DInstanceIndex, _currentColour);
	_current3DInstanceIndex += _modelRuns;
	_modelRuns = 0;
	break;
    case RenderState::Draw2D:
	_modelLoader->drawQuad(currentCommandBuffer,
			       _pipeline3D.layout, 0, _instance2Druns,
			       _current2DInstanceIndex, _currentColour,
			       _currentTexOffset);
	_current2DInstanceIndex += _instance2Druns;
	_instance2Druns = 0;
	break;
    }
}


  VkResult submitDraw(VkQueue queue, Frame *frame) {
      VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submitInfo.waitSemaphoreCount = 1;
      submitInfo.pWaitSemaphores = &frame->swapchainImageReady;
      VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      submitInfo.pWaitDstStageMask = &stageFlags;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &frame->commandBuffer;
      submitInfo.signalSemaphoreCount = 1;
      submitInfo.pSignalSemaphores = &frame->drawFinished;
      VkResult result = vkQueueSubmit(queue, 1, &submitInfo, frame->frameFinished);
      if(result != VK_SUCCESS)
	  LOG_ERR_TYPE("Render Error: Failed to sumbit draw commands.", result);
      return result;
  }

  VkResult submitPresent(VkSemaphore* waitSemaphore, VkSwapchainKHR *swapchain,
			 uint32_t* swapchainImageIndex, VkQueue queue) {
    VkPresentInfoKHR presentInfo {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchain;
    presentInfo.pImageIndices = swapchainImageIndex;
    VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	  LOG_ERR_TYPE("Render Error: Failed to sumbit draw commands.", result);
    return result;
  }

void Render::EndDraw(std::atomic<bool> &submit) {
  if (!_begunDraw)
    throw std::runtime_error("Tried to end draw before starting it");

  _begunDraw = false;

  switch(_renderState)
  {
    case RenderState::Draw3D:
    case RenderState::DrawAnim3D:
      if (_modelRuns != 0 && _current3DInstanceIndex < MAX_3D_INSTANCE)
        _drawBatch();
      break;
    case RenderState::Draw2D:
      if (_instance2Druns != 0 && _current2DInstanceIndex < MAX_2D_INSTANCE)
        _drawBatch();
      break;
  }

  for (size_t i = 0; i < _current3DInstanceIndex; i++)
      perFrame3D->bindings[0].storeSetData(swapchainFrameIndex, &perFrame3DData[i], 0, i, 0);
  _current3DInstanceIndex = 0;

  for (size_t i = 0; i < _current2DInstanceIndex; i++) {
      perFrame2DVert->bindings[0].storeSetData(swapchainFrameIndex, &perFrame2DVertData[i], 0, i, 0);
      perFrame2DFrag->bindings[0].storeSetData(swapchainFrameIndex, &perFrame2DFragData[i], 0, i, 0);	  
  }
  _current2DInstanceIndex = 0;

  //FINAL RENDER  PASS

  vkCmdEndRenderPass(currentCommandBuffer);

  finalRenderPass->beginRenderPass(currentCommandBuffer, swapchainFrameIndex);
  
  offscreenTransform->bindings[0].storeSetData(swapchainFrameIndex, &offscreenTransformData, 0, 0, 0);
  _pipelineFinal.begin(currentCommandBuffer, swapchainFrameIndex);
  vkCmdDraw(currentCommandBuffer, 3, 1, 0, 0);

  vkCmdEndRenderPass(currentCommandBuffer);
  
  VkResult result = vkEndCommandBuffer(currentCommandBuffer);
  if(result == VK_SUCCESS)
      result = submitDraw(manager->deviceState.queue.graphicsPresentQueue,
			  frames[frameIndex]);
  if(result == VK_SUCCESS) {
      VkSwapchainKHR sc = swapchain->getSwapchain();
      result = submitPresent(&frames[frameIndex]->drawFinished, &sc, &swapchainFrameIndex,
			     manager->deviceState.queue.graphicsPresentQueue);
  }
  
  if (swapchainRecreationRequired(result) || _framebufferResized) {
      LOG("end of draw, resize or recreation required");
      _resize();
  } else if (result != VK_SUCCESS)
      checkResultAndThrow(result, "failed to present swapchain image to queue");

  submit = true;
}

void Render::_update3DProjectionMatrix() {
  VP3DData.proj =
      glm::perspective(glm::radians(_projectionFov),
                       ((float)offscreenRenderPass->getExtent().width) /
                           ((float)offscreenRenderPass->getExtent().height),
                       renderConf.depth_range_3D[0], renderConf.depth_range_3D[1]);
  VP3DData.proj[1][1] *= -1; // opengl has inversed y axis, so need to correct
}

//recreates frame resources, so any state change for rendering will be updated on next draw if this is called
void Render::FramebufferResize() {
    _framebufferResized = true;
}

void Render::set3DViewMatrixAndFov(glm::mat4 view, float fov, glm::vec4 camPos) {
  VP3DData.view = view;
  _projectionFov = fov;
  lightingData.camPos = camPos;
  _update3DProjectionMatrix();
}

void Render::set2DViewMatrixAndScale(glm::mat4 view, float scale) {
  VP2DData.view = view;
  _scale2D = scale;
}

void Render::setLightDirection(glm::vec4 lightDir) {
  lightingData.direction = lightDir;
}

void Render::setRenderConf(RenderConfig renderConf) {
    this->renderConf = renderConf;
    FramebufferResize();
}

RenderConfig Render::getRenderConf() {
    return renderConf;
}

void Render::setTargetResolution(glm::vec2 resolution) {
    if(renderConf.target_resolution[0] == resolution.x &&
       renderConf.target_resolution[1] == resolution.y)
	return;
    renderConf.target_resolution[0] = resolution.x;
    renderConf.target_resolution[1] = resolution.y;
    FramebufferResize();
}

glm::vec2 Render::getTargetResolution() {
    return glm::vec2(renderConf.target_resolution[0], renderConf.target_resolution[1]);
}

}//namespace
