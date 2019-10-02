#include "Framework.h"

#include "ShaderSet.h"
#include "Texture.h"
#include "VertexFormats.h"

#include <vector>

// Helper function for aligning particles on 256 thread boundary
s32 align(s32 num, s32 alignment)
{
	return (num + (alignment - 1)) & ~(alignment - 1);
}

class ParticleSystemApp : public FrameworkApp
{
public:
	struct Particle
	{
		v3 m_position;
		f32 m_age;
		v3 m_velocity;
	};

	struct SimulationParameters
	{
		v3 m_emitterLocation;
		f32 m_sigma;
		f32 m_rho;
		f32 m_beta;
		u32 m_particleCount;
	};

	struct PerFrameCBData
	{
		m4x4 m_matView;
		m4x4 m_matProjection;
		v3 m_particleColour;
		f32 m_deltaTime;
		bool m_streak;
	};

	ParticleSystemApp() :
		m_elapsedTime(0.0f),
		m_frameTime(1.0f/60.0f),
		m_speed(1.0f)
	{}
	~ParticleSystemApp();

	void on_init(SystemsInterface& systems) override;
	void on_update(SystemsInterface& systems) override;
	void on_render(SystemsInterface& systems) override;
	void on_resize(SystemsInterface& systems) override 
	{
		systems.pD3DContext->Flush();
	}

private:
	void init_particle_buffers(ID3D11Device* pDevice);
	void init_index_buffer(ID3D11Device* pDevice);

private:
	PerFrameCBData m_perFrameCBData;
	ID3D11Buffer* m_pPerFrame_CB = nullptr;

	SimulationParameters m_simulationParameters;
	ID3D11Buffer* m_pSimulationParameters_CB = nullptr;

	std::vector<Particle> m_OldParticles;
	ID3D11Buffer* m_pOldParticleBuffer = nullptr;
	ID3D11ShaderResourceView* m_pOldParticleBuffer_SRV = nullptr;

	std::vector<Particle> m_UpdatedParticles;
	ID3D11Buffer* m_pUpdatedParticleBuffer = nullptr;	
	ID3D11UnorderedAccessView* m_pUpdatedParticleBuffer_UAV = nullptr;

	std::vector<Particle> m_RenderParticles;
	ID3D11Buffer* m_pRenderParticleBuffer = nullptr;
	ID3D11ShaderResourceView* m_pRenderParticleBuffer_SRV = nullptr;

	std::vector<UINT> m_Indices;
	ID3D11Buffer* m_pIndexBuffer = nullptr;

	ID3D11SamplerState* m_pLinearMipSamplerState = nullptr;

	ID3D11BlendState* m_pAdditiveBlendState = nullptr;
	ID3D11DepthStencilState* m_pDisabledDepthTestState = nullptr;

	ShaderSet m_particleSimulate;
	
	Texture m_texture;

	f32 m_frameTime;
	f32 m_elapsedTime;
	f32 m_speed;
	int m_particleCount;
	bool m_randomColour;
	bool m_streak;
	v3 m_particleColour;

	static constexpr u32 m_maxNumParticles = 500000;
};

// Free up resources
ParticleSystemApp::~ParticleSystemApp()
{
	SAFE_RELEASE(m_pPerFrame_CB);
	SAFE_RELEASE(m_pSimulationParameters_CB);
	SAFE_RELEASE(m_pOldParticleBuffer);
	SAFE_RELEASE(m_pOldParticleBuffer_SRV);
	SAFE_RELEASE(m_pUpdatedParticleBuffer);
	SAFE_RELEASE(m_pUpdatedParticleBuffer_UAV);
	SAFE_RELEASE(m_pRenderParticleBuffer);
	SAFE_RELEASE(m_pRenderParticleBuffer_SRV);
	SAFE_RELEASE(m_pIndexBuffer);
	SAFE_RELEASE(m_pLinearMipSamplerState);
	SAFE_RELEASE(m_pAdditiveBlendState);
	SAFE_RELEASE(m_pDisabledDepthTestState);
}

