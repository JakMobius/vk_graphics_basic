#include "shadowmap_render.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>
#include <iostream>

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan_core.h>


/// RESOURCE ALLOCATION

void SimpleShadowmapRender::AllocateResources()
{
  gBufferDepths = m_context->createImage(etna::Image::CreateInfo{
    .extent     = vk::Extent3D{ m_width, m_height, 1 },
    .name       = "g_buffer_depths",
    .format     = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment });

  gBufferCoords = m_context->createImage(etna::Image::CreateInfo{
    .extent     = vk::Extent3D{ m_width, m_height, 1 },
    .name       = "g_buffer_coords",
    .format     = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled });

  gBufferNormals = m_context->createImage(etna::Image::CreateInfo{
    .extent     = vk::Extent3D{ m_width, m_height, 1 },
    .name       = "g_buffer_normals",
    .format     = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled });

  shadowMap = m_context->createImage(etna::Image::CreateInfo{
    .extent     = vk::Extent3D{ 2048, 2048, 1 },
    .name       = "shadow_map",
    .format     = vk::Format::eD16Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled });

  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{ .name = "default_sampler" });
  constants      = m_context->createBuffer(etna::Buffer::CreateInfo{
         .size        = sizeof(UniformParams),
         .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
         .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
         .name        = "constants" });

  m_uboMappedMem = constants.map();
}

void SimpleShadowmapRender::LoadScene(const char *path, bool transpose_inst_matrices)
{
  m_pScnMgr->LoadSceneXML(path, transpose_inst_matrices);

  // TODO: Make a separate stage
  loadShaders();
  PreparePipelines();

  auto loadedCam = m_pScnMgr->GetCamera(0);
  m_cam.fov      = loadedCam.fov;
  m_cam.pos      = float3(loadedCam.pos);
  m_cam.up       = float3(loadedCam.up);
  m_cam.lookAt   = float3(loadedCam.lookAt);
  m_cam.tdist    = loadedCam.farPlane;
}

void SimpleShadowmapRender::DeallocateResources()
{
  gBufferDepths.reset();// TODO: Make an etna method to reset all the resources
  gBufferCoords.reset();
  gBufferNormals.reset();
  shadowMap.reset();
  m_swapchain.Cleanup();
  vkDestroySurfaceKHR(GetVkInstance(), m_surface, nullptr);

  constants = etna::Buffer();
}


/// PIPELINES CREATION

