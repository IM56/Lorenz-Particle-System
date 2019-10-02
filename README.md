# Lorenz-Particle-System
A simple GPU particle system written in DirectX 11/HLSL and C++.
The framework library is based on a debug-draw demo by Guilherme R. Lampert and was modified by myself and David Moore.
The particles trajectories trace out Lorenz attractors.

<h2>Getting started</h2>
<p>To use this application, you can build the project from source. Please ensure that the Framework static library is built first 
and linked to the ParticleSystem project. If you are using Visual Studio, please add Framework to the 'Additional Include Directories' 
so that the application has access to the necessary header files.</p>

<h2>What does it do?</h2>
<p>Using the <b>DirectCompute</b> capabilities of DirectX 11, this application stores, updates and renders a particle system 
entirely on the GPU. Particle billboards are dynamically generated in the vertex shader without the use of a geometry shader.</p>

<p>Whilst the number of particles in the system can be edited by the user, the maximum number of particles is set to 0.5M by default.
This can be edited in the source code by changing the variable <code>ParticleSystemApp::m_maxNumParticles</code> - the application has been
shown to handle at least 3M particles at once.</p>

<h2>Camera controls</h2>
<p>The user can move the camera's line of sight by holding right-click and moving the mouse. Whilst right-click is held down, the user can also strafe left (A key), strafe right (D key) and zoom in (W key) and zoom out (S key).</p>
