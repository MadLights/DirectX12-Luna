#include <DirectXColors.h>

#include "../../Common/d3dApp.h"

#include "../../Common/d3dx12.h"
#include "../../Common/d3dUtil.h"

using namespace DirectX;

class InitDirect3DApp final : public D3DApp
{
public:
	InitDirect3DApp(HINSTANCE hInstance) : D3DApp(hInstance) {}

private:
	virtual void Update(const GameTimer& gt) override {}
	virtual void Draw(const GameTimer& gt) override;
};

void InitDirect3DApp::Draw(const GameTimer& gt)
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);
	
	ID3D12Resource* const backBuffResource = CurrentBackBuffer();

	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffResource,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &transition);

	const D3D12_CPU_DESCRIPTOR_HANDLE backBuffView = CurrentBackBufferView();
	const D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = DepthStencilView();
	mCommandList->OMSetRenderTargets(1, &backBuffView, true, &depthStencilView);

	mCommandList->ClearRenderTargetView(backBuffView, Colors::DarkSlateBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(
		depthStencilView,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);

	transition = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffResource,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &transition);

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* const commandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, commandLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();

	FlushCommandQueue(); 
}


int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE prevInstance, 
	PSTR cmdLine, 
	int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	InitDirect3DApp app(hInstance);
	if (!app.Initialize())
	{
		MessageBox(nullptr, L"Failed to initialize app.", nullptr, 0);
		return 1;
	}

	return app.Run();
}
