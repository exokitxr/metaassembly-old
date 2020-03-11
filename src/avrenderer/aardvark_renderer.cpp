#include "aardvark_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <chrono>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>
#include <aardvark/ivrmanager.h>

#if defined(__ANDROID__)
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif

#include <vulkan/vulkan.h>
#include "VulkanExampleBase.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.hpp"
#include "VulkanUtils.hpp"

// #include "include/cef_sandbox_win.h"
#include "av_cef_app.h"
// #include "av_cef_handler.h"

// When generating projects with CMake the CEF_USE_SANDBOX value will be defined
// automatically if using the required compiler version. Pass -DUSE_SANDBOX=OFF
// to the CMake command-line to disable use of the sandbox.
// Uncomment this line to manually enable sandbox support.
// #define CEF_USE_SANDBOX 1

#if defined(CEF_USE_SANDBOX)
// The cef_sandbox.lib static library may not link successfully with all VS
// versions.
#pragma comment(lib, "cef_sandbox.lib")
#endif

#include <tools/pathtools.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "matrix.h"

#include "out.h"
#include "file_io.h"

using namespace aardvark;

VulkanExample::VulkanExample()
	: VulkanExampleBase()
{
	title = "Aardvark Renderer";
#if defined(TINYGLTF_ENABLE_DRACO)
	getOut() << "Draco mesh compression is enabled" << std::endl;
#endif
}

VulkanExample::~VulkanExample() noexcept
{
	vkDestroyPipeline(device, pipelines.skybox, nullptr);
	vkDestroyPipeline(device, pipelines.pbr, nullptr);
	vkDestroyPipeline( device, pipelines.pbrDoubleSided, nullptr );
	vkDestroyPipeline( device, pipelines.pbrAlphaBlend, nullptr );
	vkDestroyPipeline(device, pipelines.pbrAlphaBlendDoubleSided, nullptr);

	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

	m_mapModels.clear();

	for (auto buffer : uniformBuffers)
	{
		buffer.params.destroy();
		buffer.scene.destroy();
		buffer.skybox.destroy();
		buffer.leftEye.destroy();
		buffer.rightEye.destroy();
	}
	for (auto fence : waitFences) {
		vkDestroyFence(device, fence, nullptr);
	}
	for (auto semaphore : renderCompleteSemaphores) {
		vkDestroySemaphore(device, semaphore, nullptr);
	}
	for (auto semaphore : presentCompleteSemaphores) {
		vkDestroySemaphore(device, semaphore, nullptr);
	}

	textures.environmentCube.destroy();
	textures.irradianceCube.destroy();
	textures.prefilteredCube.destroy();
	textures.lutBrdf.destroy();
	textures.empty.destroy();
}

void VulkanExample::renderNode( std::shared_ptr<vkglTF::Model> pModel, std::shared_ptr<vkglTF::Node> node, uint32_t cbIndex, 
	vkglTF::Material::AlphaMode alphaMode, bool doubleSided, EEye eEye )
{
	if ( node->mesh ) {
		// Render mesh primitives
		for ( auto primitive : node->mesh->primitives ) {
			vkglTF::Material & primitiveMaterial = primitive->materialIndex >= pModel->materials.size()
				? pModel->materials.back() : pModel->materials[primitive->materialIndex];
			
			if ( primitiveMaterial.alphaMode == alphaMode && primitiveMaterial.doubleSided == doubleSided ) {
				VkDescriptorSet descriptorSet;
				switch ( eEye )
				{
				case EEye::Left:
					descriptorSet = descriptorSets[cbIndex].eye[vr::Eye_Left]->set();
					break;
				case EEye::Right:
					descriptorSet = descriptorSets[cbIndex].eye[vr::Eye_Right]->set();
					break;
				default:
				case EEye::Mirror:
					descriptorSet = descriptorSets[cbIndex].scene->set();
					break;
				}

				const std::vector<VkDescriptorSet> descriptorsets =
				{
					descriptorSet,
					primitiveMaterial.descriptorSet->set(),
					node->mesh->uniformBuffer.descriptor->set(),
				};
				vkCmdBindDescriptorSets( commandBuffers[cbIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>( descriptorsets.size() ), descriptorsets.data(), 0, NULL );


				// Pass material parameters as push constants
				PushConstBlockMaterial pushConstBlockMaterial{};
				pushConstBlockMaterial.emissiveFactor = primitiveMaterial.emissiveFactor;
				// To save push constant space, availability and texture coordinate set are combined
				// -1 = texture not used for this material, >= 0 texture used and index of texture coordinate set
				pushConstBlockMaterial.colorTextureSet = primitiveMaterial.baseColorTexture != nullptr ? primitiveMaterial.texCoordSets.baseColor : -1;
				pushConstBlockMaterial.normalTextureSet = primitiveMaterial.normalTexture != nullptr ? primitiveMaterial.texCoordSets.normal : -1;
				pushConstBlockMaterial.occlusionTextureSet = primitiveMaterial.occlusionTexture != nullptr ? primitiveMaterial.texCoordSets.occlusion : -1;
				pushConstBlockMaterial.emissiveTextureSet = primitiveMaterial.emissiveTexture != nullptr ? primitiveMaterial.texCoordSets.emissive : -1;
				pushConstBlockMaterial.alphaMask = static_cast<float>( primitiveMaterial.alphaMode == vkglTF::Material::ALPHAMODE_MASK );
				pushConstBlockMaterial.alphaMaskCutoff = primitiveMaterial.alphaCutoff;

				// TODO: glTF specs states that metallic roughness should be preferred, even if specular glossiness is present

				switch ( primitiveMaterial.workflow )
				{
				case vkglTF::Material::Workflow::MetallicRoughness:
					pushConstBlockMaterial.workflow = static_cast<float>( PBR_WORKFLOW_METALLIC_ROUGHNESS );
					pushConstBlockMaterial.baseColorFactor = primitiveMaterial.baseColorFactor;
					pushConstBlockMaterial.metallicFactor = primitiveMaterial.metallicFactor;
					pushConstBlockMaterial.roughnessFactor = primitiveMaterial.roughnessFactor;
					pushConstBlockMaterial.PhysicalDescriptorTextureSet = primitiveMaterial.metallicRoughnessTexture != nullptr ? primitiveMaterial.texCoordSets.metallicRoughness : -1;
					pushConstBlockMaterial.colorTextureSet = primitiveMaterial.baseColorTexture != nullptr ? primitiveMaterial.texCoordSets.baseColor : -1;
					break;

				case vkglTF::Material::Workflow::SpecularGlossiness:
					pushConstBlockMaterial.workflow = static_cast<float>( PBR_WORKFLOW_SPECULAR_GLOSINESS );
					pushConstBlockMaterial.PhysicalDescriptorTextureSet = primitiveMaterial.extension.specularGlossinessTexture != nullptr ? primitiveMaterial.texCoordSets.specularGlossiness : -1;
					pushConstBlockMaterial.colorTextureSet = primitiveMaterial.extension.diffuseTexture != nullptr ? primitiveMaterial.texCoordSets.baseColor : -1;
					pushConstBlockMaterial.diffuseFactor = primitiveMaterial.extension.diffuseFactor;
					pushConstBlockMaterial.specularFactor = glm::vec4( primitiveMaterial.extension.specularFactor, 1.0f );
					break;

				case vkglTF::Material::Workflow::Unlit:
					pushConstBlockMaterial.workflow = static_cast<float>( PBR_WORKFLOW_UNLIT );
					pushConstBlockMaterial.baseColorFactor = primitiveMaterial.baseColorFactor;
					break;
				}

				vkCmdPushConstants( commandBuffers[cbIndex], pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( PushConstBlockMaterial ), &pushConstBlockMaterial );

				PushConstBlockVertex pushConstVertex{};
				pushConstVertex.uvScaleAndOffset =
				{
					primitiveMaterial.baseColorScale[0], primitiveMaterial.baseColorScale[1],
					primitiveMaterial.baseColorOffset[0], primitiveMaterial.baseColorOffset[1]
				};

				vkCmdPushConstants( commandBuffers[cbIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof( PushConstBlockMaterial ), sizeof( PushConstBlockVertex ), &pushConstVertex );

				if ( primitive->hasIndices ) {
					vkCmdDrawIndexed( commandBuffers[cbIndex], primitive->indexCount, 1, primitive->firstIndex, 0, 0 );
				}
				else {
					vkCmdDraw( commandBuffers[cbIndex], primitive->vertexCount, 1, 0, 0 );
				}
			}
		}

	};
	for ( auto child : node->children ) {
		renderNode( pModel, child, cbIndex, alphaMode, doubleSided, eEye );
	}
}

void VulkanExample::recordCommandBuffers( uint32_t cbIndex )
{
	VkCommandBufferBeginInfo cmdBufferBeginInfo{};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VkCommandBuffer currentCB = commandBuffers[cbIndex];

	VK_CHECK_RESULT( vkBeginCommandBuffer( currentCB, &cmdBufferBeginInfo ) );

	// renderScene( cbIndex, renderPass, frameBuffers[cbIndex], width, height, EEye::Mirror );
	renderSceneToTarget( cbIndex, leftEyeRT, eyeWidth, eyeHeight, EEye::Left );
	renderSceneToTarget( cbIndex, rightEyeRT, eyeWidth, eyeHeight, EEye::Right );
	renderVarggles( cbIndex, vargglesRT, m_unVargglesWidth, m_unVargglesHeight);

	VK_CHECK_RESULT( vkEndCommandBuffer( currentCB ) );
}

void VulkanExample::renderVarggles( uint32_t cbIndex, vks::RenderTarget target, uint32_t targetWidth, uint32_t targetHeight)
{
	VkCommandBuffer currentCB = commandBuffers[cbIndex];

	leftEyeRT.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	rightEyeRT.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	target.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );

	VkClearValue clearValues[3];
	if ( settings.multiSampling ) {
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };
	}
	else {
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };
	}

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = target.renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = targetWidth;
	renderPassBeginInfo.renderArea.extent.height = targetHeight;
	renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.framebuffer = target.frameBuffer;
	vkCmdBeginRenderPass( currentCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

	VkViewport viewport{};
	viewport.width = static_cast<float>(targetWidth);
	viewport.height = static_cast<float>(targetHeight);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( currentCB, 0, 1, &viewport );

	VkRect2D scissor{};
	scissor.extent = { targetWidth, targetHeight };
	vkCmdSetScissor( currentCB, 0, 1, &scissor );
	vkCmdBindPipeline( currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vargglesVulkanBindings.pipeline);

	VkDescriptorSet descriptorSet = m_vargglesVulkanBindings.descriptorsets[cbIndex];
	const std::vector<VkDescriptorSet> descriptorsets = { descriptorSet };
	vkCmdBindDescriptorSets( commandBuffers[cbIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_vargglesVulkanBindings.pipelinelayout, 0, static_cast<uint32_t>( descriptorsets.size() ), descriptorsets.data(), 0, NULL );

	m_vrManager->getVargglesLookRotation(m_vargglesLookRotation);

	PushConstBlockVarggles pushConstVarggles{};
	pushConstVarggles.halfFOVInRadians = (m_eyeFOV / 2.0f) * (M_PI / 180.0f);
	pushConstVarggles.lookRotation = m_vargglesLookRotation;
	vkCmdPushConstants( commandBuffers[cbIndex], m_vargglesVulkanBindings.pipelinelayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( PushConstBlockVarggles ), &pushConstVarggles);

	vkCmdDraw( commandBuffers[cbIndex], 3, 1, 0, 0 );

	vkCmdEndRenderPass( currentCB );

	target.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	leftEyeRT.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	rightEyeRT.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
}

void VulkanExample::renderSceneToTarget( uint32_t cbIndex, vks::RenderTarget target, uint32_t targetWidth, uint32_t targetHeight, EEye eEye )
{
	VkCommandBuffer currentCB = commandBuffers[cbIndex];

	target.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );

	renderScene( cbIndex, target.renderPass, target.frameBuffer, targetWidth, targetHeight, eEye );

	target.transitionColorLayout( currentCB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
}

void VulkanExample::renderScene( uint32_t cbIndex, VkRenderPass targetRenderPass, VkFramebuffer targetFrameBuffer, uint32_t targetWidth, uint32_t targetHeight, EEye eEye )
{

	VkClearValue clearValues[3];
	if ( settings.multiSampling ) {
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };
	}
	else {
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };
	}

	VkRenderPassBeginInfo renderPassBeginInfo{};
	VkCommandBuffer currentCB = commandBuffers[cbIndex];

	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = targetRenderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = targetWidth;
	renderPassBeginInfo.renderArea.extent.height = targetHeight;
	renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.framebuffer = targetFrameBuffer;

	vkCmdBeginRenderPass( currentCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

	VkViewport viewport{};
	viewport.width = (float)targetWidth;
	viewport.height = (float)targetHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( currentCB, 0, 1, &viewport );

	VkRect2D scissor{};
	scissor.extent = { targetWidth, targetHeight };
	vkCmdSetScissor( currentCB, 0, 1, &scissor );

	//if (displayBackground) {
	//	vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[i].skybox, 0, nullptr);
	//	vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
	//	models.skybox.draw(currentCB);
	//}

	vkCmdBindPipeline( currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbr );

	recordCommandsForModels( currentCB, cbIndex, vkglTF::Material::ALPHAMODE_OPAQUE, false, eEye );
	recordCommandsForModels( currentCB, cbIndex, vkglTF::Material::ALPHAMODE_MASK, false, eEye );

	vkCmdBindPipeline( currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrDoubleSided );

	recordCommandsForModels( currentCB, cbIndex, vkglTF::Material::ALPHAMODE_OPAQUE, true, eEye );
	recordCommandsForModels( currentCB, cbIndex, vkglTF::Material::ALPHAMODE_MASK, true, eEye );

	// Transparent primitives
	// TODO: Correct depth sorting
	vkCmdBindPipeline( currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlend );
	recordCommandsForModels( currentCB, cbIndex, vkglTF::Material::ALPHAMODE_BLEND, false, eEye );

	vkCmdBindPipeline( currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlendDoubleSided );
	recordCommandsForModels( currentCB, cbIndex, vkglTF::Material::ALPHAMODE_BLEND, true, eEye );

	vkCmdEndRenderPass( currentCB );
}

