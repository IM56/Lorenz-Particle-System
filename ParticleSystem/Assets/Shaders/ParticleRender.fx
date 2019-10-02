// Particle data
struct Particle
{
	float3 position;
	float age;
	float3 velocity;
};


cbuffer PerFrameCBData : register(b2)
{
	matrix matView;
	matrix matProjection;
	float3 currentColour;
	float deltaTime;
	bool streaksOn;
};


StructuredBuffer<Particle> ParticleBuffer : register(t1);
Texture2D texture0 : register(t2);
SamplerState linearMipSampler : register(s0);


struct PS_Input
{
	float4 vpos : SV_Position;
	float4 colour : COLOR;
	float2 uv : TEXCOORD0;
};

static const float3 Billboard[] =
{
	float3(-1.0f, -1.0f, 0.0f), // Bottom-left corner
	float3(-1.0f, 1.0f, 0.0f),	// Top-left corner
	float3(1.0f, 1.0f, 0.0f),	// Top-right corner
	float3(1.0f, -1.0f, 0.0f)	// Bottom-right corner
};

static const float2 UVs[] =
{
	float2(0.0f, 1.0f),		// Bottom-left 
	float2(0.0f, 0.0f),		// Top-left
	float2(1.0f, 0.0f),		// Top-right
	float2(1.0f, 1.0f)		// Bottom-right
};

///////////////////////
// Vertex shader
///////////////////////
PS_Input VS_Main(uint vertexID : SV_VertexID)
{
	PS_Input output;

	uint particleID = vertexID / 4;
	uint cornerID = vertexID % 4;

	Particle p = ParticleBuffer[particleID];
	float3 position = p.position;

	float3 view_space_velocity = mul(float4(p.velocity, 0.0f), matView).xyz;
	// Find the distance of the particle from the camera to adjust the particle's size
	float4 view_space_pos = mul(float4(position, 1.0f), matView);
	float size = 5.0f / (length(view_space_pos.xyz) + 0.1f);

	// Produce a quad corner vertex in view space
	float3 corner_pos = Billboard[cornerID] * size;
	float3 streak = dot(normalize(view_space_velocity), normalize(corner_pos))*view_space_velocity;
	view_space_pos.xyz += 3.0f * corner_pos + size * streaksOn * streak * 50.0f * deltaTime;

	// Send the vertex to screen space
	output.vpos = mul(view_space_pos, matProjection);
	output.colour = float4(currentColour/255.0f, 1.0f);
	output.uv = UVs[cornerID];

	return output;
}


////////////////////////
// Pixel shader
////////////////////////
float4 PS_Main(PS_Input input) : SV_Target
{
	float4 materialColour = texture0.Sample(linearMipSampler, input.uv);

	return materialColour*input.colour;
}