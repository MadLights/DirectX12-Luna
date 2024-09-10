//***************************************************************************************
// BlurFilter.cpp by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "BlurFilter.h"

BlurFilter::BlurFilter(
    ID3D12Device* device,
    const UINT width, 
    const UINT height,
    const DXGI_FORMAT format)
    : md3dDevice(device), mWidth(width), mHeight(height), mFormat(format)
{
    BuildResources();
}

ID3D12Resource* BlurFilter::Output()
{
    return mBlurMap0.Get();
}

void BlurFilter::BuildDescriptors(
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
    UINT descriptorSize)
{
    // Save references to the descriptors. 
    mBlur0CpuSrv = hCpuDescriptor;
    mBlur0CpuUav = hCpuDescriptor.Offset(descriptorSize);
    mBlur1CpuSrv = hCpuDescriptor.Offset(descriptorSize);
    mBlur1CpuUav = hCpuDescriptor.Offset(descriptorSize);

    mBlur0GpuSrv = hGpuDescriptor;
    mBlur0GpuUav = hGpuDescriptor.Offset(descriptorSize);
    mBlur1GpuSrv = hGpuDescriptor.Offset(descriptorSize);
    mBlur1GpuUav = hGpuDescriptor.Offset(descriptorSize);

    BuildDescriptors();
}

void BlurFilter::OnResize(UINT newWidth, UINT newHeight)
{
    if ((mWidth != newWidth) || (mHeight != newHeight))
    {
        mWidth = newWidth;
        mHeight = newHeight;

        BuildResources();

        // New resource, so we need new descriptors to that resource.
        BuildDescriptors();
    }
}

void BlurFilter::Execute(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12RootSignature* rootSig,
    ID3D12PipelineState* horzBlurPSO,
    ID3D12PipelineState* vertBlurPSO,
    ID3D12Resource* input,
    int blurCount)
{
    const std::vector<float> weights = CalcGaussWeights(2.5f);
    int blurRadius = (int)weights.size() / 2;

    cmdList->SetComputeRootSignature(rootSig);

    cmdList->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);
    cmdList->SetComputeRoot32BitConstants(0, (UINT)weights.size(), weights.data(), 1);

    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        input,
        D3D12_RESOURCE_STATE_RENDER_TARGET, 
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->ResourceBarrier(1, &transition);

    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        mBlurMap0.Get(),
        D3D12_RESOURCE_STATE_COMMON, 
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &transition);

    // Copy the input (back-buffer in this example) to BlurMap0.
    cmdList->CopyResource(mBlurMap0.Get(), input);

    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        mBlurMap0.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, 
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &transition);

    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        mBlurMap1.Get(),
        D3D12_RESOURCE_STATE_COMMON, 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &transition);

    for (int i = 0; i < blurCount; ++i)
    {
        //
        // Horizontal Blur pass.
        //

        cmdList->SetPipelineState(horzBlurPSO);

        cmdList->SetComputeRootDescriptorTable(1, mBlur0GpuSrv);
        cmdList->SetComputeRootDescriptorTable(2, mBlur1GpuUav);

        // How many groups do we need to dispatch to cover a row of pixels, where each
        // group covers 256 pixels (the 256 is defined in the ComputeShader).
        UINT numGroupsX = (UINT)ceilf(mWidth / 256.0f);
        cmdList->Dispatch(numGroupsX, mHeight, 1);

        transition = CD3DX12_RESOURCE_BARRIER::Transition(
            mBlurMap0.Get(),
            D3D12_RESOURCE_STATE_GENERIC_READ, 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->ResourceBarrier(1, &transition);

        transition = CD3DX12_RESOURCE_BARRIER::Transition(
            mBlurMap1.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
            D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->ResourceBarrier(1, &transition);

        //
        // Vertical Blur pass.
        //

        cmdList->SetPipelineState(vertBlurPSO);

        cmdList->SetComputeRootDescriptorTable(1, mBlur1GpuSrv);
        cmdList->SetComputeRootDescriptorTable(2, mBlur0GpuUav);

        // How many groups do we need to dispatch to cover a column of pixels, where each
        // group covers 256 pixels  (the 256 is defined in the ComputeShader).
        UINT numGroupsY = (UINT)ceilf(mHeight / 256.0f);
        cmdList->Dispatch(mWidth, numGroupsY, 1);

        transition = CD3DX12_RESOURCE_BARRIER::Transition(
            mBlurMap0.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
            D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->ResourceBarrier(1, &transition);

        transition = CD3DX12_RESOURCE_BARRIER::Transition(
            mBlurMap1.Get(),
            D3D12_RESOURCE_STATE_GENERIC_READ, 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->ResourceBarrier(1, &transition);
    }
}

std::vector<float> BlurFilter::CalcGaussWeights(float sigma) const
{
    float twoSigma2 = 2.0f * sigma * sigma;

    // Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
    // For example, for sigma = 3, the width of the bell curve is 
    int blurRadius = (int)ceil(2.0f * sigma);

    assert(blurRadius <= MaxBlurRadius);

    std::vector<float> weights(2ull * blurRadius + 1);

    float weightSum = 0.0f;

    for (int i = -blurRadius; i <= blurRadius; ++i)
    {
        float x = (float)i;

        weights[i + blurRadius] = expf(-x * x / twoSigma2);

        weightSum += weights[i + blurRadius];
    }

    // Divide by the sum so all the weights add up to 1.0.
    const float weigthSumInverse = 1.0f / weightSum;
    for (float& w : weights)
    {
        w *= weigthSumInverse;
    }

    return weights;
}

void BlurFilter::BuildDescriptors()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

    uavDesc.Format = mFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    md3dDevice->CreateShaderResourceView(mBlurMap0.Get(), &srvDesc, mBlur0CpuSrv);
    md3dDevice->CreateUnorderedAccessView(mBlurMap0.Get(), nullptr, &uavDesc, mBlur0CpuUav);

    md3dDevice->CreateShaderResourceView(mBlurMap1.Get(), &srvDesc, mBlur1CpuSrv);
    md3dDevice->CreateUnorderedAccessView(mBlurMap1.Get(), nullptr, &uavDesc, mBlur1CpuUav);
}

void BlurFilter::BuildResources()
{
    // Note, compressed formats cannot be used for UAV.  We get error like:
    // ERROR: ID3D11Device::CreateTexture2D: The format (0x4d, BC3_UNORM) 
    // cannot be bound as an UnorderedAccessView, or cast to a format that
    // could be bound as an UnorderedAccessView.  Therefore this format 
    // does not support D3D11_BIND_UNORDERED_ACCESS.

    const D3D12_RESOURCE_DESC texDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = mWidth,
        .Height = mHeight,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = mFormat,
        .SampleDesc = {1, 0},
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    };

    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&mBlurMap0)));

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&mBlurMap1)));
}
