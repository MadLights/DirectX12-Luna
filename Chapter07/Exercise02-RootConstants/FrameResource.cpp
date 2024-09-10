#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount)
    : PassCB(std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true))
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));
}
