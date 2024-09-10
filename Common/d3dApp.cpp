// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "d3dApp.h"

#include <string>
#include <format>

#include <WindowsX.h>

#include "d3dUtil.h"

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

namespace {
    LRESULT CALLBACK
    MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Forward hwnd on because we can get messages (e.g., WM_CREATE)
        // before CreateWindow returns, and thus before mhMainWnd is valid.
        return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
    }
}

D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp()
{
    return mApp;
}

D3DApp::D3DApp(const HINSTANCE hInstance)
    : mhAppInst(hInstance)
{
    // Only one D3DApp can be constructed.
    assert(mApp == nullptr);
    mApp = this;
}

D3DApp::~D3DApp()
{
    if (md3dDevice != nullptr)
    {
        FlushCommandQueue();
    }
}

HINSTANCE D3DApp::AppInst()const
{
    return mhAppInst;
}

HWND D3DApp::MainWnd()const
{
    return mhMainWnd;
}

float D3DApp::AspectRatio()const
{
    return static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight);
}


int D3DApp::Run()
{
    MSG msg = { 0 };

    mTimer.Reset();

    while (msg.message != WM_QUIT) [[likely]]
    {
        // If there are Window messages then process them.
        if (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Otherwise, do animation/game stuff.
        else
        {
            mTimer.Tick();

            if (!mAppPaused) [[likely]]
            {
                CalculateFrameStats();
                Update(mTimer);
                Draw(mTimer);
            }
            else
            {
                Sleep(100);
            }
        }
    }

    return static_cast<int>(msg.wParam);
}

bool D3DApp::Initialize()
{
    if (!InitMainWindow())
        return false;

    if (!InitDirect3D())
        return false;

    // Do the initial resize code.
    OnResize();

    return true;
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

    constexpr D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        .NumDescriptors = 1,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0
    };
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void D3DApp::OnResize()
{
    assert(md3dDevice);
    assert(mSwapChain);
    assert(mDirectCmdListAlloc);

    // Flush before changing any resources.
    FlushCommandQueue();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Release the previous resources we will be recreating.
    for (int i = 0; i < SwapChainBufferCount; ++i)
    {
        mSwapChainBuffer[i].Reset();
    }
    mDepthStencilBuffer.Reset();

    // Resize the swap chain.
    ThrowIfFailed(mSwapChain->ResizeBuffers(
        SwapChainBufferCount,
        mClientWidth, mClientHeight,
        mBackBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    mCurrBackBufferIndex = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; ++i)
    {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
        md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, mRtvDescriptorSize);
    }

    // Create the depth/stencil buffer and view.
    // Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
    // the depth buffer.  Therefore, because we need to create two views to the same resource:
    //   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
    //   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
    // we need to create the depth buffer resource with a typeless format.  
    const D3D12_RESOURCE_DESC depthStencilDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = static_cast<UINT64>(mClientWidth),
        .Height = static_cast<UINT64>(mClientHeight),
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_R24G8_TYPELESS,
        .SampleDesc = { 1, 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    };

    const D3D12_CLEAR_VALUE optClear = {
        .Format = mDepthStencilFormat,
        .DepthStencil = {1.0f, 0}
    };

    const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    const D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {
        .Format = mDepthStencilFormat,
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
        .Flags = D3D12_DSV_FLAG_NONE,
        .Texture2D = {0}
    };
    md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    // Transition the resource from its initial state to be used as a depth buffer.
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &transition);

    // Execute the resize commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* const cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);

    // Wait until resize is complete.
    FlushCommandQueue();

    // Update the viewport transform to cover the client area.
    mScreenViewport.TopLeftX = 0;
    mScreenViewport.TopLeftY = 0;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

LRESULT D3DApp::MsgProc(
    const HWND hwnd, 
    const UINT msg, 
    const WPARAM wParam, 
    const LPARAM lParam)
{
    switch (msg)
    {
        // WM_ACTIVATE is sent when the window is activated or deactivated.  
        // We pause the game when the window is deactivated and unpause it 
        // when it becomes active.  
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                mAppPaused = true;
                mTimer.Stop();
            }
            else
            {
                mAppPaused = false;
                mTimer.Start();
            }
            return 0;

            // WM_SIZE is sent when the user resizes the window.  
        case WM_SIZE:
            // Save the new client area dimensions.
            mClientWidth = LOWORD(lParam);
            mClientHeight = HIWORD(lParam);
            if (md3dDevice)
            {
                if (wParam == SIZE_MINIMIZED)
                {
                    mAppPaused = true;
                    mMinimized = true;
                    mMaximized = false;
                }
                else if (wParam == SIZE_MAXIMIZED)
                {
                    mAppPaused = false;
                    mMinimized = false;
                    mMaximized = true;
                    OnResize();
                }
                else if (wParam == SIZE_RESTORED)
                {

                    // Restoring from minimized state?
                    if (mMinimized)
                    {
                        mAppPaused = false;
                        mMinimized = false;
                        OnResize();
                    }

                    // Restoring from maximized state?
                    else if (mMaximized)
                    {
                        mAppPaused = false;
                        mMaximized = false;
                        OnResize();
                    }
                    else if (mResizing)
                    {
                        // If user is dragging the resize bars, we do not resize 
                        // the buffers here because as the user continuously 
                        // drags the resize bars, a stream of WM_SIZE messages are
                        // sent to the window, and it would be pointless (and slow)
                        // to resize for each WM_SIZE message received from dragging
                        // the resize bars.  So instead, we reset after the user is 
                        // done resizing the window and releases the resize bars, which 
                        // sends a WM_EXITSIZEMOVE message.
                    }
                    else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
                    {
                        OnResize();
                    }
                }
            }
            return 0;

            // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
        case WM_ENTERSIZEMOVE:
            mAppPaused = true;
            mResizing = true;
            mTimer.Stop();
            return 0;

            // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
            // Here we reset everything based on the new window dimensions.
        case WM_EXITSIZEMOVE:
            mAppPaused = false;
            mResizing = false;
            mTimer.Start();
            OnResize();
            return 0;

            // WM_DESTROY is sent when the window is being destroyed.
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

            // The WM_MENUCHAR message is sent when a menu is active and the user presses 
            // a key that does not correspond to any mnemonic or accelerator key. 
        case WM_MENUCHAR:
            // Don't beep when we alt-enter.
            return MAKELRESULT(0, MNC_CLOSE);

            // Catch this message so to prevent the window from becoming too small.
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
            ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
            return 0;

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
            OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
            OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitMainWindow()
{
    const WNDCLASS wc = {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = MainWndProc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = mhAppInst,
        .hIcon = LoadIconW(0, IDI_APPLICATION),
        .hCursor = LoadCursorW(0, IDC_ARROW),
        .hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH),
        .lpszMenuName = nullptr,
        .lpszClassName = L"MainWnd"
    };

    if (RegisterClassW(&wc) == 0)
    {
        MessageBoxW(0, L"RegisterClass Failed.", 0, 0);
        return false;
    }

    // Compute window rectangle dimensions based on requested client area dimensions.
    RECT R = { 0, 0, mClientWidth, mClientHeight };
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
    const int width = R.right - R.left;
    const int height = R.bottom - R.top;

    mhMainWnd = CreateWindowW(
        L"MainWnd", 
        mMainWndCaption.c_str(),
        WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, 
        CW_USEDEFAULT, 
        width, 
        height, 
        0, 
        0, 
        mhAppInst, 
        0);
    if (!mhMainWnd)
    {
        MessageBoxW(0, L"CreateWindow Failed.", 0, 0);
        return false;
    }

    ShowWindow(mhMainWnd, SW_SHOW);
    UpdateWindow(mhMainWnd);

    return true;
}

bool D3DApp::InitDirect3D()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG) 
    // Enable the D3D12 debug layer.
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
        dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&mdxgiFactory)));

    // Try to create hardware device.
    const HRESULT hardwareResult = D3D12CreateDevice(
        nullptr,             // default adapter
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&md3dDevice));

    // Fallback to WARP device.
    if (FAILED(hardwareResult))
    {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            pWarpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&md3dDevice)));
    }

    ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&mFence)));

    mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Log name of current adapter.
    {
        ComPtr<IDXGIAdapter> currentAdapter;
        ThrowIfFailed(mdxgiFactory->EnumAdapterByLuid(
            md3dDevice->GetAdapterLuid(), 
            IID_PPV_ARGS(&currentAdapter)));
        DXGI_ADAPTER_DESC adapterDesc = {};
        currentAdapter->GetDesc(&adapterDesc);
        OutputDebugStringW(L"\nCURRENT ADPATER: ");
        OutputDebugStringW(adapterDesc.Description);
        OutputDebugStringW(L"\n\n");
    }

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();

    return true;
}

