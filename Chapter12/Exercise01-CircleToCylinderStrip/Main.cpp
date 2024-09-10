//***************************************************************************************
// Chapter 12 Exercise 01: Transform a circle line strip into a cylinder using the
//  geometry shader.
// 
// Solution by MadLights.
// 
// Hold down rigt mouse button to zoom in/out.
//***************************************************************************************

#include <algorithm>
#include <array>
#include <vector>
#include <string>

#include "../../Common/DDSTextureLoader.h"
#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

constexpr int gNumFrameResources = 3;

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Color;
};

class CircleToCylinder final : public D3DApp
{
public:
    CircleToCylinder(HINSTANCE hInstance);
    CircleToCylinder(const CircleToCylinder& rhs) = delete;
    CircleToCylinder& operator=(const CircleToCylinder& rhs) = delete;
    ~CircleToCylinder();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void UpdateCamera(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();

private:
    static constexpr int mkCircleDivisions = 128;
    static constexpr int mkCircleVertexCount = mkCircleDivisions + 1;

	std::array<std::unique_ptr<FrameResource>, 3> mFrameResources = {};
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;
	
	ComPtr<ID3D12Resource> mVertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> mVertexUploaderGPU = nullptr;

	D3D12_VERTEX_BUFFER_VIEW mVertexBuffView = {};

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::array<D3D12_INPUT_ELEMENT_DESC, 2> mInputLayout = {};

    ComPtr<ID3D12PipelineState> mPSO = nullptr;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.3f*XM_PI;
	float mPhi = 0.4f*XM_PI;
	float mRadius = 2.5f;

	POINT mLastMousePos = {};
};

int WINAPI WinMain(
	_In_ const HINSTANCE hInstance, 
	_In_opt_ const HINSTANCE prevInstance,
    _In_ const PSTR cmdLine, 
	_In_ const int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        CircleToCylinder theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(const DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

CircleToCylinder::CircleToCylinder(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

CircleToCylinder::~CircleToCylinder()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool CircleToCylinder::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();
    
    // Releases the upload buffer.
    mVertexUploaderGPU = nullptr;

    return true;
}
 
void CircleToCylinder::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMStoreFloat4x4(
		&mProj, 
		XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f));
}

void CircleToCylinder::Update(const GameTimer& gt)
{
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		if (!eventHandle) [[unlikely]]
		{
			std::abort();
		}
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	UpdateMainPassCB(gt);
}

void CircleToCylinder::Draw(const GameTimer& gt)
{
    auto& cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, 
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &transition);

    // Clear the back buffer and depth buffer.
	const auto backBuffView = CurrentBackBufferView();
	const auto depthBuffView = DepthStencilView();

    mCommandList->ClearRenderTargetView(backBuffView, Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(
		depthBuffView, 
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 
		1.0f, 
		0, 
		0, 
		nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &backBuffView, true, &depthBuffView);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(
        0, 
        passCB->GetGPUVirtualAddress());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBuffView);
    mCommandList->IASetIndexBuffer(nullptr);
    mCommandList->DrawInstanced(mkCircleVertexCount, 1, 0, 0);

    // Indicate a state transition on the resource usage.
	transition = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &transition);

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CircleToCylinder::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void CircleToCylinder::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void CircleToCylinder::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = std::clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = std::clamp(mRadius, 2.5f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
 
void CircleToCylinder::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	const XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	static const XMVECTOR target = XMVectorZero();
	static const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}


void CircleToCylinder::UpdateMainPassCB(const GameTimer& gt)
{
	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	mCurrFrameResource->PassCB->CopyData(0, mMainPassCB);
}


void CircleToCylinder::BuildRootSignature()
{
    // Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[1] = {};

	// Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		1u, 
		slotRootParameter,
		0u, 
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}


void CircleToCylinder::BuildShadersAndInputLayout()
{
	mShaders["cylinderVS"] = d3dUtil::CompileShader(
		L"Shaders\\CircleToCylinder.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["cylinderGS"] = d3dUtil::CompileShader(
		L"Shaders\\CircleToCylinder.hlsl", nullptr, "GS", "gs_5_0");
	mShaders["cylinderPS"] = d3dUtil::CompileShader(
		L"Shaders\\CircleToCylinder.hlsl", nullptr, "PS", "ps_5_0");
	
	mInputLayout[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    mInputLayout[1] = { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
}

void CircleToCylinder::BuildShapeGeometry()
{
	constexpr float kTheta = XM_2PI / mkCircleDivisions;
	
	std::array<Vertex, mkCircleVertexCount> vertices = {};

    constexpr XMFLOAT3 kRed(1.0f, 0.0f, 0.0f);
	for (int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].Pos.x = std::cos(i * kTheta);
		vertices[i].Pos.y = 0.0f;
		vertices[i].Pos.z = std::sin(i * kTheta);
        vertices[i].Color = kRed;
	}
	
	constexpr UINT vbSizeBytes = UINT(sizeof(Vertex) * vertices.size());

	mVertexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		vertices.data(),
		vbSizeBytes,
		mVertexUploaderGPU);

	mVertexBuffView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
	mVertexBuffView.SizeInBytes = vbSizeBytes;
	mVertexBuffView.StrideInBytes = sizeof(Vertex);
}

void CircleToCylinder::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC cylinderPSODesc = {};
	cylinderPSODesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	cylinderPSODesc.pRootSignature = mRootSignature.Get();
	cylinderPSODesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["cylinderVS"]->GetBufferPointer()), 
		mShaders["cylinderVS"]->GetBufferSize()
	};
    cylinderPSODesc.GS =
    {
        reinterpret_cast<BYTE*>(mShaders["cylinderGS"]->GetBufferPointer()),
        mShaders["cylinderGS"]->GetBufferSize()
    };
	cylinderPSODesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["cylinderPS"]->GetBufferPointer()),
		mShaders["cylinderPS"]->GetBufferSize()
	};
	cylinderPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	cylinderPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	cylinderPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	cylinderPSODesc.SampleMask = UINT_MAX;
	cylinderPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	cylinderPSODesc.NumRenderTargets = 1;
	cylinderPSODesc.RTVFormats[0] = mBackBufferFormat;
	cylinderPSODesc.SampleDesc = { 1, 0 };
	cylinderPSODesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
        &cylinderPSODesc, 
        IID_PPV_ARGS(&mPSO)));
}

void CircleToCylinder::BuildFrameResources()
{
    for(auto& fr : mFrameResources)
    {
        fr = std::make_unique<FrameResource>(md3dDevice.Get(), 1);
    }
}