void ParticleSystemApp::on_init(SystemsInterface& systems)
{
	// Initialize camera setup
	systems.pCamera->eye = v3(-100.0f, 0.0f, -50.0f);
	systems.pCamera->look_at(v3(0.0f, 0.0f, 30.0f));

	// Compile the particle update compute shader
	m_particleSimulate.init(systems.pD3DDevice,
		ShaderSetDesc::Create_CS("Assets/Shaders/ParticleSimulate.fx", "CS_Main"),
		{VertexFormatTraits<Vertex_Pos3fColour4ub>::desc, VertexFormatTraits<Vertex_Pos3fColour4ub>::size}, false);

	// Compile the VS/PS pair for rendering particles
	m_particleSimulate.init(systems.pD3DDevice,
		ShaderSetDesc::Create_VS_PS("Assets/Shaders/ParticleRender.fx", "VS_Main", "PS_Main"),
		{ VertexFormatTraits<Vertex_Pos3fColour4ub>::desc, VertexFormatTraits<Vertex_Pos3fColour4ub>::size }, false);

	// Create a per-frame constant buffer
	PerFrameCBData frameData;
	m_perFrameCBData = frameData;
	m_perFrameCBData.m_particleColour = v3(0.0f, 255.0f, 0.0f);

	// Create a simulation constant buffer and fill with uninitialized data
	SimulationParameters simParams;
	m_simulationParameters = simParams;
	m_simulationParameters.m_sigma = 17.683f;
	m_simulationParameters.m_rho = 25.0f;
	m_simulationParameters.m_beta = 1.6666f;
	m_particleCount = m_maxNumParticles;

	m_speed = 0.5f;

	m_randomColour = true;
	m_streak = true;

	// Create per-frame constant buffers
	m_pPerFrame_CB = create_constant_buffer<PerFrameCBData>(systems.pD3DDevice, &m_perFrameCBData);
	m_pSimulationParameters_CB = create_constant_buffer<SimulationParameters>(systems.pD3DDevice, &m_simulationParameters);
	
	// Prepare structured buffers containing particle data
	init_particle_buffers(systems.pD3DDevice);

	// Create an SRV to the updated particle buffer for the vertex shader
	m_pRenderParticleBuffer_SRV = create_structured_buffer_SRV(systems.pD3DDevice, m_maxNumParticles, m_pRenderParticleBuffer);

	// Create an SRV for the compute shader to read last frame's particles
	m_pOldParticleBuffer_SRV = create_structured_buffer_SRV(systems.pD3DDevice, m_maxNumParticles, m_pOldParticleBuffer);

	// Create a UAV for the compute shader to write updated particle data
	m_pUpdatedParticleBuffer_UAV = create_structured_buffer_UAV(systems.pD3DDevice, m_maxNumParticles, m_pUpdatedParticleBuffer);

	// Create index buffer for rendering particles
	init_index_buffer(systems.pD3DDevice);

	// Get some textures
	m_texture.init_from_image(systems.pD3DDevice, "Assets/Textures/particle.png", false);
	// Set the sampler state
	m_pLinearMipSamplerState = create_basic_sampler(systems.pD3DDevice, D3D11_TEXTURE_ADDRESS_WRAP);

	// Create the blend state for additive blending
	D3D11_BLEND_DESC blendDesc;
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	
	HRESULT hr = systems.pD3DDevice->CreateBlendState(&blendDesc, &m_pAdditiveBlendState);
	ASSERT(!FAILED(hr) && m_pAdditiveBlendState);

	// Create the depth stencil state for disabled depth buffer
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depthStencilDesc.StencilEnable = false;
	depthStencilDesc.StencilWriteMask = 0xFF;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	hr = systems.pD3DDevice->CreateDepthStencilState(&depthStencilDesc, &m_pDisabledDepthTestState);
	ASSERT(!FAILED(hr) && m_pDisabledDepthTestState);

	// Set the depth stencil state
	systems.pD3DContext->OMSetDepthStencilState(m_pDisabledDepthTestState, 0);
}