void VulkanExample::recordCommandsForModels( VkCommandBuffer currentCB, uint32_t i, vkglTF::Material::AlphaMode eAlphaMode, 
	bool bDoubleSided, EEye eEye )
{
	for ( auto pModel : m_modelsToRender )
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers( currentCB, 0, 1, &pModel->m_model->buffers->vertices.buffer, offsets );
		if ( pModel->m_model->buffers->indices.buffer != VK_NULL_HANDLE )
		{
			vkCmdBindIndexBuffer( currentCB, pModel->m_model->buffers->indices.buffer, 0, VK_INDEX_TYPE_UINT32 );
		}

		for ( auto node : pModel->m_model->nodes ) {
			renderNode( pModel->m_model, node, i, eAlphaMode, bDoubleSided, eEye );
		}
	}
}

void VulkanExample::loadEnvironment( std::string filename )
{
	getOut() << "Loading environment from " << filename << std::endl;
	if ( textures.environmentCube.image ) {
		textures.environmentCube.destroy();
		textures.irradianceCube.destroy();
		textures.prefilteredCube.destroy();
	}
	textures.environmentCube.loadFromFile( filename, VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue );
	generateCubemaps();
}

void VulkanExample::loadAssets()
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	tinygltf::asset_manager = androidApp->activity->assetManager;
	readDirectory( assetpath + "models", "*.gltf", scenes, true );
#else
	const std::string assetpath = tools::GetDataPath().generic_string() + "/";
	struct stat info;
	if ( stat( assetpath.c_str(), &info ) != 0 ) {
		std::string msg = "Could not locate asset path in \"" + assetpath + "\".\nMake sure binary is run from correct relative directory!";
		getOut() << msg << std::endl;
		exit( -1 );
	}
#endif
	readDirectory( assetpath + "environments", "*.ktx", environments, false );

	textures.empty.loadFromFile( assetpath + "textures/empty.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue );

	std::string sceneFile = assetpath + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
	std::string envMapFile = assetpath + "environments/softboxes_hdr16f_cube.ktx";
	for ( size_t i = 0; i < args.size(); i++ ) {
		if ( std::string( args[i] ).find( ".gltf" ) != std::string::npos ) {
			std::ifstream file( args[i] );
			if ( file.good() ) {
				sceneFile = args[i];
			}
			else {
				getOut() << "could not load \"" << args[i] << "\"" << std::endl;
			}
		}
		if ( std::string( args[i] ).find( ".ktx" ) != std::string::npos ) {
			std::ifstream file( args[i] );
			if ( file.good() ) {
				envMapFile = args[i];
			}
			else {
				getOut() << "could not load \"" << args[i] << "\"" << std::endl;
			}
		}
	}

	//loadScene(sceneFile.c_str());
	//models.skybox.loadFromFile(assetpath + "models/Box/glTF-Embedded/Box.gltf", vulkanDevice, queue);

	if ( !m_skybox.loadFromFile( assetpath + "models/Box/glTF-Embedded/Box.gltf", vulkanDevice, m_descriptorManager, queue ) )
	{
		getOut() << "Couldn't load skybox. Bailing.";
	}

	loadEnvironment( envMapFile.c_str() );

}

void VulkanExample::UpdateDescriptorForScene( VkDescriptorSet descriptorSet, 
	VkBuffer buffer, uint32_t bufferSize,
	VkBuffer paramsBuffer, uint32_t paramsBufferSize
)
{
	VkDescriptorBufferInfo bufferInfo = { buffer, 0, bufferSize };
	VkDescriptorBufferInfo paramBufferInfo = { paramsBuffer, 0, paramsBufferSize };

	std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};

	writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writeDescriptorSets[0].descriptorCount = 1;
	writeDescriptorSets[0].dstSet = descriptorSet;
	writeDescriptorSets[0].dstBinding = 0;
	writeDescriptorSets[0].pBufferInfo = &bufferInfo;

	writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writeDescriptorSets[1].descriptorCount = 1;
	writeDescriptorSets[1].dstSet = descriptorSet;
	writeDescriptorSets[1].dstBinding = 1;
	writeDescriptorSets[1].pBufferInfo = &paramBufferInfo;

	writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writeDescriptorSets[2].descriptorCount = 1;
	writeDescriptorSets[2].dstSet = descriptorSet;
	writeDescriptorSets[2].dstBinding = 2;
	writeDescriptorSets[2].pImageInfo = &textures.irradianceCube.descriptor;

	writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writeDescriptorSets[3].descriptorCount = 1;
	writeDescriptorSets[3].dstSet = descriptorSet;
	writeDescriptorSets[3].dstBinding = 3;
	writeDescriptorSets[3].pImageInfo = &textures.prefilteredCube.descriptor;

	writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writeDescriptorSets[4].descriptorCount = 1;
	writeDescriptorSets[4].dstSet = descriptorSet;
	writeDescriptorSets[4].dstBinding = 4;
	writeDescriptorSets[4].pImageInfo = &textures.lutBrdf.descriptor;

	vkUpdateDescriptorSets( device, static_cast<uint32_t>( writeDescriptorSets.size() ), writeDescriptorSets.data(), 0, NULL );
}

void VulkanExample::setupDescriptors()
{
	/*
		Descriptor sets
	*/

	// Scene (matrices and environment maps)
	{

		for ( auto i = 0; i < descriptorSets.size(); i++ )
		{
			auto fnUpdateDescriptor =
				[this, i]( vks::VulkanDevice *vulkanDevice, vks::CDescriptorSet *descriptor )
			{
				UpdateDescriptorForScene( descriptor->set(),
					uniformBuffers[i].scene.buffer,
					uniformBuffers[i].scene.size,
					uniformBuffers[i].params.buffer,
					uniformBuffers[i].params.size );
			};

			descriptorSets[i].scene = m_descriptorManager->createDescriptorSet( fnUpdateDescriptor, vks::EDescriptorLayout::Scene );

			auto fnUpdateDescriptorLeftEye =
				[this, i]( vks::VulkanDevice *vulkanDevice, vks::CDescriptorSet *descriptor )
			{
				UpdateDescriptorForScene( descriptor->set(),
					uniformBuffers[i].leftEye.buffer,
					uniformBuffers[i].leftEye.size,
					uniformBuffers[i].params.buffer,
					uniformBuffers[i].params.size );
			};
			descriptorSets[i].eye[vr::Eye_Left] = m_descriptorManager->createDescriptorSet( fnUpdateDescriptorLeftEye, vks::EDescriptorLayout::Scene );

			auto fnUpdateDescriptorRightEye =
				[this, i]( vks::VulkanDevice *vulkanDevice, vks::CDescriptorSet *descriptor )
			{
				UpdateDescriptorForScene( descriptor->set(),
					uniformBuffers[i].rightEye.buffer,
					uniformBuffers[i].rightEye.size,
					uniformBuffers[i].params.buffer,
					uniformBuffers[i].params.size );
			};
			descriptorSets[i].eye[vr::Eye_Right] = m_descriptorManager->createDescriptorSet( fnUpdateDescriptorRightEye, vks::EDescriptorLayout::Scene );
		}
	}

	// Material (samplers)
	// Per-Material descriptor sets
	for ( auto iModel : m_mapModels )
	{
		setupDescriptorSetsForModel( iModel.second );
	}

	// Skybox (fixed set)
	for ( auto i = 0; i < uniformBuffers.size(); i++ )
	{
		descriptorSets[i].skybox = m_descriptorManager->createDescriptorSet(
			[this, i]( vks::VulkanDevice *vulkanDevice, vks::CDescriptorSet *desc )
		{
			std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = desc->set();
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].skybox.descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = desc->set();
			writeDescriptorSets[1].dstBinding = 1;
			writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;

			writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[2].descriptorCount = 1;
			writeDescriptorSets[2].dstSet = desc->set();
			writeDescriptorSets[2].dstBinding = 2;
			writeDescriptorSets[2].pImageInfo = &textures.prefilteredCube.descriptor;

			vkUpdateDescriptorSets( device, static_cast<uint32_t>( writeDescriptorSets.size() ), writeDescriptorSets.data(), 0, nullptr );
		}, vks::EDescriptorLayout::Scene );
	}
}

