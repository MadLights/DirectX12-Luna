// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "d3dUtil.h"

#include <format>
#include <fstream>

#include <comdef.h>

#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

DxException::DxException(
    const HRESULT hr, 
    const std::wstring& functionName, 
    const std::wstring& filename, 
    const int lineNumber)
    : ErrorCode(hr),
      FunctionName(functionName),
      Filename(filename),
      LineNumber(lineNumber)
{}

bool d3dUtil::IsKeyDown(const int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    const std::ifstream::pos_type size = fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read((char*)blob->GetBufferPointer(), size);

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* const device,
    ID3D12GraphicsCommandList* const cmdList,
    const void* const initData,
    const UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    assert(device);
    assert(cmdList);
    assert(initData);

    ComPtr<ID3D12Resource> defaultBuffer;

    // Create the actual default buffer resource.

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap. 

    heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
		D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

    // Describe the data we want to copy into the default buffer.
    const D3D12_SUBRESOURCE_DATA subResourceData = {
        .pData = initData,
        .RowPitch = static_cast<LONG_PTR>(byteSize),
        .SlicePitch = subResourceData.RowPitch
    };

    // Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
    // will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
    // the intermediate upload heap data will be copied to mBuffer.
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON, 
        D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &transition);
    
    UpdateSubresources<1>(
        cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	    
    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, 
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &transition);

    // Note: uploadBuffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the uploadBuffer after it knows the copy has been executed.

    return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3DBlob> byteCode;
	ComPtr<ID3DBlob> errors;
	HRESULT hr = D3DCompileFromFile(
        filename.c_str(), 
        defines, 
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), 
        target.c_str(), 
        compileFlags, 
        0, 
        &byteCode, 
        &errors);

    if (FAILED(hr))
    {
        if (errors)
        {
            OutputDebugStringA("\nSHADER COMPILATION FAILED!!!\n");
		    OutputDebugStringA((char*)errors->GetBufferPointer());
            OutputDebugStringA("\n");
        }
        
        ThrowIfFailed(hr);
    }
	return byteCode;
}

std::wstring DxException::ToString() const
{
    // Get the string description of the error code.
    const _com_error err(ErrorCode);
    return std::format(
        L"{} FAILED!\n\n In file {}; line {}.\n\n Error:\n{}",
        FunctionName,
        Filename,
        LineNumber,
        err.ErrorMessage());
}