void ParticleSystemApp::on_update(SystemsInterface& systems)
{

	float updatedTime = static_cast<float>(getTimeSeconds());
	m_frameTime = updatedTime - m_elapsedTime;
	m_elapsedTime = updatedTime;

	// Update simulation parameters
	// Set emission point to be a random point within a sphere
	v2 polars = DirectX::XM_2PI*randv2();
	m_simulationParameters.m_emitterLocation = 1.0f*v3(sinf(polars.x)*cosf(polars.y), sinf(polars.x)*sinf(polars.y), cosf(polars.x));
	ImGui::Text("Frame time: %.0f ms", 1000.0f*m_frameTime);
	ImGui::Text("FPS: %.0f", (1.0f / m_frameTime));
	
	ImGui::SliderInt("Particle Count", &m_particleCount, 0, static_cast<int>(m_maxNumParticles), "%.0f");
	
	ImGui::SliderFloat("Sigma", (f32*)(&m_simulationParameters.m_sigma), 0.0f, 100.0f);
	ImGui::SliderFloat("Rho", (f32*)(&m_simulationParameters.m_rho), 0.0f, 100.0f);
	ImGui::SliderFloat("Beta", (f32*)(&m_simulationParameters.m_beta), 0.0f, 30.0f);
	ImGui::SliderFloat("Speed", (f32*)&m_speed, 0.01f, 1.0f);
	ImGui::Checkbox("Random Particle Colour", &m_randomColour);
	ImGui::Checkbox("Streaks", &m_streak);

	DemoFeatures::editorHud(systems.pDebugDrawContext);

	if (m_randomColour)
	{
		m_particleColour = (255.0f/2.0f)*(randv3()+v3(1.0f,1.0f,1.0f));
		m_randomColour = false;
	}
	else
	{
		ImGui::SliderFloat("R", (f32*)&m_particleColour.x, 0.0f, 255.0f);
		ImGui::SliderFloat("G", (f32*)&m_particleColour.y, 0.0f, 255.0f);
		ImGui::SliderFloat("B", (f32*)&m_particleColour.z, 0.0f, 255.0f);
	}

	// Update per-frame data
	m_perFrameCBData.m_matProjection = systems.pCamera->projMatrix.Transpose();
	m_perFrameCBData.m_matView = systems.pCamera->viewMatrix.Transpose();
	m_perFrameCBData.m_deltaTime = m_frameTime * m_speed;
	m_perFrameCBData.m_particleColour = m_particleColour;
	m_perFrameCBData.m_streak = m_streak;

	// Bind compute shader
	m_particleSimulate.bind(systems.pD3DContext);

	// Push per-frame data to the GPU
	push_constant_buffer(systems.pD3DContext, m_pPerFrame_CB, m_perFrameCBData);
	push_constant_buffer(systems.pD3DContext, m_pSimulationParameters_CB, m_simulationParameters);

	// Bind SRVs to compute shader
	ID3D11ShaderResourceView* arr_pSRVs[] = { m_pOldParticleBuffer_SRV };
	systems.pD3DContext->CSSetShaderResources(0, 1, arr_pSRVs);

	// Bind UAVs to compute shader
	ID3D11UnorderedAccessView* arr_pUAVs[] = { m_pUpdatedParticleBuffer_UAV};
	systems.pD3DContext->CSSetUnorderedAccessViews(0, 1, arr_pUAVs, nullptr);

	// Bind constant buffer to compute shader
	ID3D11Buffer* arr_pCBs[] = { m_pPerFrame_CB, m_pSimulationParameters_CB };
	systems.pD3DContext->CSSetConstantBuffers(0, 2, arr_pCBs);

	// Launch 1D thread groups, one thread per particle
	u32 numThreads = align(m_particleCount, 256);
	systems.pD3DContext->Dispatch( numThreads/256, 1, 1);

	// Set the updated particles to be next frame's old particles
	systems.pD3DContext->CopyResource(m_pOldParticleBuffer, m_pUpdatedParticleBuffer);
	// Obtain a copy of the updated particles to render
	systems.pD3DContext->CopyResource(m_pRenderParticleBuffer, m_pUpdatedParticleBuffer);

	// Unbind SRVs from compute shader
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	systems.pD3DContext->CSSetShaderResources(0, 1, nullSRVs);

	// Unbind UAVS from compute shader
	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	systems.pD3DContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
}

void ParticleSystemApp::on_render(SystemsInterface& systems)
{
	// Bind constant buffers to vertex and pixel shaders
	ID3D11Buffer* cbuffers[] = { m_pPerFrame_CB };
	systems.pD3DContext->VSSetConstantBuffers(2, 1, cbuffers);
	systems.pD3DContext->PSSetConstantBuffers(2, 1, cbuffers);


	// Bind particle buffer SRV to vertex shader
	ID3D11ShaderResourceView* arr_pSRVs[] = { m_pRenderParticleBuffer_SRV };
	systems.pD3DContext->VSSetShaderResources(1, 1, arr_pSRVs);

	// Bind a texture to pixel shader
	m_texture.bind(systems.pD3DContext, ShaderStage::kPixel, 2);

	// Bind a sampler state to pixel shader
	ID3D11SamplerState* samplers[] = { m_pLinearMipSamplerState };
	systems.pD3DContext->PSSetSamplers(0, 1, samplers);

	// Set the blend state
	systems.pD3DContext->OMSetBlendState(m_pAdditiveBlendState, nullptr, 0xFFFFFFFF);

	// Set the index buffer
	systems.pD3DContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set the primitive topology
	systems.pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Draw the particles
	systems.pD3DContext->DrawIndexed(m_particleCount*6, 0, 0);

	// Unbind shader resources
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	systems.pD3DContext->VSSetShaderResources(1, 1, nullSRVs);
}