void VulkanExample::setupDescriptorSetsForModel( std::shared_ptr<vkglTF::Model> pModel )
{
	for ( auto &material : pModel->materials )
	{
		material.descriptorSet = m_descriptorManager->createDescriptorSet( 
			[this, material]( vks::VulkanDevice *vulkanDevice, vks::CDescriptorSet *desc )
		{
			std::vector<VkDescriptorImageInfo> imageDescriptors =
			{
				textures.empty.descriptor,
				textures.empty.descriptor,
				material.normalTexture ? material.normalTexture->descriptor : textures.empty.descriptor,
				material.occlusionTexture ? material.occlusionTexture->descriptor : textures.empty.descriptor,
				material.emissiveTexture ? material.emissiveTexture->descriptor : textures.empty.descriptor
			};

			switch ( material.workflow )
			{
			case vkglTF::Material::Workflow::MetallicRoughness:
				// TODO: glTF specs states that metallic roughness should be preferred, even if specular glossiness is present
				if ( material.baseColorTexture )
				{
					imageDescriptors[0] = material.baseColorTexture->descriptor;
				}
				if ( material.metallicRoughnessTexture )
				{
					imageDescriptors[1] = material.metallicRoughnessTexture->descriptor;
				}
				break;

			case vkglTF::Material::Workflow::SpecularGlossiness:
				if ( material.extension.diffuseTexture )
				{
					imageDescriptors[0] = material.extension.diffuseTexture->descriptor;
				}
				if ( material.extension.specularGlossinessTexture )
				{
					imageDescriptors[1] = material.extension.specularGlossinessTexture->descriptor;
				}
				break;

			case vkglTF::Material::Workflow::Unlit:
				if ( material.baseColorTexture )
				{
					imageDescriptors[0] = material.baseColorTexture->descriptor;
				}
				break;
			}

			std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
			for ( size_t i = 0; i < imageDescriptors.size(); i++ )
			{
				writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[i].descriptorCount = 1;
				writeDescriptorSets[i].dstSet = desc->set();
				writeDescriptorSets[i].dstBinding = static_cast<uint32_t>( i );
				writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
			}

			vkUpdateDescriptorSets( device, static_cast<uint32_t>( writeDescriptorSets.size() ), writeDescriptorSets.data(), 0, NULL );
		}, vks::EDescriptorLayout::Material );
	}
}

void VulkanExample::preparePipelines()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
	inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
	rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCI.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState blendAttachmentState{};
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
	depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCI.depthTestEnable = VK_FALSE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;
	depthStencilStateCI.front = depthStencilStateCI.back;

	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
	multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

	if ( settings.multiSampling ) {
		multisampleStateCI.rasterizationSamples = settings.sampleCount;
	}

	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicStateCI{};
	dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
	dynamicStateCI.dynamicStateCount = static_cast<uint32_t>( dynamicStateEnables.size() );

	// Pipeline layout
	const std::vector<VkDescriptorSetLayout> setLayouts =
	{
		m_descriptorManager->getLayout( vks::EDescriptorLayout::Scene ),
		m_descriptorManager->getLayout( vks::EDescriptorLayout::Material ),
		m_descriptorManager->getLayout( vks::EDescriptorLayout::Node ),
	};

	std::array<VkPushConstantRange, 2> arrayConstantRanges{};
	arrayConstantRanges[0].size = sizeof( PushConstBlockMaterial );
	arrayConstantRanges[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	arrayConstantRanges[1].size = sizeof( PushConstBlockVertex );
	arrayConstantRanges[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	arrayConstantRanges[1].offset = sizeof( PushConstBlockMaterial );

	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>( setLayouts.size() );
	pipelineLayoutCI.pSetLayouts = setLayouts.data();
	pipelineLayoutCI.pushConstantRangeCount = (uint32_t)arrayConstantRanges.size();
	pipelineLayoutCI.pPushConstantRanges = arrayConstantRanges.data();
	VK_CHECK_RESULT( vkCreatePipelineLayout( device, &pipelineLayoutCI, nullptr, &pipelineLayout ) );

	// Vertex bindings an attributes
	VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof( vkglTF::Model::Vertex ), VK_VERTEX_INPUT_RATE_VERTEX };
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
		{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof( float ) * 3 },
		{ 2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof( float ) * 6 },
		{ 3, 0, VK_FORMAT_R32G32_SFLOAT, sizeof( float ) * 8 },
		{ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof( float ) * 10 },
		{ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof( float ) * 14 }
	};
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
	vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCI.vertexBindingDescriptionCount = 1;
	vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
	vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>( vertexInputAttributes.size() );
	vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

	// Pipelines
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.layout = pipelineLayout;
	pipelineCI.renderPass = renderPass;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pVertexInputState = &vertexInputStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>( shaderStages.size() );
	pipelineCI.pStages = shaderStages.data();

	if ( settings.multiSampling ) {
		multisampleStateCI.rasterizationSamples = settings.sampleCount;
	}

	// Skybox pipeline (background cube)
	shaderStages = {
		loadShader( device, "skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT ),
		loadShader( device, "skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT )
	};
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox ) );
	for ( auto shaderStage : shaderStages ) {
		vkDestroyShaderModule( device, shaderStage.module, nullptr );
	}

	// PBR pipeline
	shaderStages = {
		loadShader( device, "pbr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT ),
		loadShader( device, "pbr_khr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT )
	};
	depthStencilStateCI.depthWriteEnable = VK_TRUE;
	depthStencilStateCI.depthTestEnable = VK_TRUE;
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbr ) );

	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrDoubleSided ) );

	rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
	blendAttachmentState.blendEnable = VK_TRUE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrAlphaBlend ) );

	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrAlphaBlendDoubleSided ) );

	for ( auto shaderStage : shaderStages ) {
		vkDestroyShaderModule( device, shaderStage.module, nullptr );
	}
}

void VulkanExample::prepareVarggles()
{
	m_vargglesEyePerspectiveProjection = glm::perspective(m_eyeFOV, 1.0f, 0.1f, 256.0f);

	// create left / right eye render target color image samplers
	VkSamplerCreateInfo samplerCI{};
	samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCI.magFilter = VK_FILTER_LINEAR;
	samplerCI.minFilter = VK_FILTER_LINEAR;
	samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.minLod = 0.0f;
	samplerCI.maxLod = 0.0f;
	samplerCI.maxAnisotropy = 1.0f;
	samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &leftEyeRT.color.sampler));
	VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &rightEyeRT.color.sampler));
	leftEyeRT.color.updateDescriptor();
	rightEyeRT.color.updateDescriptor();

	// expect triangle lists
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
	inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// set polygon mode to fill culling etc
	VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
	rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCI.lineWidth = 1.0f;

	// dont do alpha blending
	VkPipelineColorBlendAttachmentState blendAttachmentState{};
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.blendEnable = VK_FALSE;

	// set up blend state
	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;

	// dont do depth tests
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
	depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCI.depthTestEnable = VK_FALSE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCI.front = depthStencilStateCI.back;
	depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

	// viewport will have 1 viewport one scissor
	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.scissorCount = 1;

	// set up multisamples
	VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
	multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

	if (settings.multiSampling) {
		multisampleStateCI.rasterizationSamples = settings.sampleCount;
	}

	// tell it to expect a new viewport / scissor each pass
	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	// hold info about all dynamic state
	VkPipelineDynamicStateCreateInfo dynamicStateCI{};
	dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
	dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

	// there will be two 2d image samplers
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
	};

	// save this as our descriptor set layout
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
	descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
	descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>( setLayoutBindings.size() );
	VK_CHECK_RESULT( vkCreateDescriptorSetLayout( device, &descriptorSetLayoutCI, nullptr, &m_vargglesVulkanBindings.descriptorsetlayout) );

	// we will need a pool of 2 image samplers
	VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
	VkDescriptorPoolCreateInfo descriptorPoolCI{};
	descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCI.poolSizeCount = 1;
	descriptorPoolCI.pPoolSizes = &poolSize;
	descriptorPoolCI.maxSets = static_cast<uint32_t>(m_vargglesVulkanBindings.descriptorsets.size());
	VK_CHECK_RESULT( vkCreateDescriptorPool( device, &descriptorPoolCI, nullptr, &m_vargglesVulkanBindings.descriptorpool ) );

	// Allocate the descriptor sets from the pool
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
	descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocInfo.descriptorPool = m_vargglesVulkanBindings.descriptorpool;
	descriptorSetAllocInfo.pSetLayouts = &m_vargglesVulkanBindings.descriptorsetlayout;
	descriptorSetAllocInfo.descriptorSetCount = 1;

	// do it for both sets for each render pass
	std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{};
	for(auto& descriptorSet : m_vargglesVulkanBindings.descriptorsets)
	{ 
		VK_CHECK_RESULT( vkAllocateDescriptorSets( device, &descriptorSetAllocInfo, &descriptorSet ) );

		writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSets[0].descriptorCount = 1;
		writeDescriptorSets[0].dstSet = descriptorSet;
		writeDescriptorSets[0].dstBinding = 0;
		writeDescriptorSets[0].pImageInfo = &leftEyeRT.color.descriptor;

		writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSets[1].descriptorCount = 1;
		writeDescriptorSets[1].dstSet = descriptorSet;
		writeDescriptorSets[1].dstBinding = 1;
		writeDescriptorSets[1].pImageInfo = &rightEyeRT.color.descriptor;

		vkUpdateDescriptorSets( device, static_cast<uint32_t>( writeDescriptorSets.size() ), writeDescriptorSets.data(), 0, NULL );
	}

	// describe size of push constants
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.size = sizeof(PushConstBlockVarggles);

	// tell vulkan about push constatns and descriptor sets
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &m_vargglesVulkanBindings.descriptorsetlayout;
	pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
	VK_CHECK_RESULT( vkCreatePipelineLayout( device, &pipelineLayoutCI, nullptr, &m_vargglesVulkanBindings.pipelinelayout ) );

	// bind nothing to vertex input
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
	vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCI.vertexBindingDescriptionCount = 0;
	vertexInputStateCI.pVertexBindingDescriptions = nullptr;
	vertexInputStateCI.vertexAttributeDescriptionCount = 0;
	vertexInputStateCI.pVertexAttributeDescriptions = nullptr;

	// load shaders and create the pipeline
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
	shaderStages[0] = loadShader(device, "varggles.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	shaderStages[1] = loadShader(device, "varggles.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	// set up pipeline ci
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.layout = m_vargglesVulkanBindings.pipelinelayout;
	pipelineCI.renderPass = vargglesRT.renderPass;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pVertexInputState = &vertexInputStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = 2;
	pipelineCI.pStages = shaderStages.data();
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &m_vargglesVulkanBindings.pipeline ) );

	// delete shaders
	for ( auto shaderStage : shaderStages ) {
		vkDestroyShaderModule( device, shaderStage.module, nullptr );
	}
}