void SimpleShadowmapRender::PreparePipelines()
{
  // create full screen quad for debug purposes
  //
  m_pFSQuad = std::make_shared<vk_utils::QuadRenderer>(0, 0, 512, 512);
  m_pFSQuad->Create(m_context->getDevice(),
    VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad3_vert.vert.spv",
    VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad.frag.spv",
    vk_utils::RenderTargetInfo2D{
      .size          = VkExtent2D{ m_width, m_height },// this is debug full screen quad
      .format        = m_swapchain.GetFormat(),
      .loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD,// seems we need LOAD_OP_LOAD if we want to draw quad to part of screen
      .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
  SetupSimplePipeline();
}

void SimpleShadowmapRender::loadShaders()
{
  etna::create_program("g_buffer_pass",
    { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/gbuffer.frag.spv",
      VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
  etna::create_program("shadowmap_pass",
    { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple_shadow.frag.spv",
      VK_GRAPHICS_BASIC_ROOT "/resources/shaders/gbuffer.vert.spv" });
  etna::create_program("simple_shadow", { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
}

void SimpleShadowmapRender::SetupSimplePipeline()
{
  std::vector<std::pair<VkDescriptorType, uint32_t>> dtypes = {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }
  };

  m_pBindings = std::make_shared<vk_utils::DescriptorMaker>(m_context->getDevice(), dtypes, 2);

  m_pBindings->BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
  m_pBindings->BindImage(0, shadowMap.getView({}), defaultSampler.get(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  m_pBindings->BindEnd(&m_quadDS, &m_quadDSLayout);

  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = { etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = m_pScnMgr->GetVertexStreamDescription() } }
  };

  auto &pipelineManager   = etna::get_context().getPipelineManager();
  m_gPassPipeline         = pipelineManager.createGraphicsPipeline("g_buffer_pass",
            { .vertexShaderInput = sceneVertexInputDesc,
              .blendingConfig    = {
                   .attachments = {
          vk::PipelineColorBlendAttachmentState{
                       .blendEnable = false,
            // Which color channels should we write to?
                       .colorWriteMask = vk::ColorComponentFlagBits::eR
                              | vk::ColorComponentFlagBits::eG
                              | vk::ColorComponentFlagBits::eB
                              | vk::ColorComponentFlagBits::eA },
          vk::PipelineColorBlendAttachmentState{
                       .blendEnable = false,
            // Which color channels should we write to?
                       .colorWriteMask = vk::ColorComponentFlagBits::eR
                              | vk::ColorComponentFlagBits::eG
                              | vk::ColorComponentFlagBits::eB
                              | vk::ColorComponentFlagBits::eA } } },
              .fragmentShaderOutput = { .colorAttachmentFormats = {
                                  vk::Format::eR32G32B32A32Sfloat,
                                  vk::Format::eR32G32B32A32Sfloat,
                                },
                .depthAttachmentFormat = vk::Format::eD32Sfloat } });
  m_lightningPassPipeline = pipelineManager.createGraphicsPipeline("shadowmap_pass",
    { .vertexShaderInput    = etna::VertexShaderInputDescription{ .bindings = {} },
      .fragmentShaderOutput = {
        .colorAttachmentFormats = { static_cast<vk::Format>(m_swapchain.GetFormat()) },
        .depthAttachmentFormat  = vk::Format::eD32Sfloat } });
  m_shadowPipeline        = pipelineManager.createGraphicsPipeline("simple_shadow",
           { .vertexShaderInput    = sceneVertexInputDesc,
             .fragmentShaderOutput = {
               .depthAttachmentFormat = vk::Format::eD16Unorm } });
  //
}

void SimpleShadowmapRender::DestroyPipelines()
{
  m_pFSQuad = nullptr;// smartptr delete it's resources
}


/// COMMAND BUFFER FILLING

void SimpleShadowmapRender::DrawSceneCmd(VkCommandBuffer a_cmdBuff, const float4x4 &a_wvp, VkPipelineLayout a_pipelineLayout)
{
  VkShaderStageFlags stageFlags = (VK_SHADER_STAGE_VERTEX_BIT);

  VkDeviceSize zero_offset = 0u;
  VkBuffer vertexBuf       = m_pScnMgr->GetVertexBuffer();
  VkBuffer indexBuf        = m_pScnMgr->GetIndexBuffer();

  vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
  vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

  pushConst2M.projView = a_wvp;
  for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
  {
    auto inst         = m_pScnMgr->GetInstanceInfo(i);
    pushConst2M.model = m_pScnMgr->GetInstanceMatrix(i);
    vkCmdPushConstants(a_cmdBuff, a_pipelineLayout, stageFlags, 0, sizeof(pushConst2M), &pushConst2M);

    auto mesh_info = m_pScnMgr->GetMeshInfo(inst.mesh_id);
    vkCmdDrawIndexed(a_cmdBuff, mesh_info.m_indNum, 1, mesh_info.m_indexOffset, mesh_info.m_vertexOffset, 0);
  }
}

void SimpleShadowmapRender::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff, VkImage a_targetImage, VkImageView a_targetImageView)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

  //// draw scene to shadowmap
  //
  {
    etna::RenderTargetState renderTargets(a_cmdBuff, { 2048, 2048 }, {}, shadowMap);

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline.getVkPipeline());
    DrawSceneCmd(a_cmdBuff, m_lightMatrix, m_shadowPipeline.getVkPipelineLayout());
  }

  //// g_buffer pass
  //
  {
    etna::RenderTargetState renderTargets(
      a_cmdBuff,
      { m_width, m_height },
      {
        { gBufferCoords.get(), gBufferCoords.getView({}) },
        { gBufferNormals.get(), gBufferNormals.getView({}) },
      },
      gBufferDepths);

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gPassPipeline.getVkPipeline());
    DrawSceneCmd(a_cmdBuff, m_worldViewProj, m_gPassPipeline.getVkPipelineLayout());
  }

  //// draw final scene to screen
  //
  {
    auto lightningPassInfoInfo = etna::get_shader_program("shadowmap_pass");

    VkDescriptorSet vkSet = etna::create_descriptor_set(
      lightningPassInfoInfo.getDescriptorLayoutId(0),
      a_cmdBuff,
      {
        etna::Binding{ 0, constants.genBinding() },
        etna::Binding{ 1, shadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) },
        etna::Binding{ 2, gBufferNormals.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) },
        etna::Binding{ 3, gBufferCoords.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) },

      })
                              .getVkSet();

    etna::RenderTargetState renderTargets(
      a_cmdBuff,
      { m_width, m_height },
      { { a_targetImage, a_targetImageView } },
      gBufferDepths);

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightningPassPipeline.getVkPipeline());
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightningPassPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);
    vkCmdDraw(a_cmdBuff, 6, 1, 0, 0);
  }

  if (m_input.drawFSQuad)
  {
    float scaleAndOffset[4] = { 0.5f, 0.5f, -0.5f, +0.5f };
    m_pFSQuad->SetRenderTarget(a_targetImageView);
    m_pFSQuad->DrawCmd(a_cmdBuff, m_quadDS, scaleAndOffset);
  }

  etna::set_state(a_cmdBuff, a_targetImage, vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlags2(), vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor);

  etna::finish_frame(a_cmdBuff);

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}