void D3DApp::CreateCommandObjects()
{
    constexpr D3D12_COMMAND_QUEUE_DESC queueDesc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
    };
    ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

    ThrowIfFailed(md3dDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

    ThrowIfFailed(md3dDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(), // Associated command allocator
        nullptr,                   // Initial PipelineStateObject
        IID_PPV_ARGS(mCommandList.GetAddressOf())));

    // Start off in a closed state.  This is because the first time we refer 
    // to the command list we will Reset it, and it needs to be closed before
    // calling Reset.
    mCommandList->Close();
}

void D3DApp::CreateSwapChain()
{
    // Release the previous swapchain we will be recreating.
    mSwapChain.Reset();

    const DXGI_SWAP_CHAIN_DESC1 sd = {
        .Width = static_cast<UINT>(mClientWidth),
        .Height = static_cast<UINT>(mClientHeight),
        .Format = mBackBufferFormat,
        .SampleDesc = { 1, 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = SwapChainBufferCount,
        .Scaling = DXGI_SCALING_NONE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
    };

    // Note: Swap chain uses queue to perform flush.
    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(mdxgiFactory->CreateSwapChainForHwnd(
        mCommandQueue.Get(),
        mhMainWnd,
        &sd,
        nullptr,
        nullptr,
        &swapChain1));

    ThrowIfFailed(mdxgiFactory->MakeWindowAssociation(mhMainWnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(swapChain1.As(&mSwapChain));
}

void D3DApp::FlushCommandQueue()
{
    // Advance the fence value to mark commands up to this fence point.
    ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point.  Because we 
    // are on the GPU timeline, the new fence point won't be set until the GPU finishes
    // processing all the commands prior to this Signal().
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

    // Wait until the GPU has completed commands up to this fence point.
    if (mFence->GetCompletedValue() < mCurrentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        assert(eventHandle);

        // Fire event when GPU hits current fence.  
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

        // Wait until the GPU hits current fence event is fired.
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

ID3D12Resource* D3DApp::CurrentBackBuffer()const
{
    return mSwapChainBuffer[mCurrBackBufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView()const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        mCurrBackBufferIndex,
        mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView()const
{
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::CalculateFrameStats()
{
    // Code computes the average frames per second, and also the 
    // average time it takes to render one frame.  These stats 
    // are appended to the window caption bar.

    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    ++frameCnt;

    // Compute averages over one second period.
    if ((mTimer.TotalTime() - timeElapsed) >= 1.0f)
    {
        const float fps = static_cast<float>(frameCnt); // fps = frameCnt / 1
        const float mspf = 1000.0f / fps;

        const std::wstring windowText = std::format(
            L"{}    fps: {}    mspf: {}",
            mMainWndCaption,
            fps,
            mspf);
        SetWindowTextW(mhMainWnd, windowText.c_str());

        // Reset for next average.
        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}

void D3DApp::LogAdapters()
{
    OutputDebugStringW(L"ADAPTERS:\n");
    UINT i = 0;
    IDXGIAdapter* adapter = nullptr;
    std::vector<IDXGIAdapter*> adapterList;
    adapterList.reserve(3);
    DXGI_ADAPTER_DESC desc = {};
    std::wstring text;
    while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        adapter->GetDesc(&desc);

        text = std::format(L"***Adapter: {}\n", desc.Description);
        OutputDebugStringW(text.c_str());

        adapterList.push_back(adapter);

        ++i;
    }

    OutputDebugStringW(L"\n");
    
    for (size_t x = 0; x < adapterList.size(); ++x)
    {
        LogAdapterOutputs(adapterList[x]);
        ReleaseCom(adapterList[x]);
    }

    OutputDebugStringW(L"\n");
}

void D3DApp::LogAdapterOutputs(IDXGIAdapter* const adapter)
{
    UINT i = 0;
    IDXGIOutput* output = nullptr;
    DXGI_OUTPUT_DESC desc = {};
    std::wstring text;
    while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
    {
        output->GetDesc(&desc);

        text = std::format(L"***Output: {}\n", desc.DeviceName);
        OutputDebugStringW(text.c_str());

        LogOutputDisplayModes(output, mBackBufferFormat);
        ReleaseCom(output);
        ++i;
    }
}

void D3DApp::LogOutputDisplayModes(IDXGIOutput* const output, const DXGI_FORMAT format)
{
    UINT count = 0;
    UINT flags = 0;

    // Call with nullptr to get list count.
    output->GetDisplayModeList(format, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modeList(count);
    output->GetDisplayModeList(format, flags, &count, &modeList[0]);

    std::wstring text;
    for (auto& x : modeList)
    {
        const UINT n = x.RefreshRate.Numerator;
        const UINT d = x.RefreshRate.Denominator;
        text = L"Width = " + std::to_wstring(x.Width) + L" " +
            L"Height = " + std::to_wstring(x.Height) + L" " +
            L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
            L"\n";

        ::OutputDebugStringW(text.c_str());
    }
}