/*
	Generate a BRDF integration map storing roughness/NdotV as a look-up-table
*/
void VulkanExample::generateBRDFLUT()
{
	auto tStart = std::chrono::high_resolution_clock::now();

	const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
	const int32_t dim = 512;

	// Image
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = format;
	imageCI.extent.width = dim;
	imageCI.extent.height = dim;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VK_CHECK_RESULT( vkCreateImage( device, &imageCI, nullptr, &textures.lutBrdf.image ) );
	// printf( "Image 0x%llX function %s\n", (size_t)textures.lutBrdf.image, __FUNCTION__ );

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements( device, textures.lutBrdf.image, &memReqs );
	VkMemoryAllocateInfo memAllocInfo{};
	memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	VK_CHECK_RESULT( vkAllocateMemory( device, &memAllocInfo, nullptr, &textures.lutBrdf.deviceMemory ) );
	VK_CHECK_RESULT( vkBindImageMemory( device, textures.lutBrdf.image, textures.lutBrdf.deviceMemory, 0 ) );

	// View
	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = format;
	viewCI.subresourceRange = {};
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.layerCount = 1;
	viewCI.image = textures.lutBrdf.image;
	VK_CHECK_RESULT( vkCreateImageView( device, &viewCI, nullptr, &textures.lutBrdf.view ) );

	// Sampler
	VkSamplerCreateInfo samplerCI{};
	samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCI.magFilter = VK_FILTER_LINEAR;
	samplerCI.minFilter = VK_FILTER_LINEAR;
	samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.minLod = 0.0f;
	samplerCI.maxLod = 1.0f;
	samplerCI.maxAnisotropy = 1.0f;
	samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT( vkCreateSampler( device, &samplerCI, nullptr, &textures.lutBrdf.sampler ) );

	// FB, Att, RP, Pipe, etc.
	VkAttachmentDescription attDesc{};
	// Color attachment
	attDesc.format = format;
	attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;

	// Use subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Create the actual renderpass
	VkRenderPassCreateInfo renderPassCI{};
	renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCI.attachmentCount = 1;
	renderPassCI.pAttachments = &attDesc;
	renderPassCI.subpassCount = 1;
	renderPassCI.pSubpasses = &subpassDescription;
	renderPassCI.dependencyCount = 2;
	renderPassCI.pDependencies = dependencies.data();

	VkRenderPass renderpass;
	VK_CHECK_RESULT( vkCreateRenderPass( device, &renderPassCI, nullptr, &renderpass ) );

	VkFramebufferCreateInfo framebufferCI{};
	framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferCI.renderPass = renderpass;
	framebufferCI.attachmentCount = 1;
	framebufferCI.pAttachments = &textures.lutBrdf.view;
	framebufferCI.width = dim;
	framebufferCI.height = dim;
	framebufferCI.layers = 1;

	VkFramebuffer framebuffer;
	VK_CHECK_RESULT( vkCreateFramebuffer( device, &framebufferCI, nullptr, &framebuffer ) );

	// Desriptors
	VkDescriptorSetLayout descriptorsetlayout;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
	descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	VK_CHECK_RESULT( vkCreateDescriptorSetLayout( device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout ) );

	// Pipeline layout
	VkPipelineLayout pipelinelayout;
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
	VK_CHECK_RESULT( vkCreatePipelineLayout( device, &pipelineLayoutCI, nullptr, &pipelinelayout ) );

	// Pipeline
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
	inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
	rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCI.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState blendAttachmentState{};
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
	depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCI.depthTestEnable = VK_FALSE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCI.front = depthStencilStateCI.back;
	depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
	multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI{};
	dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
	dynamicStateCI.dynamicStateCount = static_cast<uint32_t>( dynamicStateEnables.size() );

	VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
	emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.layout = pipelinelayout;
	pipelineCI.renderPass = renderpass;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pVertexInputState = &emptyInputStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = 2;
	pipelineCI.pStages = shaderStages.data();

	// Look-up-table (from BRDF) pipeline		
	shaderStages = {
		loadShader( device, "genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT ),
		loadShader( device, "genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT )
	};
	VkPipeline pipeline;
	VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline ) );
	for ( auto shaderStage : shaderStages ) {
		vkDestroyShaderModule( device, shaderStage.module, nullptr );
	}

	// Render
	VkClearValue clearValues[1];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = renderpass;
	renderPassBeginInfo.renderArea.extent.width = dim;
	renderPassBeginInfo.renderArea.extent.height = dim;
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.framebuffer = framebuffer;

	VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer( VK_COMMAND_BUFFER_LEVEL_PRIMARY, true );
	vkCmdBeginRenderPass( cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

	VkViewport viewport{};
	viewport.width = (float)dim;
	viewport.height = (float)dim;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.extent.width = width;
	scissor.extent.height = height;

	vkCmdSetViewport( cmdBuf, 0, 1, &viewport );
	vkCmdSetScissor( cmdBuf, 0, 1, &scissor );
	vkCmdBindPipeline( cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdDraw( cmdBuf, 3, 1, 0, 0 );
	vkCmdEndRenderPass( cmdBuf );
	vulkanDevice->flushCommandBuffer( cmdBuf, queue );

	vkQueueWaitIdle( queue );

	vkDestroyPipeline( device, pipeline, nullptr );
	vkDestroyPipelineLayout( device, pipelinelayout, nullptr );
	vkDestroyRenderPass( device, renderpass, nullptr );
	vkDestroyFramebuffer( device, framebuffer, nullptr );
	vkDestroyDescriptorSetLayout( device, descriptorsetlayout, nullptr );

	textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
	textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
	textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	textures.lutBrdf.device = vulkanDevice;

	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>( tEnd - tStart ).count();
	getOut() << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
}

/*
	Offline generation for the cube maps used for PBR lighting
	- Irradiance cube map
	- Pre-filterd environment cubemap
*/
void VulkanExample::generateCubemaps()
{
	enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

	for ( uint32_t target = 0; target < PREFILTEREDENV + 1; target++ ) {

		vks::TextureCubeMap cubemap;

		auto tStart = std::chrono::high_resolution_clock::now();

		VkFormat format;
		int32_t dim;

		switch ( target ) {
		case IRRADIANCE:
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			dim = 64;
			break;
		case PREFILTEREDENV:
			format = VK_FORMAT_R16G16B16A16_SFLOAT;
			dim = 512;
			break;
		};

		const uint32_t numMips = static_cast<uint32_t>( floor( log2( dim ) ) ) + 1;

		// Create target cubemap
		{
			// Image
			VkImageCreateInfo imageCI{};
			imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = format;
			imageCI.extent.width = dim;
			imageCI.extent.height = dim;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = numMips;
			imageCI.arrayLayers = 6;
			imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			VK_CHECK_RESULT( vkCreateImage( device, &imageCI, nullptr, &cubemap.image ) );
			// printf( "Image 0x%llX function %s\n", (size_t)cubemap.image, __FUNCTION__ );

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements( device, cubemap.image, &memReqs );
			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
			VK_CHECK_RESULT( vkAllocateMemory( device, &memAllocInfo, nullptr, &cubemap.deviceMemory ) );
			VK_CHECK_RESULT( vkBindImageMemory( device, cubemap.image, cubemap.deviceMemory, 0 ) );

			// View
			VkImageViewCreateInfo viewCI{};
			viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			viewCI.format = format;
			viewCI.subresourceRange = {};
			viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewCI.subresourceRange.levelCount = numMips;
			viewCI.subresourceRange.layerCount = 6;
			viewCI.image = cubemap.image;
			VK_CHECK_RESULT( vkCreateImageView( device, &viewCI, nullptr, &cubemap.view ) );

			// Sampler
			VkSamplerCreateInfo samplerCI{};
			samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerCI.magFilter = VK_FILTER_LINEAR;
			samplerCI.minFilter = VK_FILTER_LINEAR;
			samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCI.minLod = 0.0f;
			samplerCI.maxLod = static_cast<float>( numMips );
			samplerCI.maxAnisotropy = 1.0f;
			samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT( vkCreateSampler( device, &samplerCI, nullptr, &cubemap.sampler ) );
		}

		// FB, Att, RP, Pipe, etc.
		VkAttachmentDescription attDesc{};
		// Color attachment
		attDesc.format = format;
		attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription{};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Renderpass
		VkRenderPassCreateInfo renderPassCI{};
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.attachmentCount = 1;
		renderPassCI.pAttachments = &attDesc;
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpassDescription;
		renderPassCI.dependencyCount = 2;
		renderPassCI.pDependencies = dependencies.data();
		VkRenderPass renderpass;
		VK_CHECK_RESULT( vkCreateRenderPass( device, &renderPassCI, nullptr, &renderpass ) );

		struct Offscreen {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
			VkFramebuffer framebuffer;
		} offscreen;

		// Create offscreen framebuffer
		{
			// Image
			VkImageCreateInfo imageCI{};
			imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = format;
			imageCI.extent.width = dim;
			imageCI.extent.height = dim;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = 1;
			imageCI.arrayLayers = 1;
			imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT( vkCreateImage( device, &imageCI, nullptr, &offscreen.image ) );
			// printf( "Image 0x%llX function %s\n", (size_t)offscreen.image, __FUNCTION__ );

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements( device, offscreen.image, &memReqs );
			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
			VK_CHECK_RESULT( vkAllocateMemory( device, &memAllocInfo, nullptr, &offscreen.memory ) );
			VK_CHECK_RESULT( vkBindImageMemory( device, offscreen.image, offscreen.memory, 0 ) );

			// View
			VkImageViewCreateInfo viewCI{};
			viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCI.format = format;
			viewCI.flags = 0;
			viewCI.subresourceRange = {};
			viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewCI.subresourceRange.baseMipLevel = 0;
			viewCI.subresourceRange.levelCount = 1;
			viewCI.subresourceRange.baseArrayLayer = 0;
			viewCI.subresourceRange.layerCount = 1;
			viewCI.image = offscreen.image;
			VK_CHECK_RESULT( vkCreateImageView( device, &viewCI, nullptr, &offscreen.view ) );

			// Framebuffer
			VkFramebufferCreateInfo framebufferCI{};
			framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCI.renderPass = renderpass;
			framebufferCI.attachmentCount = 1;
			framebufferCI.pAttachments = &offscreen.view;
			framebufferCI.width = dim;
			framebufferCI.height = dim;
			framebufferCI.layers = 1;
			VK_CHECK_RESULT( vkCreateFramebuffer( device, &framebufferCI, nullptr, &offscreen.framebuffer ) );

			VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer( VK_COMMAND_BUFFER_LEVEL_PRIMARY, true );
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.image = offscreen.image;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier( layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier );
			vulkanDevice->flushCommandBuffer( layoutCmd, queue, true );
		}

		// Descriptors
		VkDescriptorSetLayout descriptorsetlayout;
		VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorSetLayoutCI.pBindings = &setLayoutBinding;
		descriptorSetLayoutCI.bindingCount = 1;
		VK_CHECK_RESULT( vkCreateDescriptorSetLayout( device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout ) );

		// Descriptor Pool
		VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolCI.poolSizeCount = 1;
		descriptorPoolCI.pPoolSizes = &poolSize;
		descriptorPoolCI.maxSets = 2;
		VkDescriptorPool descriptorpool;
		VK_CHECK_RESULT( vkCreateDescriptorPool( device, &descriptorPoolCI, nullptr, &descriptorpool ) );

		// Descriptor sets
		VkDescriptorSet descriptorset;
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorpool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT( vkAllocateDescriptorSets( device, &descriptorSetAllocInfo, &descriptorset ) );
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = descriptorset;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pImageInfo = &textures.environmentCube.descriptor;
		vkUpdateDescriptorSets( device, 1, &writeDescriptorSet, 0, nullptr );

		struct PushBlockIrradiance {
			glm::mat4 mvp;
			float deltaPhi = ( 2.0f * float( M_PI ) ) / 180.0f;
			float deltaTheta = ( 0.5f * float( M_PI ) ) / 64.0f;
		} pushBlockIrradiance;

		struct PushBlockPrefilterEnv {
			glm::mat4 mvp;
			float roughness;
			uint32_t numSamples = 32u;
		} pushBlockPrefilterEnv;

		// Pipeline layout
		VkPipelineLayout pipelinelayout;
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		switch ( target ) {
		case IRRADIANCE:
			pushConstantRange.size = sizeof( PushBlockIrradiance );
			break;
		case PREFILTEREDENV:
			pushConstantRange.size = sizeof( PushBlockPrefilterEnv );
			break;
		};

		VkPipelineLayoutCreateInfo pipelineLayoutCI{};
		pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCI.setLayoutCount = 1;
		pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT( vkCreatePipelineLayout( device, &pipelineLayoutCI, nullptr, &pipelinelayout ) );

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.lineWidth = 1.0f;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
		colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.front = depthStencilStateCI.back;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineViewportStateCreateInfo viewportStateCI{};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
		multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI{};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>( dynamicStateEnables.size() );

		// Vertex input state
		VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof( vkglTF::Model::Vertex ), VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = 1;
		vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = pipelinelayout;
		pipelineCI.renderPass = renderpass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = 2;
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.renderPass = renderpass;

		shaderStages[0] = loadShader( device, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT );
		switch ( target ) {
		case IRRADIANCE:
			shaderStages[1] = loadShader( device, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT );
			break;
		case PREFILTEREDENV:
			shaderStages[1] = loadShader( device, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT );
			break;
		};
		VkPipeline pipeline;
		VK_CHECK_RESULT( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline ) );
		for ( auto shaderStage : shaderStages ) {
			vkDestroyShaderModule( device, shaderStage.module, nullptr );
		}

		// Render cubemap
		VkClearValue clearValues[1];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderpass;
		renderPassBeginInfo.framebuffer = offscreen.framebuffer;
		renderPassBeginInfo.renderArea.extent.width = dim;
		renderPassBeginInfo.renderArea.extent.height = dim;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;

		std::vector<glm::mat4> matrices = {
			glm::rotate( glm::rotate( glm::mat4( 1.0f ), glm::radians( 90.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) ), glm::radians( 180.0f ), glm::vec3( 1.0f, 0.0f, 0.0f ) ),
			glm::rotate( glm::rotate( glm::mat4( 1.0f ), glm::radians( -90.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) ), glm::radians( 180.0f ), glm::vec3( 1.0f, 0.0f, 0.0f ) ),
			glm::rotate( glm::mat4( 1.0f ), glm::radians( -90.0f ), glm::vec3( 1.0f, 0.0f, 0.0f ) ),
			glm::rotate( glm::mat4( 1.0f ), glm::radians( 90.0f ), glm::vec3( 1.0f, 0.0f, 0.0f ) ),
			glm::rotate( glm::mat4( 1.0f ), glm::radians( 180.0f ), glm::vec3( 1.0f, 0.0f, 0.0f ) ),
			glm::rotate( glm::mat4( 1.0f ), glm::radians( 180.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ) ),
		};

		VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer( VK_COMMAND_BUFFER_LEVEL_PRIMARY, false );

		VkViewport viewport{};
		viewport.width = (float)dim;
		viewport.height = (float)dim;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.extent.width = width;
		scissor.extent.height = height;

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = numMips;
		subresourceRange.layerCount = 6;

		// Change image layout for all cubemap faces to transfer destination
		{
			vulkanDevice->beginCommandBuffer( cmdBuf );
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.image = cubemap.image;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.subresourceRange = subresourceRange;
			vkCmdPipelineBarrier( cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier );
			vulkanDevice->flushCommandBuffer( cmdBuf, queue, false );
		}

		for ( uint32_t m = 0; m < numMips; m++ ) {
			for ( uint32_t f = 0; f < 6; f++ ) {

				vulkanDevice->beginCommandBuffer( cmdBuf );

				viewport.width = static_cast<float>( dim * std::pow( 0.5f, m ) );
				viewport.height = static_cast<float>( dim * std::pow( 0.5f, m ) );
				vkCmdSetViewport( cmdBuf, 0, 1, &viewport );
				vkCmdSetScissor( cmdBuf, 0, 1, &scissor );

				// Render scene from cube face's point of view
				vkCmdBeginRenderPass( cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

				// Pass parameters for current pass using a push constant block
				switch ( target ) {
				case IRRADIANCE:
					pushBlockIrradiance.mvp = glm::perspective( (float)( M_PI / 2.0 ), 1.0f, 0.1f, 512.0f ) * matrices[f];
					vkCmdPushConstants( cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( PushBlockIrradiance ), &pushBlockIrradiance );
					break;
				case PREFILTEREDENV:
					pushBlockPrefilterEnv.mvp = glm::perspective( (float)( M_PI / 2.0 ), 1.0f, 0.1f, 512.0f ) * matrices[f];
					pushBlockPrefilterEnv.roughness = (float)m / (float)( numMips - 1 );
					vkCmdPushConstants( cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( PushBlockPrefilterEnv ), &pushBlockPrefilterEnv );
					break;
				};

				vkCmdBindPipeline( cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
				vkCmdBindDescriptorSets( cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL );

				VkDeviceSize offsets[1] = { 0 };

				m_skybox.draw( cmdBuf );

				vkCmdEndRenderPass( cmdBuf );

				VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				subresourceRange.baseMipLevel = 0;
				subresourceRange.levelCount = numMips;
				subresourceRange.layerCount = 6;

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.image = offscreen.image;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					vkCmdPipelineBarrier( cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier );
				}

				// Copy region for transfer from framebuffer to cube face
				VkImageCopy copyRegion{};

				copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.srcSubresource.baseArrayLayer = 0;
				copyRegion.srcSubresource.mipLevel = 0;
				copyRegion.srcSubresource.layerCount = 1;
				copyRegion.srcOffset = { 0, 0, 0 };

				copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.dstSubresource.baseArrayLayer = f;
				copyRegion.dstSubresource.mipLevel = m;
				copyRegion.dstSubresource.layerCount = 1;
				copyRegion.dstOffset = { 0, 0, 0 };

				copyRegion.extent.width = static_cast<uint32_t>( viewport.width );
				copyRegion.extent.height = static_cast<uint32_t>( viewport.height );
				copyRegion.extent.depth = 1;

				vkCmdCopyImage(
					cmdBuf,
					offscreen.image,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					cubemap.image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&copyRegion );

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.image = offscreen.image;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					vkCmdPipelineBarrier( cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier );
				}

				vulkanDevice->flushCommandBuffer( cmdBuf, queue, false );
			}
		}

		{
			vulkanDevice->beginCommandBuffer( cmdBuf );
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.image = cubemap.image;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.subresourceRange = subresourceRange;
			vkCmdPipelineBarrier( cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier );
			vulkanDevice->flushCommandBuffer( cmdBuf, queue, false );
		}


		vkDestroyRenderPass( device, renderpass, nullptr );
		vkDestroyFramebuffer( device, offscreen.framebuffer, nullptr );
		vkFreeMemory( device, offscreen.memory, nullptr );
		vkDestroyImageView( device, offscreen.view, nullptr );
		vkDestroyImage( device, offscreen.image, nullptr );
		vkDestroyDescriptorPool( device, descriptorpool, nullptr );
		vkDestroyDescriptorSetLayout( device, descriptorsetlayout, nullptr );
		vkDestroyPipeline( device, pipeline, nullptr );
		vkDestroyPipelineLayout( device, pipelinelayout, nullptr );

		cubemap.descriptor.imageView = cubemap.view;
		cubemap.descriptor.sampler = cubemap.sampler;
		cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		cubemap.device = vulkanDevice;

		switch ( target ) {
		case IRRADIANCE:
			textures.irradianceCube = cubemap;
			break;
		case PREFILTEREDENV:
			textures.prefilteredCube = cubemap;
			shaderValuesParams.prefilteredCubeMipLevels = static_cast<float>( numMips );
			break;
		};

		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>( tEnd - tStart ).count();
		getOut() << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
	}
}

/*
	Prepare and initialize uniform buffers containing shader parameters
*/
void VulkanExample::prepareUniformBuffers()
{
	for ( auto &uniformBuffer : uniformBuffers ) {
		uniformBuffer.scene.create( vulkanDevice, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			sizeof( shaderValuesScene ) );
		uniformBuffer.skybox.create( vulkanDevice, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			sizeof( shaderValuesSkybox ) );
		uniformBuffer.params.create( vulkanDevice, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			sizeof( shaderValuesParams ) );
		uniformBuffer.leftEye.create( vulkanDevice, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			sizeof( shaderValuesLeftEye ) );
		uniformBuffer.rightEye.create( vulkanDevice, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			sizeof( shaderValuesRightEye ) );
	}
	updateUniformBuffers();
}

void VulkanExample::updateUniformBuffers()
{
	// Scene
	shaderValuesScene.matProjectionFromView = camera.matrices.perspective;
	shaderValuesScene.matViewFromHmd = camera.matrices.view;

	// Center and scale model
	glm::mat4 aabb( 1.f );
	float scale = ( 1.0f / std::max( aabb[0][0], std::max( aabb[1][1], aabb[2][2] ) ) ) * 0.5f;
	glm::vec3 translate = -glm::vec3( aabb[3][0], aabb[3][1], aabb[3][2] );
	translate += -0.5f * glm::vec3( aabb[0][0], aabb[1][1], aabb[2][2] );

	shaderValuesScene.matHmdFromStage = glm::mat4( 1.0f );
	shaderValuesScene.matHmdFromStage[0][0] = scale;
	shaderValuesScene.matHmdFromStage[1][1] = scale;
	shaderValuesScene.matHmdFromStage[2][2] = scale;
	shaderValuesScene.matHmdFromStage = glm::translate( shaderValuesScene.matHmdFromStage, translate );

	shaderValuesScene.camPos = glm::vec3(
		-camera.position.z * sin( glm::radians( camera.rotation.y ) ) * cos( glm::radians( camera.rotation.x ) ),
		-camera.position.z * sin( glm::radians( camera.rotation.x ) ),
		camera.position.z * cos( glm::radians( camera.rotation.y ) ) * cos( glm::radians( camera.rotation.x ) )
	);

	// Skybox
	shaderValuesSkybox.matProjectionFromView = camera.matrices.perspective;
	shaderValuesSkybox.matViewFromHmd = shaderValuesScene.matProjectionFromView;
	shaderValuesSkybox.matHmdFromStage = glm::mat4( glm::mat3( camera.matrices.view ) );

	// left eye
	shaderValuesLeftEye.matProjectionFromView = m_vargglesEyePerspectiveProjection;
	shaderValuesLeftEye.matViewFromHmd = GetHMDMatrixPoseEye( vr::Eye_Left );
	shaderValuesLeftEye.matHmdFromStage = m_vrManager->getHmdFromUniverse();

	glm::mat4 stageFromView = glm::inverse( shaderValuesLeftEye.matViewFromHmd * shaderValuesLeftEye.matHmdFromStage );
	shaderValuesLeftEye.camPos = glm::vec3( stageFromView * glm::vec4( 0, 0, 0, 1.f ) );

	// right eye
	shaderValuesRightEye.matProjectionFromView = m_vargglesEyePerspectiveProjection;
	shaderValuesRightEye.matViewFromHmd = GetHMDMatrixPoseEye( vr::Eye_Right );
	shaderValuesRightEye.matHmdFromStage = m_vrManager->getHmdFromUniverse();
	stageFromView = glm::inverse( shaderValuesRightEye.matViewFromHmd * shaderValuesRightEye.matHmdFromStage );
	shaderValuesRightEye.camPos = glm::vec3( stageFromView * glm::vec4( 0, 0, 0, 1.f ) );
}

void VulkanExample::updateParams()
{
	shaderValuesParams.lightDir = glm::vec4(
		sin( glm::radians( lightSource.rotation.x ) ) * cos( glm::radians( lightSource.rotation.y ) ),
		sin( glm::radians( lightSource.rotation.y ) ),
		cos( glm::radians( lightSource.rotation.x ) ) * cos( glm::radians( lightSource.rotation.y ) ),
		0.0f );
	shaderValuesParams.debugViewInputs = 0;
	shaderValuesParams.debugViewEquation = 0;
}

void VulkanExample::windowResized()
{
	vkDeviceWaitIdle( device );
	updateUniformBuffers();
}

void VulkanExample::prepare()
{
	VulkanExampleBase::prepare();

	camera.type = Camera::CameraType::lookat;

	camera.setPerspective( 45.0f, (float)width / (float)height, 0.1f, 256.0f );
	camera.rotationSpeed = 0.25f;
	camera.movementSpeed = 0.1f;
	camera.setPosition( { 0.0f, 0.0f, 1.0f } );
	camera.setRotation( { 0.0f, 0.0f, 0.0f } );

	waitFences.resize( renderAhead );
	presentCompleteSemaphores.resize( renderAhead );
	renderCompleteSemaphores.resize( renderAhead );
	commandBuffers.resize( swapChain.imageCount );
	uniformBuffers.resize( swapChain.imageCount );
	descriptorSets.resize( swapChain.imageCount );
	m_vargglesVulkanBindings.descriptorsets.resize(swapChain.imageCount);
	// Command buffer execution fences
	for ( auto &waitFence : waitFences ) {
		VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
		VK_CHECK_RESULT( vkCreateFence( device, &fenceCI, nullptr, &waitFence ) );
	}
	// Queue ordering semaphores
	for ( auto &semaphore : presentCompleteSemaphores ) {
		VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
		VK_CHECK_RESULT( vkCreateSemaphore( device, &semaphoreCI, nullptr, &semaphore ) );
	}
	for ( auto &semaphore : renderCompleteSemaphores ) {
		VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
		VK_CHECK_RESULT( vkCreateSemaphore( device, &semaphoreCI, nullptr, &semaphore ) );
	}
	// Command buffers
	{
		VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
		cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdBufAllocateInfo.commandPool = cmdPool;
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdBufAllocateInfo.commandBufferCount = static_cast<uint32_t>( commandBuffers.size() );
		VK_CHECK_RESULT( vkAllocateCommandBuffers( device, &cmdBufAllocateInfo, commandBuffers.data() ) );
	}

	//vr::VRSystem()->GetRecommendedRenderTargetSize( &eyeWidth, &eyeHeight );
	eyeWidth = m_unVargglesEyeResolution;
	eyeHeight = m_unVargglesEyeResolution;
	leftEyeRT.init( swapChain.colorFormat, depthFormat, eyeWidth, eyeHeight, vulkanDevice, queue, settings.multiSampling );
	rightEyeRT.init( swapChain.colorFormat, depthFormat, eyeWidth, eyeHeight, vulkanDevice, queue, settings.multiSampling );
	vargglesRT.init( swapChain.colorFormat, depthFormat, m_unVargglesWidth, m_unVargglesHeight, vulkanDevice, queue, settings.multiSampling );

	loadAssets();
	generateBRDFLUT();
	generateCubemaps();
	prepareUniformBuffers();
	setupDescriptors();
	preparePipelines();
	prepareVarggles();

	prepared = true;
}

void VulkanExample::onWindowClose()
{
	if ( CAardvarkCefApp::instance() )
	{
		// CAardvarkCefApp::instance()->CloseAllBrowsers( true );
	}
}

std::shared_ptr<vkglTF::Model> VulkanExample::findOrLoadModel( std::string modelUri, std::string *psError )
{
	auto iModel = m_mapModels.find( modelUri );
	if ( iModel != m_mapModels.end() )
	{
    getOut() << "Parse found" << std::endl;
		return iModel->second;
	}

  std::vector<char> data = readFile(modelUri);
  return loadModelFromMemory(modelUri, data.data(), data.size());
}

std::shared_ptr<vkglTF::Model> VulkanExample::loadModelFromMemory(const std::string &modelUri, const char *data, size_t size) {
  auto pModel = std::make_shared<vkglTF::Model>();
  bool bLoaded = pModel->loadFromMemory( data, size, vulkanDevice, m_descriptorManager, queue );

  if ( bLoaded )
  {
    m_mapModels.insert( std::make_pair( modelUri, pModel ) );
    setupDescriptorSetsForModel( pModel );
    getOut() << "Load ok " << modelUri << std::endl;
    return pModel;
  }
  else
  {
    getOut() << "Parse failed" << std::endl;
  }

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Gets a Matrix Projection Eye with respect to nEye.
//-----------------------------------------------------------------------------
glm::mat4 VulkanExample::GetHMDMatrixProjectionEye( vr::Hmd_Eye nEye )
{
	if ( !vr::VRSystem() )
		return glm::mat4( 1.f );

	vr::HmdMatrix44_t mat = vr::VRSystem()->GetProjectionMatrix( nEye, 0.1f, 50.f );

	return glm::mat4(
		mat.m[0][0], mat.m[1][0], mat.m[2][0], mat.m[3][0],
		mat.m[0][1], mat.m[1][1], mat.m[2][1], mat.m[3][1],
		mat.m[0][2], mat.m[1][2], mat.m[2][2], mat.m[3][2],
		mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3]
	);
}

//-----------------------------------------------------------------------------
// Purpose: Gets an HMDMatrixPoseEye with respect to nEye.
//-----------------------------------------------------------------------------
glm::mat4 VulkanExample::GetHMDMatrixPoseEye( vr::Hmd_Eye nEye )
{
	if ( !vr::VRSystem() )
		return glm::mat4( 1.f );

	vr::HmdMatrix34_t matEyeRight = vr::VRSystem()->GetEyeToHeadTransform( nEye );
	glm::mat4 matrixObj(
		matEyeRight.m[0][0], matEyeRight.m[1][0], matEyeRight.m[2][0], 0.0,
		matEyeRight.m[0][1], matEyeRight.m[1][1], matEyeRight.m[2][1], 0.0,
		matEyeRight.m[0][2], matEyeRight.m[1][2], matEyeRight.m[2][2], 0.0,
		matEyeRight.m[0][3], matEyeRight.m[1][3], matEyeRight.m[2][3], 1.0f
	);

	return glm::inverse( matrixObj );
}

void VulkanExample::render()
{
}

struct SortableModel_t
{
	float distance;
	CVulkanRendererModelInstance *model;
};

bool operator<( const SortableModel_t & lhs, const SortableModel_t & rhs )
{
	return lhs.distance > rhs.distance;
}

void VulkanExample::update()
{
  /* getOut() << "example update" << std::endl;
  for ( auto model : m_modelsToRender ) {
    float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    model->m_model->translation.y = r;
  } */
}

void VulkanExample::processRenderList()
{
	// animate everything
	glm::mat4 hmdFromUniverse = m_vrManager->getHmdFromUniverse();
	std::vector<SortableModel_t> sortableModels;
	for ( auto model : m_modelsToRender )
	{
		model->animate( frameTimer );

		glm::mat4 hmdFromModel = hmdFromUniverse * model->m_modelParent.matParentFromNode;
		glm::vec4 modelInHmdSpace = hmdFromModel * glm::vec4( { 0, 0, 0, 1.f } );
		float dist = glm::length( glm::vec3( modelInHmdSpace ) );

		sortableModels.push_back( { dist, model } );
	}

	// sort models back to front so transparency will work 
	// if there is any. Yes, this will be wrong when things 
	// intersect.
	std::sort( sortableModels.begin(), sortableModels.end() );
	m_modelsToRender.clear();
	for ( auto & sorted : sortableModels )
	{
		m_modelsToRender.push_back( sorted.model );
	}

	if ( m_updateDescriptors )
	{
		m_descriptorManager->updateDescriptors();
		m_updateDescriptors = false;
	}

	VK_CHECK_RESULT( vkWaitForFences( device, 1, &waitFences[frameIndex], VK_TRUE, UINT64_MAX ) );
	VK_CHECK_RESULT( vkResetFences( device, 1, &waitFences[frameIndex] ) );

	VkResult acquire = swapChain.acquireNextImage( presentCompleteSemaphores[frameIndex], &currentBuffer );
	if ( ( acquire == VK_ERROR_OUT_OF_DATE_KHR ) || ( acquire == VK_SUBOPTIMAL_KHR ) ) {
		windowResize();
	}
	else {
		VK_CHECK_RESULT( acquire );
	}

	recordCommandBuffers( currentBuffer );

	// Update UBOs
	updateUniformBuffers();
	UniformBufferSet currentUB = uniformBuffers[currentBuffer];
	memcpy( currentUB.scene.mapped, &shaderValuesScene, sizeof( shaderValuesScene ) );
	memcpy( currentUB.leftEye.mapped, &shaderValuesLeftEye, sizeof( shaderValuesLeftEye ) );
	memcpy( currentUB.rightEye.mapped, &shaderValuesRightEye, sizeof( shaderValuesRightEye ) );
	memcpy( currentUB.params.mapped, &shaderValuesParams, sizeof( shaderValuesParams ) );
	memcpy( currentUB.skybox.mapped, &shaderValuesSkybox, sizeof( shaderValuesSkybox ) );

	const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pWaitDstStageMask = &waitDstStageMask;
	submitInfo.pWaitSemaphores = &presentCompleteSemaphores[frameIndex];
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderCompleteSemaphores[frameIndex];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[currentBuffer];
	submitInfo.commandBufferCount = 1;
	VK_CHECK_RESULT( vkQueueSubmit( queue, 1, &submitInfo, waitFences[frameIndex] ) );

	submitEyeBuffers();
	setOverlayTexture();

	VkResult present = swapChain.queuePresent( queue, currentBuffer, renderCompleteSemaphores[frameIndex] );
	if ( !( ( present == VK_SUCCESS ) || ( present == VK_SUBOPTIMAL_KHR ) ) ) {
		if ( present == VK_ERROR_OUT_OF_DATE_KHR ) {
			windowResize();
			return;
		}
		else {
			VK_CHECK_RESULT( present );
		}
	}

	frameIndex += 1;
	frameIndex %= renderAhead;

	if ( !paused ) {
		if ( rotateModel ) {
			modelrot.y += frameTimer * 35.0f;
			if ( modelrot.y > 360.0f ) {
				modelrot.y -= 360.0f;
			}
		}

		updateParams();
		if ( rotateModel ) {
			updateUniformBuffers();
		}
	}
	if ( camera.updated ) {
		updateUniformBuffers();
	}

	// m_uriRequests.processResults();
}

bool VulkanExample::getModelBox( const std::string & uri, AABB_t *pBox, std::string *psError)
{
	auto pModel = findOrLoadModel( uri, psError );
	if ( !pModel )
	{
		return false;
	}

	pBox->xMin = pModel->dimensions.min.x;
	pBox->yMin = pModel->dimensions.min.y;
	pBox->zMin = pModel->dimensions.min.z;
	pBox->xMax = pModel->dimensions.max.x;
	pBox->yMax = pModel->dimensions.max.y;
	pBox->zMax = pModel->dimensions.max.z;
	return true;
}


void VulkanExample::submitEyeBuffers()
{
	// Submit to OpenVR
	vr::VRTextureBounds_t bounds;
	bounds.uMin = 0.0f;
	bounds.uMax = 1.0f;
	bounds.vMin = 0.0f;
	bounds.vMax = 1.0f;

	vr::VRVulkanTextureData_t vulkanData;
	vulkanData.m_nImage = (uint64_t)leftEyeRT.color.image;
	vulkanData.m_pDevice = (VkDevice_T *)device;
	vulkanData.m_pPhysicalDevice = (VkPhysicalDevice_T *)vulkanDevice->physicalDevice;
	vulkanData.m_pInstance = (VkInstance_T *)instance;
	vulkanData.m_pQueue = (VkQueue_T *)queue;
	vulkanData.m_nQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;

	vulkanData.m_nWidth = eyeWidth;
	vulkanData.m_nHeight = eyeHeight;
	vulkanData.m_nFormat = VK_FORMAT_R8G8B8A8_UNORM;
	vulkanData.m_nSampleCount = 1;

	vr::Texture_t texture = { &vulkanData, vr::TextureType_Vulkan, vr::ColorSpace_Auto };
	//vr::VRCompositor()->Submit( vr::Eye_Left, &texture, &bounds );

	vulkanData.m_nImage = (uint64_t)rightEyeRT.color.image;
	//vr::VRCompositor()->Submit( vr::Eye_Right, &texture, &bounds );
}

void VulkanExample::setOverlayTexture() {
	vr::VRTextureBounds_t bounds;
	bounds.uMin = 0.0f;
	bounds.uMax = 1.0f;
	bounds.vMin = 0.0f;
	bounds.vMax = 1.0f;

	vr::VRVulkanTextureData_t vulkanData;
	vulkanData.m_nImage = (uint64_t)vargglesRT.color.image;
	vulkanData.m_pDevice = (VkDevice_T *)device;
	vulkanData.m_pPhysicalDevice = (VkPhysicalDevice_T *)vulkanDevice->physicalDevice;
	vulkanData.m_pInstance = (VkInstance_T *)instance;
	vulkanData.m_pQueue = (VkQueue_T *)queue;
	vulkanData.m_nQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;

	vulkanData.m_nWidth = m_unVargglesWidth;
	vulkanData.m_nHeight = m_unVargglesHeight;
	vulkanData.m_nFormat = VK_FORMAT_R8G8B8A8_UNORM;
	vulkanData.m_nSampleCount = 1;

	vr::Texture_t texture = { &vulkanData, vr::TextureType_Vulkan, vr::ColorSpace_Auto };
	m_vrManager->setVargglesTexture(&texture);
}

CVulkanRendererModelInstance::CVulkanRendererModelInstance( 
	VulkanExample *renderer,
	const std::string & uri, std::shared_ptr< vkglTF::Model > modelExample )
{
	m_renderer = renderer;
	m_modelUri = uri;
	m_model = modelExample;
	m_model->parent = &m_modelParent;
}

CVulkanRendererModelInstance::~CVulkanRendererModelInstance()
{

}

void CVulkanRendererModelInstance::setUniverseFromModel( const glm::mat4 & universeFromModel )
{
	m_modelParent.matParentFromNode = universeFromModel;
}

void CVulkanRendererModelInstance::setOverrideTexture( void *textureHandle, ETextureType type, ETextureFormat format,
	uint32_t width, uint32_t height )
{
	void *pvNewDxgiHandle = textureHandle;
	VkFormat textureFormat = VK_FORMAT_R8G8B8A8_UINT;
	VkFormat viewTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
	switch ( format )
	{
	default:
		assert( false );
		break;

	case ETextureFormat::R8G8B8A8:
		textureFormat = VK_FORMAT_R8G8B8A8_UINT;
		viewTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
		break;

	case ETextureFormat::B8G8R8A8:
		textureFormat = VK_FORMAT_B8G8R8A8_UINT;
		viewTextureFormat = VK_FORMAT_B8G8R8A8_UNORM;
		break;
	}

	if ( m_lastDxgiHandle != pvNewDxgiHandle )
	{
		m_overrideTexture = std::make_shared<vks::Texture2D>();
		m_overrideTexture->loadFromDxgiSharedHandle( pvNewDxgiHandle,
			textureFormat, viewTextureFormat,
			width, height,
			m_renderer->vulkanDevice, m_renderer->queue );

		for ( auto & material : m_model->materials )
		{
			material.baseColorTexture = m_overrideTexture;
		}

		m_renderer->setupDescriptorSetsForModel( m_model );

		m_lastDxgiHandle = pvNewDxgiHandle;
	}

}

void CVulkanRendererModelInstance::setBaseColor( const glm::vec4 & color )
{
	if ( m_lastBaseColor!= color )
	{
		m_lastBaseColor = color;

		for ( auto & material : m_model->materials )
		{
			material.baseColorFactor = color;
		}

		m_renderer->setupDescriptorSetsForModel( m_model );
	}
}



void CVulkanRendererModelInstance::animate( float animationTimeElapsed )
{
	m_model->animate( animationTimeElapsed );

	// TODO(Joe): Figure out how to only do this when a parent is changing
	for ( auto &node : m_model->nodes ) {
		node->update();
	}
}

std::unique_ptr<IModelInstance> VulkanExample::loadModelInstance(const std::string &modelUrl, unsigned char *data, size_t size) {
  std::shared_ptr<tinygltf::Model> model(new tinygltf::Model());

  bool planeModelLoaded = false;
  if (size >= 4) {
    tinygltf::TinyGLTF gltfContext;

    std::string error;
    std::string warning;
    
    const bool bBinary = *((uint32_t *)data) == 0x46546C67;
    if ( bBinary )
    {
      planeModelLoaded = gltfContext.LoadBinaryFromMemory( model.get(), &error, &warning, data, size );
    }
    else
    {
      planeModelLoaded = gltfContext.LoadASCIIFromString( model.get(), &error, &warning, (char *)data, size, ""  );
    }
  }
  
  auto model2 = std::make_shared<vkglTF::Model>();
  model2->loadFromGltfModel( vulkanDevice, m_descriptorManager, model, queue, 1.0f );
  setupDescriptorSetsForModel( model2 );
  return std::make_unique<CVulkanRendererModelInstance>( this, modelUrl, model2 );
}

std::shared_ptr<vkglTF::Model> planeModelVk;
std::unique_ptr<IModelInstance> VulkanExample::createDefaultModelInstance(const std::string &modelUrl) {
  if (!planeModelVk) {
    {    
      std::string uri("data/plane.glb");
      // uri = "data/models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
      std::vector<char> data = readFile(uri);

      std::shared_ptr<tinygltf::Model> planeModel(new tinygltf::Model());
      bool planeModelLoaded = false;
      if (data.size() >= 4) {
        tinygltf::TinyGLTF gltfContext;

        std::string error;
        std::string warning;
        
        const bool bBinary = *((uint32_t *)data.data()) == 0x46546C67;
        if ( bBinary )
        {
          planeModelLoaded = gltfContext.LoadBinaryFromMemory( planeModel.get(), &error, &warning, (const unsigned char*)data.data(), (uint32_t)data.size() );
        }
        else
        {
          planeModelLoaded = gltfContext.LoadASCIIFromString( planeModel.get(), &error, &warning, (const char*)data.data(), (uint32_t)data.size(), ""  );
        }
      }
      
      if (planeModelLoaded) {
        planeModelVk = std::make_shared<vkglTF::Model>();
        planeModelVk->loadFromGltfModel( vulkanDevice, m_descriptorManager, planeModel, queue, 1.0f );

        
        /* auto &images = planeModel.images;
        for (auto &image : images) {
          getOut() << "got image " << image.mimeType << " " << image.width << " " << image.height << " " << image.component << " " << image.image.size() << std::endl;
          image.image = {
            255,
            0,
            0,
            255,
          };
        }
        
        planeModelVk->nodes.clear();
        planeModelVk->linearNodes.clear();
        planeModelVk->buffers = nullptr;
        planeModelVk->skins.clear();
        planeModelVk->textures.clear();
        planeModelVk->textureSamplers.clear();
        // planeModelVk->materials.clear();

        planeModelVk->loadFromGltfModel( vulkanDevice, m_descriptorManager, planeModel, queue, 1.0f ); */
        
        setupDescriptorSetsForModel( planeModelVk );
      }
    }

    if (!planeModelVk) {
      getOut() << "failed to get plane model" << std::endl;
      abort();
    }
  }
  
  getOut() << "model loaded 2 " << planeModelVk->modelPtr.get() << std::endl;

  // auto model = std::make_shared<vkglTF::Model>();
  // model->copyFrom(*planeModelVk);
	return std::make_unique<CVulkanRendererModelInstance>( this, modelUrl, planeModelVk );
}

void VulkanExample::setModelTransform(IModelInstance *modelInstance, float *positions, size_t numPositions, float *quaternions, size_t numQuaternions, float *scales, size_t numScales) {
  CVulkanRendererModelInstance *model = (CVulkanRendererModelInstance *)modelInstance;

  model->m_model->translation.x = positions[0];
  model->m_model->translation.y = positions[1];
  model->m_model->translation.z = positions[2];

  model->m_model->rotation.x = quaternions[0];
  model->m_model->rotation.y = quaternions[1];
  model->m_model->rotation.z = quaternions[2];
  model->m_model->rotation.w = quaternions[3];

  model->m_model->scale.x = scales[0];
  model->m_model->scale.y = scales[1];
  model->m_model->scale.z = scales[2];
}

void VulkanExample::setModelMatrix(IModelInstance *modelInstance, float *matrix, size_t numPositions) {
  CVulkanRendererModelInstance *model = (CVulkanRendererModelInstance *)modelInstance;

  decomposeMatrix(matrix, glm::value_ptr(model->m_model->translation), glm::value_ptr(model->m_model->rotation), glm::value_ptr(model->m_model->scale));
}

std::unique_ptr<IModelInstance> VulkanExample::setModelGeometry(std::unique_ptr<IModelInstance> modelInstance, float *positions, size_t numPositions, float *normals, size_t numNormals, float *colors, size_t numColors, float *uvs, size_t numUvs, uint16_t *indices, size_t numIndices) {
  getOut() << "set 0 " << modelInstance.get() << std::endl;

  CVulkanRendererModelInstance *model = (CVulkanRendererModelInstance *)modelInstance.get();

  getOut() << "set 1" << std::endl;

  std::shared_ptr<tinygltf::Model> model2(new tinygltf::Model(*model->m_model->modelPtr));
  auto &accessors = model2->accessors;
  auto &bufferViews = model2->bufferViews;
  auto &buffers = model2->buffers;
  auto &materials = model2->materials;
  auto &samplers = model2->samplers;
  // auto &images = model2->images;
  for (auto &mesh : model2->meshes) {
    for (auto &primitive : mesh.primitives) {
    	// attributes
      std::map<std::string, int> &attributes = primitive.attributes;
      for (auto &attribute : attributes) {
        auto &accessor = accessors[attribute.second];
        auto &count = accessor.count;
        auto &type = accessor.type;
        const auto &elementSize = tinygltf::GetTypeSizeInBytes(type);

        auto &bufferView = bufferViews[accessor.bufferView];
        auto &byteOffset = bufferView.byteOffset;
        
        auto &buffer = buffers[bufferView.buffer];
        unsigned char *dataStart = &buffer.data[byteOffset];

        const char *typeString;
        if (type == TINYGLTF_TYPE_SCALAR) {
          typeString = "scalar";
        } else if (type == TINYGLTF_TYPE_VEC2) {
          typeString = "vec2";
        } else if (type == TINYGLTF_TYPE_VEC3) {
          typeString = "vec3";
        } else if (type == TINYGLTF_TYPE_VEC4) {
          typeString = "vec4";
        } else if (type == TINYGLTF_TYPE_MAT2) {
          typeString = "mat2";
        } else if (type == TINYGLTF_TYPE_MAT3) {
          typeString = "mat3";
        } else if (type == TINYGLTF_TYPE_MAT4) {
          typeString = "mat4";
        } else {
          typeString = "unknown";
        }
        getOut() << "got accessor " << attribute.first << " " << attribute.second << " " << typeString << " " << accessor.bufferView << " " << byteOffset << " '" << buffer.name << "'" << std::endl;
        if (type == TINYGLTF_TYPE_VEC3) {
          for (size_t i = 0; i < count; i++) {
            float *v3 = &((float *)dataStart)[i*3];
            getOut() << v3[0] << " " << v3[1] << " " << v3[2] << std::endl;
          }
        }
      }
      // index
      {
	      auto &accessor = accessors[primitive.indices];
	      auto &componentType = accessor.componentType;
	      auto &count = accessor.count;
	      const auto &componentSize = tinygltf::GetComponentSizeInBytes(componentType);
	      
	      auto &bufferView = bufferViews[accessor.bufferView];
	      auto &byteOffset = bufferView.byteOffset;
	      
	      auto &buffer = buffers[bufferView.buffer];
	      unsigned char *dataStart = &buffer.data[byteOffset];

	      const char *componentTypeString;
	      if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
	        componentTypeString = "byte";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
	        componentTypeString = "unsignedbyte";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
	        componentTypeString = "short";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
	        componentTypeString = "unsignedshort";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_INT) {
	        componentTypeString = "int";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
	        componentTypeString = "unsignedint";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
	        componentTypeString = "float";
	      } else if (componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) {
	        componentTypeString = "double";
	      } else {
	        componentTypeString = "unknown";
	      }
	      getOut() << "got index " << componentTypeString << " " << count << " " << componentSize << std::endl;
	      if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
	        for (size_t i = 0; i < count; i++) {
	          uint16_t *v = &((uint16_t *)dataStart)[i];
	          getOut() << *v << " ";
	        }
	        getOut() << std::endl;
	      }
	    }

      // attributes
      getOut() << "set 2" << std::endl;
      {
      	auto &attribute = attributes["POSITION"];
      	const auto &src = positions;
        const auto &size = numPositions;

      	auto &accessor = accessors[attribute];
      	accessor.byteOffset = 0;
      	accessor.count = size;
        // auto &count = accessor.count;
        // auto &type = accessor.type;
        // const auto &elementSize = tinygltf::GetTypeSizeInBytes(type);

        // auto bufferView = bufferViews[accessor.bufferView];
        // bufferViews.push_back(bufferView);

        std::vector<unsigned char> data(size * sizeof(src[0]));
        memcpy(data.data(), src, data.size());
        tinygltf::Buffer buffer{
        	std::string(), // std::string name;
				  std::move(data), // std::vector<unsigned char> data;
				  std::string(), // std::string uri;  // considered as required here but not in the spec (need to clarify)
				  tinygltf::Value() // Value extras;
				};
				buffers.push_back(std::move(buffer));

        tinygltf::BufferView bufferView{};
        bufferView.buffer = buffers.size() - 1;
        bufferView.byteLength = size * sizeof(src[0]);
        bufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        /* tinygltf::BufferView bufferView{
          "", // std::string name;
				  buffers.size() - 1, // int buffer;         // Required
				  0, // size_t byteOffset;  // minimum 0, default 0
				  src.size() * sizeof(src[0]), // size_t byteLength;  // required, minimum 1
				  0, // size_t byteStride;  // minimum 4, maximum 252 (multiple of 4), default 0 = understood to be tightly packed
				  TINYGLTF_TARGET_ARRAY_BUFFER, // int target;         // ["ARRAY_BUFFER", "ELEMENT_ARRAY_BUFFER"]
				  {}, // Value extras;
				  false // bool dracoDecoded;  // Flag indicating this has been draco decoded
        }; */
        bufferViews.push_back(std::move(bufferView));
        accessor.bufferView = bufferViews.size() - 1;
      }
      getOut() << "set 2" << std::endl;
      {
        auto &attribute = attributes["NORMAL"];
        const auto &src = normals;
        const auto &size = numNormals;

      	auto &accessor = accessors[attribute];
      	accessor.byteOffset = 0;
      	accessor.count = size;

        std::vector<unsigned char> data(size * sizeof(src[0]));
        memcpy(data.data(), src, data.size());
        tinygltf::Buffer buffer{
        	std::string(), // std::string name;
				  std::move(data), // std::vector<unsigned char> data;
				  std::string(), // std::string uri;  // considered as required here but not in the spec (need to clarify)
				  tinygltf::Value() // Value extras;
				};
				buffers.push_back(std::move(buffer));

        tinygltf::BufferView bufferView{};
        bufferView.buffer = buffers.size() - 1;
        bufferView.byteLength = size * sizeof(src[0]);
        bufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        bufferViews.push_back(std::move(bufferView));
        accessor.bufferView = bufferViews.size() - 1;
      }
      {
        auto &attribute = attributes["COLOR_0"];
        const auto &src = colors;
        const auto &size = numColors;

      	auto &accessor = accessors[attribute];
      	accessor.byteOffset = 0;
      	accessor.count = size;

        std::vector<unsigned char> data(size * sizeof(src[0]));
        memcpy(data.data(), src, data.size());
        tinygltf::Buffer buffer{
        	std::string(), // std::string name;
				  std::move(data), // std::vector<unsigned char> data;
				  std::string(), // std::string uri;  // considered as required here but not in the spec (need to clarify)
				  tinygltf::Value() // Value extras;
				};
				buffers.push_back(std::move(buffer));

        tinygltf::BufferView bufferView{};
        bufferView.buffer = buffers.size() - 1;
        bufferView.byteLength = size * sizeof(src[0]);
        bufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        bufferViews.push_back(std::move(bufferView));
        accessor.bufferView = bufferViews.size() - 1;
      }
      {
        auto &attribute = attributes["TEXCOORD_0"];
        const auto &src = uvs;
        const auto &size = numUvs;

      	auto &accessor = accessors[attribute];
      	accessor.byteOffset = 0;
      	accessor.count = size;

        std::vector<unsigned char> data(size * sizeof(src[0]));
        memcpy(data.data(), src, data.size());
        tinygltf::Buffer buffer{
        	std::string(), // std::string name;
				  std::move(data), // std::vector<unsigned char> data;
				  std::string(), // std::string uri;  // considered as required here but not in the spec (need to clarify)
				  tinygltf::Value() // Value extras;
				};
				buffers.push_back(std::move(buffer));

        tinygltf::BufferView bufferView{};
        bufferView.buffer = buffers.size() - 1;
        bufferView.byteLength = size * sizeof(src[0]);
        bufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        bufferViews.push_back(std::move(bufferView));
        accessor.bufferView = bufferViews.size() - 1;
      }
      // index
      {
      	const auto &src = indices;
      	const auto &size = numIndices;

	      auto &accessor = accessors[primitive.indices];
      	accessor.byteOffset = 0;
      	accessor.count = size;

        /* auto buffer = tinygltf::Buffer{
        	std::string(), // std::string name;
				  std::vector<unsigned char>((unsigned char *)src.data(), src.size() * sizeof(src[0])), // std::vector<unsigned char> data;
				  std::string(), // std::string uri;  // considered as required here but not in the spec (need to clarify)
				  tinygltf::Value() // Value extras;
				}; */
        std::vector<unsigned char> data(size * sizeof(src[0]));
        memcpy(data.data(), src, data.size());
        tinygltf::Buffer buffer{
        	std::string(), // std::string name;
				  std::move(data), // std::vector<unsigned char> data;
				  std::string(), // std::string uri;  // considered as required here but not in the spec (need to clarify)
				  tinygltf::Value() // Value extras;
				};
				buffers.push_back(std::move(buffer));

        /* auto bufferView = tinygltf::BufferView{
          std::string(), // std::string name;
				  buffers.size() - 1, // int buffer;         // Required
				  0, // size_t byteOffset;  // minimum 0, default 0
				  src.size() * sizeof(src[0]), // size_t byteLength;  // required, minimum 1
				  0, // size_t byteStride;  // minimum 4, maximum 252 (multiple of 4), default 0 = understood to be tightly packed
				  TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, // int target;         // ["ARRAY_BUFFER", "ELEMENT_ARRAY_BUFFER"]
				  tinygltf::Value(), // Value extras;
				  false // bool dracoDecoded;  // Flag indicating this has been draco decoded
        }; */
        tinygltf::BufferView bufferView{};
        bufferView.buffer = buffers.size() - 1;
        bufferView.byteLength = size * sizeof(src[0]);
        bufferView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
        bufferViews.push_back(std::move(bufferView));
        accessor.bufferView = bufferViews.size() - 1;
	    }
      // materials
      for (auto &material : materials) {
        getOut() << "got material" << std::endl;
      }
      for (auto &sampler : samplers) {
        getOut() << "got sampler" << std::endl;
      }
    }
  }

  auto model3 = std::make_shared<vkglTF::Model>();
  model3->loadFromGltfModel( vulkanDevice, m_descriptorManager, model2, queue, 1.0f );
  setupDescriptorSetsForModel( model3 );

  auto result = std::make_unique<CVulkanRendererModelInstance>( this, model->m_modelUri, model3 );
  result->m_model->translation = model->m_model->translation;
  result->m_model->rotation = model->m_model->rotation;
  result->m_model->scale = model->m_model->scale;
  return std::move(result);
}

