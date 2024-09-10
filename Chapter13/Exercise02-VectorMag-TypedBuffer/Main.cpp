//***************************************************************************************
// Chapter 13 Exercise 02: Compute shader that given an array of vectors calculates
//  the length of each vector. These lengths are then output to a file. uses Buffer
//  instead of StructuredBuffr (as in Exercise 01).
// 
// Solution by MadLights.
// 
//***************************************************************************************

#include <array>
#include <fstream>

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

class VecAddCSApp final : public D3DApp
{
public:
    VecAddCSApp(HINSTANCE hInstance);
    VecAddCSApp(const VecAddCSApp& rhs) = delete;
    VecAddCSApp& operator=(const VecAddCSApp& rhs) = delete;
    ~VecAddCSApp();

    virtual bool Initialize()override;

private:
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    void DoComputeWork();

    void BuildBuffers();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildPSOs();
    void BuildFrameResources();

private:
    PassConstants mMainPassCB;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::array<std::unique_ptr<FrameResource>, gNumFrameResources> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvUavHeap = nullptr;

    ComPtr<ID3D12Resource> mInputBuffer = nullptr;
    ComPtr<ID3D12Resource> mInputUploadBufferA = nullptr;
    ComPtr<ID3D12Resource> mOutputBuffer = nullptr;
    ComPtr<ID3D12Resource> mReadBackBuffer = nullptr;

    int mCurrFrameResourceIndex = 0;

    static constexpr int kNumDataElements = 64;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        VecAddCSApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (const DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

VecAddCSApp::VecAddCSApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

VecAddCSApp::~VecAddCSApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool VecAddCSApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildBuffers();
    BuildDescriptorHeaps();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    mInputUploadBufferA = nullptr;

    DoComputeWork();

    return true;
}

void VecAddCSApp::Update(const GameTimer& gt)
{
    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
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
}

void VecAddCSApp::Draw(const GameTimer& gt)
{
    auto& cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
//	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
//		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &transition);

    const auto backBuffView = CurrentBackBufferView();
    const auto dsBuffView = DepthStencilView();

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &backBuffView, true, &dsBuffView);

    mCommandList->ClearRenderTargetView(backBuffView, (float*)&mMainPassCB.FogColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(dsBuffView, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

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


void VecAddCSApp::DoComputeWork()
{
    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSOs["vecLength"].Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvUavHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, descriptorHeaps);

    mCommandList->SetComputeRootSignature(mRootSignature.Get());

    CD3DX12_GPU_DESCRIPTOR_HANDLE heapHandleGPU(
        mSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetComputeRootDescriptorTable(0, heapHandleGPU);
    heapHandleGPU.Offset(mCbvSrvUavDescriptorSize);
    mCommandList->SetComputeRootDescriptorTable(1, heapHandleGPU);

    mCommandList->Dispatch(1, 1, 1);

    // Schedule to copy the data to the default buffer to the readback buffer.
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        mOutputBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    mCommandList->ResourceBarrier(1, &transition);

    mCommandList->CopyResource(mReadBackBuffer.Get(), mOutputBuffer.Get());

    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        mOutputBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    mCommandList->ResourceBarrier(1, &transition);

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait for the work to finish.
    FlushCommandQueue();

    mInputBuffer = nullptr;
    mOutputBuffer = nullptr;
    mSrvUavHeap = nullptr;

    // Map the data so we can read it on CPU.
    float* mappedData = nullptr;
    ThrowIfFailed(mReadBackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

    std::ofstream fout("results.txt");

    for (int i = 0; i < kNumDataElements; ++i)
    {
        const float length = mappedData[i];
        if (length >= 1.0f && length <= 10.0f)
        {
            fout << length << '\n';
        }
        else
        {
            fout << "Length out of range\n";
        }
    }

    mReadBackBuffer->Unmap(0, nullptr);
    mReadBackBuffer = nullptr;
}

void VecAddCSApp::BuildBuffers()
{
    std::array<XMFLOAT3, kNumDataElements> data = {};
    for (auto& v : data)
    {
        XMStoreFloat3(
            &v,
            MathHelper::RandUnitVec3() * MathHelper::RandF(1.0f, 10.0f));
    }

    constexpr UINT64 inputBuffByteSize = data.size() * sizeof(XMFLOAT3);

    // Create some buffers to be used as SRVs.
    mInputBuffer = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        data.data(),
        inputBuffByteSize,
        mInputUploadBufferA);

    // Create the buffer that will be a UAV.
    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    constexpr UINT64 outputBuffByteSize = data.size() * sizeof(float);
    const auto uavBuffDesc = CD3DX12_RESOURCE_DESC::Buffer(
        outputBuffByteSize, 
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uavBuffDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&mOutputBuffer)));

    const CD3DX12_HEAP_PROPERTIES readbackBuffProps(D3D12_HEAP_TYPE_READBACK);
    const auto readbackBuffDesc = CD3DX12_RESOURCE_DESC::Buffer(outputBuffByteSize);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &readbackBuffProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackBuffDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mReadBackBuffer)));
}

void VecAddCSApp::BuildRootSignature()
{
    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[2] = {};

    const CD3DX12_DESCRIPTOR_RANGE srvTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    const CD3DX12_DESCRIPTOR_RANGE uavTable (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    slotRootParameter[0].InitAsDescriptorTable(1, &srvTable);
    slotRootParameter[1].InitAsDescriptorTable(1, &uavTable);

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        2,
        slotRootParameter,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
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

void VecAddCSApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = {};
    srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvUavHeapDesc.NumDescriptors = 2;
    srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvUavHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &srvUavHeapDesc, 
        IID_PPV_ARGS(&mSrvUavHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE heapHandle(
        mSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

    // Create view to input buffer.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = kNumDataElements;
    md3dDevice->CreateShaderResourceView(
        mInputBuffer.Get(),
        &srvDesc,
        heapHandle);

    heapHandle.Offset(mCbvSrvUavDescriptorSize);

    // Create view to output buffer.
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = kNumDataElements;
    md3dDevice->CreateUnorderedAccessView(
        mOutputBuffer.Get(),
        nullptr,
        &uavDesc,
        heapHandle);
}

void VecAddCSApp::BuildShadersAndInputLayout()
{
    mShaders["vecLengthCS"] = d3dUtil::CompileShader(L"Shaders\\VecLength.hlsl", nullptr, "CS", "cs_5_0");
}

void VecAddCSApp::BuildPSOs()
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = mRootSignature.Get();
    computePsoDesc.CS =
    {
        reinterpret_cast<BYTE*>(mShaders["vecLengthCS"]->GetBufferPointer()),
        mShaders["vecLengthCS"]->GetBufferSize()
    };
    computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(md3dDevice->CreateComputePipelineState(
        &computePsoDesc, 
        IID_PPV_ARGS(&mPSOs["vecLength"])));
}

void VecAddCSApp::BuildFrameResources()
{
    for (auto& fr : mFrameResources)
    {
        fr = std::make_unique<FrameResource>(md3dDevice.Get(), 1);
    }
}

