#include "application.hpp"
#include <imgui.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace application {
namespace {
constexpr float pi = 3.14159265358979323846f;
// Column-major matrices; clip depth is Vulkan's [0, 1].
struct Mat4 { float v[16]{}; };
Mat4 identity() { Mat4 m; for(int i=0;i<4;++i) m.v[i*5]=1; return m; }
Mat4 operator*(const Mat4& a,const Mat4& b) {
    Mat4 r;
    for(int c=0;c<4;++c) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) r.v[c*4+row]+=a.v[k*4+row]*b.v[c*4+k];
    return r;
}
Mat4 rotation(int axis,float angle) {
    Mat4 m=identity(); int a=(axis+1)%3,b=(axis+2)%3;
    m.v[a*4+a]=m.v[b*4+b]=std::cos(angle);
    m.v[a*4+b]=std::sin(angle); m.v[b*4+a]=-std::sin(angle); return m;
}
struct Vertex { float position[3]; float color[3]; };
// Square base (y=-0.8), apex (y=1.2). Six triangles, five distinct corners.
constexpr std::array<Vertex,18> vertices{{
    {{-1,-.8f,-1},{.95f,.45f,.16f}}, {{1,-.8f,-1},{.95f,.45f,.16f}}, {{0,1.2f,0},{.95f,.45f,.16f}},
    {{1,-.8f,-1},{.20f,.67f,.87f}}, {{1,-.8f,1},{.20f,.67f,.87f}}, {{0,1.2f,0},{.20f,.67f,.87f}},
    {{1,-.8f,1},{.35f,.80f,.52f}}, {{-1,-.8f,1},{.35f,.80f,.52f}}, {{0,1.2f,0},{.35f,.80f,.52f}},
    {{-1,-.8f,1},{.65f,.40f,.90f}}, {{-1,-.8f,-1},{.65f,.40f,.90f}}, {{0,1.2f,0},{.65f,.40f,.90f}},
    {{-1,-.8f,-1},{.80f,.70f,.30f}}, {{-1,-.8f,1},{.80f,.70f,.30f}}, {{1,-.8f,1},{.80f,.70f,.30f}},
    {{-1,-.8f,-1},{.80f,.70f,.30f}}, {{1,-.8f,1},{.80f,.70f,.30f}}, {{1,-.8f,-1},{.80f,.70f,.30f}}
}};
VkBuffer vertexBuffer{};
VmaAllocation vertexAllocation{};
VkPipeline pipeline{};
VkPipelineLayout pipelineLayout{};
float angles[3]{20,35,0};
float position[3]{};
float scale=1, distance=6;
int projection=0;
void check(VkResult result,const char* message) { if(result!=VK_SUCCESS) throw std::runtime_error(message); }
VkShaderModule loadShader(const char* name) {
    std::ifstream file(std::string(SHADER_DIR)+"/"+name,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error(std::string("Cannot open shader: ")+name);
    auto bytes=file.tellg();
    if(bytes<=0 || bytes%4!=0) throw std::runtime_error("Invalid SPIR-V size");
    std::vector<uint32_t> code(static_cast<size_t>(bytes)/4);
    file.seekg(0); file.read(reinterpret_cast<char*>(code.data()),bytes);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize=static_cast<size_t>(bytes); ci.pCode=code.data();
    VkShaderModule module; check(vkCreateShaderModule(graphics::internal::context.device,&ci,nullptr,&module),"Create shader failed"); return module;
}
Mat4 transform(float aspect) {
    Mat4 s=identity(); s.v[0]=s.v[5]=s.v[10]=scale;
    Mat4 t=identity(); t.v[12]=position[0]; t.v[13]=position[1]; t.v[14]=position[2];
    Mat4 model=t*rotation(2,angles[2]*pi/180)*rotation(1,angles[1]*pi/180)*rotation(0,angles[0]*pi/180)*s;
    Mat4 view=identity(); view.v[14]=-distance;
    const float near=.1f,far=100;
    Mat4 p;
    if(projection==0) {
        float f=1/std::tan(pi/8); // 45 degrees field of view
        p.v[0]=f/aspect; p.v[5]=-f;
        p.v[10]=far/(near-far); p.v[11]=-1; p.v[14]=far*near/(near-far);
    } else {
        float halfHeight=distance*std::tan(pi/8);
        p=identity(); p.v[0]=1/(halfHeight*aspect); p.v[5]=-1/halfHeight;
        p.v[10]=1/(near-far); p.v[14]=near/(near-far);
    }
    return p*view*model;
}
} // namespace