void ParticleSystemApp::init_particle_buffers(ID3D11Device* pDevice)
{
	// Allocate an array that contains initial particle data
	m_OldParticles.resize(m_maxNumParticles);

	// Write in some initial particle data
	for (u32 i = 0; i < m_maxNumParticles; ++i)
	{
		m_OldParticles[i].m_position = 10.0f*randv3();
		m_OldParticles[i].m_velocity = randv3();
		m_OldParticles[i].m_age = 20.0f*(randf()+1.0f)/2.0f;
	}

	// Copy the array into a subresource
	D3D11_SUBRESOURCE_DATA particleData;
	particleData.pSysMem = m_OldParticles.data();
	particleData.SysMemPitch = 0;
	particleData.SysMemSlicePitch = 0;

	// Create a structured buffer for old particle data
	m_pOldParticleBuffer = create_default_structured_buffer<Particle>(pDevice, m_maxNumParticles, &particleData);

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	m_UpdatedParticles.resize(m_maxNumParticles);

	// Fill updated particles with any data, it will be overwritten by GPU
	// Write in some initial particle data
	for (u32 i = 0; i < m_maxNumParticles; ++i)
	{
		m_UpdatedParticles[i].m_position = 5.0f*randv3();
		m_UpdatedParticles[i].m_velocity = v3(0.0f);
		m_UpdatedParticles[i].m_age = 20.0f*(randf() + 1.0f) / 2.0f;
	}
	
	// Copy the array into a subresource
	D3D11_SUBRESOURCE_DATA particleData2;
	particleData2.pSysMem = m_UpdatedParticles.data();
	particleData2.SysMemPitch = 0;
	particleData2.SysMemSlicePitch = 0;

	m_pUpdatedParticleBuffer = create_default_structured_buffer<Particle>(pDevice, m_maxNumParticles, &particleData2);

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	m_RenderParticles.resize(m_maxNumParticles);

	// Fill updated particles with any data, it will be overwritten by GPU
	// Write in some initial particle data
	for (u32 i = 0; i < m_maxNumParticles; ++i)
	{
		m_RenderParticles[i].m_position = 5.0f*randv3();
		m_RenderParticles[i].m_velocity = v3(0.0f);
		m_RenderParticles[i].m_age = 20.0f*(randf() + 1.0f) / 2.0f;
	}

	// Copy the array into a subresource
	D3D11_SUBRESOURCE_DATA particleData3;
	particleData3.pSysMem = m_RenderParticles.data();
	particleData3.SysMemPitch = 0;
	particleData3.SysMemSlicePitch = 0;

	m_pRenderParticleBuffer = create_default_structured_buffer<Particle>(pDevice, m_maxNumParticles, &particleData3);
}

void ParticleSystemApp::init_index_buffer(ID3D11Device* pDevice)
{
	ID3D11Buffer* pIndexBuffer;
	
	// Create data to fill index buffer
	m_Indices.resize(6*m_maxNumParticles);

	for (UINT i = 0; i < m_maxNumParticles; ++i)
	{
		m_Indices[6 * i] = 4 * i;
		m_Indices[6 * i + 1] = 4 * i + 1;
		m_Indices[6 * i + 2] = 4 * i + 2;
		m_Indices[6 * i + 3] = 4 * i;
		m_Indices[6 * i + 4] = 4 * i + 2;
		m_Indices[6 * i + 5] = 4 * i + 3;
	}

	// Create an index buffer description
	D3D11_BUFFER_DESC desc;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.ByteWidth = m_maxNumParticles * 6 * sizeof(UINT);
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.Usage = D3D11_USAGE_IMMUTABLE;

	// Create subresource data
	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = m_Indices.data();
	data.SysMemPitch = 0;
	data.SysMemSlicePitch = 0;

	// Create the buffer and assign to member pointer
	HRESULT hr = pDevice->CreateBuffer(&desc, &data, &pIndexBuffer);
	
	if(!FAILED(hr) && pIndexBuffer)
		m_pIndexBuffer = pIndexBuffer;
}


ParticleSystemApp g_particleApp;

FRAMEWORK_IMPLEMENT_MAIN(g_particleApp, "Particle System")