std::unique_ptr<IModelInstance> VulkanExample::setModelTexture(std::unique_ptr<IModelInstance> modelInstance, int width, int height, unsigned char *data, size_t size) {
  CVulkanRendererModelInstance *model = (CVulkanRendererModelInstance *)modelInstance.get();

  std::shared_ptr<tinygltf::Model> model2(new tinygltf::Model(*(model->m_model->modelPtr)));
  auto &images = model2->images;
  for (auto &image : images) {
    image.width = width;
    image.height = height;
    image.image = std::vector<unsigned char>(size);
    memcpy(image.image.data(), data, size);
  }
  
  auto model3 = std::make_shared<vkglTF::Model>();
  model3->loadFromGltfModel( vulkanDevice, m_descriptorManager, model2, queue, 1.0f );
  setupDescriptorSetsForModel( model3 );

  auto result = std::make_unique<CVulkanRendererModelInstance>( this, model->m_modelUri, model3 );
  result->m_model->translation = model->m_model->translation;
  result->m_model->rotation = model->m_model->rotation;
  result->m_model->scale = model->m_model->scale;
  return std::move(result);
}

void VulkanExample::setBoneTexture(IModelInstance *modelInstance, float *boneTexture, size_t numBoneTexture) {
  CVulkanRendererModelInstance *model = (CVulkanRendererModelInstance *)modelInstance;

  size_t numSkins = 0;
  for (size_t i = 0; i < model->m_model->linearNodes.size(); i++) {
    auto &node = model->m_model->linearNodes[i];
    auto &mesh = node->mesh;
    auto &skin = node->skin;
    if (mesh && skin) {
      if (numBoneTexture*sizeof(boneTexture[0]) <= sizeof(mesh->uniformBlock.jointMatrix)) {
        memcpy(mesh->uniformBlock.jointMatrix, boneTexture, numBoneTexture*sizeof(boneTexture[0]));
        /* glm::mat4 jointMat = glm::translate( glm::mat4{1}, glm::vec3(0, 0.2, 0) );
        for (size_t i = 0; i < skin->joints.size(); i++) {
          std::shared_ptr<Node> jointNode = skin->joints[i];
          glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
          jointMat = inverseTransform * jointMat;
          mesh->uniformBlock.jointMatrix[i] = jointMat;
        } */
      } else {
        getOut() << "bad bones size: " << (numBoneTexture*sizeof(boneTexture[0])) << " " << sizeof(mesh->uniformBlock.jointMatrix) << " " << numBoneTexture << std::endl;
        abort();
      }
    }
  }
}

void VulkanExample::resetRenderList()
{
	m_modelsToRender.clear();
}

void VulkanExample::addToRenderList( IModelInstance *modelInstance )
{
  CVulkanRendererModelInstance *model = (CVulkanRendererModelInstance *)modelInstance;
	m_modelsToRender.push_back( model );
}

std::shared_ptr<vkglTF::Model> model;
void VulkanExample::runFrame( bool *shouldQuit, double frameTime )
{
	// bool unusedShouldRender;
	// pumpWindowEvents( shouldQuit, &unusedShouldRender );
	updateFrameTime( frameTime );
}

void VulkanExample::init( HINSTANCE hInstance, IVrManager *vrManager )
{
	m_vrManager = vrManager;

	initVulkan();
	setupWindow( hInstance );
	prepare();
  
  /* std::string uri("data/models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf");
  std::string err;
  std::shared_ptr<vkglTF::Model> model = findOrLoadModel(uri, &err);
  CVulkanRendererModelInstance *vulkanModelInstance = new CVulkanRendererModelInstance( this, uri, model );
	m_modelsToRender.push_back( vulkanModelInstance ); */
}