bool initialize() {
    auto& c=graphics::internal::context;
    VkShaderModule vert{},frag{};
    try {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size=sizeof(vertices); bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo ai{}; ai.usage=VMA_MEMORY_USAGE_AUTO;
        ai.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocation{};
        check(vmaCreateBuffer(c.allocator,&bi,&ai,&vertexBuffer,&vertexAllocation,&allocation),"Create vertex buffer failed");
        std::memcpy(allocation.pMappedData,vertices.data(),sizeof(vertices));
        check(vmaFlushAllocation(c.allocator,vertexAllocation,0,VK_WHOLE_SIZE),"Flush vertex buffer failed");
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(Mat4)};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.pushConstantRangeCount=1; li.pPushConstantRanges=&range;
        check(vkCreatePipelineLayout(c.device,&li,nullptr,&pipelineLayout),"Create pipeline layout failed");
        vert=loadShader("pyramid.vert.spv"); frag=loadShader("pyramid.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(auto& stage:stages) { stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName="main"; }
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vert;
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=frag;
        VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding; vi.vertexAttributeDescriptionCount=2; vi.pVertexAttributeDescriptions=attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask=0xf;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount=1; blend.pAttachments=&attachment;
        VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamicStates;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia; pi.pViewportState=&vp; pi.pRasterizationState=&rs; pi.pMultisampleState=&ms; pi.pDepthStencilState=&ds; pi.pColorBlendState=&blend; pi.pDynamicState=&dynamic; pi.layout=pipelineLayout; pi.renderPass=c.render_pass;
        check(vkCreateGraphicsPipelines(c.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"Create graphics pipeline failed");
        vkDestroyShaderModule(c.device,vert,nullptr); vkDestroyShaderModule(c.device,frag,nullptr);
        ImGui::StyleColorsDark(); return true;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        if(vert) vkDestroyShaderModule(c.device,vert,nullptr);
        if(frag) vkDestroyShaderModule(c.device,frag,nullptr);
        shutdown(); return false;
    }
}
void shutdown() {
    auto& c=graphics::internal::context; vkDeviceWaitIdle(c.device);
    if(pipeline) vkDestroyPipeline(c.device,pipeline,nullptr);
    if(pipelineLayout) vkDestroyPipelineLayout(c.device,pipelineLayout,nullptr);
    if(vertexBuffer) vmaDestroyBuffer(c.allocator,vertexBuffer,vertexAllocation);
}
void update([[maybe_unused]] double time) {
    ImGui::SetNextWindowPos(ImVec2(16,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300,320),ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 1 | Variant 6");
    ImGui::TextUnformatted("Square pyramid - C++ / Vulkan");
    ImGui::TextUnformatted("5 corners, 6 triangles");
    ImGui::Separator();
    ImGui::Combo("Projection",&projection,"Perspective\0Orthographic\0");
    ImGui::SliderFloat3("Rotation",angles,-180,180,"%.0f deg");
    ImGui::SliderFloat3("Position",position,-2,2,"%.2f");
    ImGui::SliderFloat("Scale",&scale,.2f,2,"%.2f");
    ImGui::SliderFloat("Distance",&distance,3,12,"%.1f");
    if(ImGui::Button("Reset")) { angles[0]=20; angles[1]=35; angles[2]=0; for(float& x:position)x=0; scale=1; distance=6; projection=0; }
    ImGui::Separator(); ImGui::TextUnformatted("Drag outside this panel to rotate.");
    ImGui::TextUnformatted("Mouse wheel: zoom.");
    ImGui::Text("%.1f FPS",ImGui::GetIO().Framerate);
    ImGui::End();
    auto& io=ImGui::GetIO();
    if(!io.WantCaptureMouse) {
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) { angles[1]+=io.MouseDelta.x*.4f; angles[0]+=io.MouseDelta.y*.4f; }
        distance=std::clamp(distance-io.MouseWheel*.4f,3.f,12.f);
    }
}
void render(const graphics::internal::FrameData& fd) {
    auto& c=graphics::internal::context;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkResetCommandBuffer(fd.command_buffer,0),"Reset command buffer failed");
    check(vkBeginCommandBuffer(fd.command_buffer,&begin),"Begin command buffer failed");
    VkClearValue clear[2]{}; clear[0].color={{.035f,.05f,.085f,1}}; clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rp.renderPass=c.render_pass; rp.framebuffer=fd.framebuffer; rp.renderArea.extent=c.swapchain_extent; rp.clearValueCount=2; rp.pClearValues=clear;
    vkCmdBeginRenderPass(fd.command_buffer,&rp,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0,0,float(c.swapchain_extent.width),float(c.swapchain_extent.height),0,1};
    VkRect2D scissor{{0,0},c.swapchain_extent};
    vkCmdSetViewport(fd.command_buffer,0,1,&vp); vkCmdSetScissor(fd.command_buffer,0,1,&scissor);
    vkCmdBindPipeline(fd.command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    VkDeviceSize offset=0; vkCmdBindVertexBuffers(fd.command_buffer,0,1,&vertexBuffer,&offset);
    Mat4 mvp=transform(float(c.swapchain_extent.width)/c.swapchain_extent.height);
    vkCmdPushConstants(fd.command_buffer,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(mvp),&mvp);
    vkCmdDraw(fd.command_buffer,uint32_t(vertices.size()),1,0,0);
    vkCmdEndRenderPass(fd.command_buffer);
    check(vkEndCommandBuffer(fd.command_buffer),"End command buffer failed");
}
} // namespace application
