// Particle data 
struct Particle
{
	float3 position;
	float age;
	float3 velocity;
};

cbuffer PerFrameCBData : register(b0)
{
	matrix matView;
	matrix matProjection;
	float3 currentColour;
	float deltaTime;
	bool streak;
};

cbuffer SimulationParameters : register(b1)
{
	float3 emitterLocation;
	float sigma;
	float rho;
	float beta;
	int particleCount;
};

StructuredBuffer<Particle> OldParticles : register(t0);
RWStructuredBuffer<Particle> UpdatedParticles : register(u0);



////////////////////
// COMPUTE SHADER //
////////////////////

// Dispatch 256 threads per thread group = 256 particles
[numthreads(256, 1, 1)]

void CS_Main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
	uint myID = DispatchThreadID.x;

	// Prevent undefined behaviour by not utilising more threads than particles
	if (myID <= particleCount)
	{
		// Read an particle from the old buffer
		Particle p = OldParticles[myID];

		// Update its data according to Lorentz dynamical system
		float x = p.position.x;
		float y = p.position.y;
		float z = p.position.z;
		p.velocity = float3(sigma*(y - x), x*(rho - z) - y, x*y - beta * z);
		p.position = p.position + deltaTime * p.velocity;
		p.age += deltaTime;

		// Place the particle in the updated buffer
		UpdatedParticles[myID] = p;
	}
}
