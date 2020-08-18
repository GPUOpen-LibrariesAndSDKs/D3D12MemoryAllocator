//
// Copyright (c) 2019-2020 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Common.h"
#include "Tests.h"
#include <thread>

extern ID3D12GraphicsCommandList* BeginCommandList();
extern void EndCommandList(ID3D12GraphicsCommandList* cmdList);

static constexpr UINT64 MEGABYTE = 1024 * 1024;

template<typename T>
struct D3d12maObjDeleter
{
    void operator()(T* obj) const
    {
        if(obj)
        {
            obj->Release();
        }
    }
};

typedef std::unique_ptr<D3D12MA::Allocation, D3d12maObjDeleter<D3D12MA::Allocation>> AllocationUniquePtr;
typedef std::unique_ptr<D3D12MA::Pool, D3d12maObjDeleter<D3D12MA::Pool>> PoolUniquePtr;
typedef std::unique_ptr<D3D12MA::VirtualBlock, D3d12maObjDeleter<D3D12MA::VirtualBlock>> VirtualBlockUniquePtr;

struct ResourceWithAllocation
{
    CComPtr<ID3D12Resource> resource;
    AllocationUniquePtr allocation;
    UINT64 size = UINT64_MAX;
    UINT dataSeed = 0;

    void Reset()
    {
        resource.Release();
        allocation.reset();
        size = UINT64_MAX;
        dataSeed = 0;
    }
};

static void FillResourceDescForBuffer(D3D12_RESOURCE_DESC& outResourceDesc, UINT64 size)
{
    outResourceDesc = {};
    outResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    outResourceDesc.Alignment = 0;
    outResourceDesc.Width = size;
    outResourceDesc.Height = 1;
    outResourceDesc.DepthOrArraySize = 1;
    outResourceDesc.MipLevels = 1;
    outResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    outResourceDesc.SampleDesc.Count = 1;
    outResourceDesc.SampleDesc.Quality = 0;
    outResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    outResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
}

static void FillData(void* outPtr, const UINT64 sizeInBytes, UINT seed)
{
    UINT* outValues = (UINT*)outPtr;
    const UINT64 sizeInValues = sizeInBytes / sizeof(UINT);
    UINT value = seed;
    for(UINT i = 0; i < sizeInValues; ++i)
    {
        outValues[i] = value++;
    }
}

static bool ValidateData(const void* ptr, const UINT64 sizeInBytes, UINT seed)
{
    const UINT* values = (const UINT*)ptr;
    const UINT64 sizeInValues = sizeInBytes / sizeof(UINT);
    UINT value = seed;
    for(UINT i = 0; i < sizeInValues; ++i)
    {
        if(values[i] != value++)
        {
            //FAIL("ValidateData failed.");
            return false;
        }
    }
    return true;
}

static bool ValidateDataZero(const void* ptr, const UINT64 sizeInBytes)
{
    const UINT* values = (const UINT*)ptr;
    const UINT64 sizeInValues = sizeInBytes / sizeof(UINT);
    for(UINT i = 0; i < sizeInValues; ++i)
    {
        if(values[i] != 0)
        {
            //FAIL("ValidateData failed.");
            return false;
        }
    }
    return true;
}

static void TestVirtualBlocks(const TestContext& ctx)
{
    wprintf(L"Test virtual blocks\n");

    using namespace D3D12MA;

    const UINT64 blockSize = 16 * MEGABYTE;
    const UINT64 alignment = 256;

    // # Create block 16 MB

    VirtualBlockUniquePtr block;
    VirtualBlock* blockPtr = nullptr;
    VIRTUAL_BLOCK_DESC blockDesc = {};
    blockDesc.pAllocationCallbacks = ctx.allocationCallbacks;
    blockDesc.Size = blockSize;
    CHECK_HR( CreateVirtualBlock(&blockDesc, &blockPtr) );
    CHECK_BOOL( blockPtr );
    block.reset(blockPtr);

    // # Allocate 8 MB

    VIRTUAL_ALLOCATION_DESC allocDesc = {};
    allocDesc.Alignment = alignment;
    allocDesc.pUserData = (void*)(uintptr_t)1;
    allocDesc.Size = 8 * MEGABYTE;
    UINT64 alloc0Offset;
    CHECK_HR( block->Allocate(&allocDesc, &alloc0Offset) );
    CHECK_BOOL( alloc0Offset < blockSize );

    // # Validate the allocation
  
    VIRTUAL_ALLOCATION_INFO allocInfo = {};
    block->GetAllocationInfo(alloc0Offset, &allocInfo);
    CHECK_BOOL( allocInfo.size == allocDesc.Size );
    CHECK_BOOL( allocInfo.pUserData == allocDesc.pUserData );

    // # Check SetUserData

    block->SetAllocationUserData(alloc0Offset, (void*)(uintptr_t)2);
    block->GetAllocationInfo(alloc0Offset, &allocInfo);
    CHECK_BOOL( allocInfo.pUserData == (void*)(uintptr_t)2 );

    // # Allocate 4 MB

    allocDesc.Size = 4 * MEGABYTE;
    allocDesc.Alignment = alignment;
    UINT64 alloc1Offset;
    CHECK_HR( block->Allocate(&allocDesc, &alloc1Offset) );
    CHECK_BOOL( alloc1Offset < blockSize );
    CHECK_BOOL( alloc1Offset + 4 * MEGABYTE <= alloc0Offset || alloc0Offset + 8 * MEGABYTE <= alloc1Offset ); // Check if they don't overlap.

    // # Allocate another 8 MB - it should fail

    allocDesc.Size = 8 * MEGABYTE;
    allocDesc.Alignment = alignment;
    UINT64 alloc2Offset;
    CHECK_BOOL( FAILED(block->Allocate(&allocDesc, &alloc2Offset)) );
    CHECK_BOOL( alloc2Offset == UINT64_MAX );

    // # Free the 4 MB block. Now allocation of 8 MB should succeed.

    block->FreeAllocation(alloc1Offset);
    CHECK_HR( block->Allocate(&allocDesc, &alloc2Offset) );
    CHECK_BOOL( alloc2Offset < blockSize );
    CHECK_BOOL( alloc2Offset + 4 * MEGABYTE <= alloc0Offset || alloc0Offset + 8 * MEGABYTE <= alloc2Offset ); // Check if they don't overlap.

    // # Calculate statistics

    StatInfo statInfo = {};
    block->CalculateStats(&statInfo);
    CHECK_BOOL(statInfo.AllocationCount == 2);
    CHECK_BOOL(statInfo.BlockCount == 1);
    CHECK_BOOL(statInfo.UsedBytes == blockSize);
    CHECK_BOOL(statInfo.UnusedBytes + statInfo.UsedBytes == blockSize);

    // # Generate JSON dump

    WCHAR* json = nullptr;
    block->BuildStatsString(&json);
    {
        std::wstring str(json);
        CHECK_BOOL( str.find(L"\"UserData\": 1") != std::wstring::npos );
        CHECK_BOOL( str.find(L"\"UserData\": 2") != std::wstring::npos );
    }
    block->FreeStatsString(json);

    // # Free alloc0, leave alloc2 unfreed.

    block->FreeAllocation(alloc0Offset);

    // # Test alignment

    {
        constexpr size_t allocCount = 10;
        UINT64 allocOffset[allocCount] = {};
        for(size_t i = 0; i < allocCount; ++i)
        {
            const bool alignment0 = i == allocCount - 1;
            allocDesc.Size = i * 3 + 15;
            allocDesc.Alignment = alignment0 ? 0 : 8;
            CHECK_HR(block->Allocate(&allocDesc, &allocOffset[i]));
            if(!alignment0)
            {
                CHECK_BOOL(allocOffset[i] % allocDesc.Alignment == 0);
            }
        }

        for(size_t i = allocCount; i--; )
        {
            block->FreeAllocation(allocOffset[i]);
        }
    }

    // # Final cleanup

    block->FreeAllocation(alloc2Offset);

    //block->Clear();
}

static void TestFrameIndexAndJson(const TestContext& ctx)
{
    const UINT64 bufSize = 32ull * 1024;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    const UINT BEGIN_INDEX = 10;
    const UINT END_INDEX = 20;
    for (UINT frameIndex = BEGIN_INDEX; frameIndex < END_INDEX; ++frameIndex)
    {
        ctx.allocator->SetCurrentFrameIndex(frameIndex);
        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR(ctx.allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &alloc,
            __uuidof(ID3D12Resource),
            NULL));

        WCHAR* statsString;
        ctx.allocator->BuildStatsString(&statsString, TRUE);
        const UINT BUFFER_SIZE = 1024;
        WCHAR buffer[BUFFER_SIZE];
        for (UINT testIndex = BEGIN_INDEX; testIndex < END_INDEX; ++testIndex)
        {
            swprintf(buffer, BUFFER_SIZE, L"\"CreationFrameIndex\": %u", testIndex);
            if (testIndex == frameIndex)
            {
                CHECK_BOOL(wcsstr(statsString, buffer) != NULL);
            }
            else
            {
                CHECK_BOOL(wcsstr(statsString, buffer) == NULL);
            }
        }
        ctx.allocator->FreeStatsString(statsString);
        alloc->Release();
    }
}

static void TestCommittedResourcesAndJson(const TestContext& ctx)
{
    wprintf(L"Test committed resources and JSON\n");
    
    const UINT count = 4;
    const UINT64 bufSize = 32ull * 1024;
    const wchar_t* names[count] = {
        L"Resource\nFoo\r\nBar",
        L"Resource \"'&<>?#@!&-=_+[]{};:,./\\",
        nullptr,
        L"",
    };

    ResourceWithAllocation resources[count];

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    for(UINT i = 0; i < count; ++i)
    {
        const bool receiveExplicitResource = i < 2;

        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            NULL,
            &alloc,
            __uuidof(ID3D12Resource),
            receiveExplicitResource ? (void**)&resources[i].resource : NULL));
        resources[i].allocation.reset(alloc);

        if(receiveExplicitResource)
        {
            ID3D12Resource* res = resources[i].resource.p;
            CHECK_BOOL(res && res == resources[i].allocation->GetResource());
            const ULONG refCountAfterAdd = res->AddRef();
            CHECK_BOOL(refCountAfterAdd == 3);
            res->Release();
        }
        
        // Make sure it has implicit heap.
        CHECK_BOOL( resources[i].allocation->GetHeap() == NULL && resources[i].allocation->GetOffset() == 0 );

        resources[i].allocation->SetName(names[i]);
    }

    // Check names.
    for(UINT i = 0; i < count; ++i)
    {
        const wchar_t* const allocName = resources[i].allocation->GetName();
        if(allocName)
        {
            CHECK_BOOL( wcscmp(allocName, names[i]) == 0 );
        }
        else
        {
            CHECK_BOOL(names[i] == NULL);
        }
    }

    WCHAR* jsonString;
    ctx.allocator->BuildStatsString(&jsonString, TRUE);
    CHECK_BOOL(wcsstr(jsonString, L"\"Resource\\nFoo\\r\\nBar\"") != NULL);
    CHECK_BOOL(wcsstr(jsonString, L"\"Resource \\\"'&<>?#@!&-=_+[]{};:,.\\/\\\\\"") != NULL);
    CHECK_BOOL(wcsstr(jsonString, L"\"\"") != NULL);
    ctx.allocator->FreeStatsString(jsonString);
}

static void TestCustomHeapFlags(const TestContext& ctx)
{
    wprintf(L"Test custom heap flags\n");

    // 1. Just memory heap with custom flags
    {
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        allocDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES |
            D3D12_HEAP_FLAG_SHARED; // Extra flag.

        D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = {};
        resAllocInfo.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        resAllocInfo.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR( ctx.allocator->AllocateMemory(&allocDesc, &resAllocInfo, &alloc) );
        ResourceWithAllocation res;
        res.allocation.reset(alloc);

        // Must be created as separate allocation.
        CHECK_BOOL( res.allocation->GetOffset() == 0 );
    }

    // 2. Committed resource with custom flags
    {
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = 1920;
        resourceDesc.Height = 1080;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        allocDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER; // Extra flags.

        ResourceWithAllocation res;
        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            NULL,
            &alloc,
            IID_PPV_ARGS(&res.resource)) );
        res.allocation.reset(alloc);

        // Must be created as committed.
        CHECK_BOOL( res.allocation->GetHeap() == NULL );
    }
}

static void TestPlacedResources(const TestContext& ctx)
{
    wprintf(L"Test placed resources\n");

    const bool alwaysCommitted = (ctx.allocatorFlags & D3D12MA::ALLOCATOR_FLAG_ALWAYS_COMMITTED) != 0;

    const UINT count = 4;
    const UINT64 bufSize = 32ull * 1024;
    ResourceWithAllocation resources[count];

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    D3D12MA::Allocation* alloc = nullptr;
    for(UINT i = 0; i < count; ++i)
    {
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &alloc,
            IID_PPV_ARGS(&resources[i].resource)) );
        resources[i].allocation.reset(alloc);

        // Make sure it doesn't have implicit heap.
        if(!alwaysCommitted)
        {
            CHECK_BOOL( resources[i].allocation->GetHeap() != NULL );
        }
    }

    // Make sure at least some of the resources belong to the same heap, but their memory ranges don't overlap.
    bool sameHeapFound = false;
    for(size_t i = 0; i < count; ++i)
    {
        for(size_t j = i + 1; j < count; ++j)
        {
            const ResourceWithAllocation& resI = resources[i];
            const ResourceWithAllocation& resJ = resources[j];
            if(resI.allocation->GetHeap() != NULL &&
                resI.allocation->GetHeap() == resJ.allocation->GetHeap())
            {
                sameHeapFound = true;
                CHECK_BOOL(resI.allocation->GetOffset() + resI.allocation->GetSize() <= resJ.allocation->GetOffset() ||
                    resJ.allocation->GetOffset() + resJ.allocation->GetSize() <= resI.allocation->GetOffset());
            }
        }
    }
    if(!alwaysCommitted)
    {
        CHECK_BOOL(sameHeapFound);
    }

    // Additionally create a texture to see if no error occurs due to bad handling of Resource Tier.
    resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = 1024;
    resourceDesc.Height = 1024;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ResourceWithAllocation textureRes;
    CHECK_HR( ctx.allocator->CreateResource(
        &allocDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        NULL,
        &alloc,
        IID_PPV_ARGS(&textureRes.resource)) );
    textureRes.allocation.reset(alloc);

    // Additionally create an MSAA render target to see if no error occurs due to bad handling of Resource Tier.
    resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = 1920;
    resourceDesc.Height = 1080;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDesc.SampleDesc.Count = 2;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ResourceWithAllocation renderTargetRes;
    CHECK_HR( ctx.allocator->CreateResource(
        &allocDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        NULL,
        &alloc,
        IID_PPV_ARGS(&renderTargetRes.resource)) );
    renderTargetRes.allocation.reset(alloc);
}

static void TestOtherComInterface(const TestContext& ctx)
{
    wprintf(L"Test other COM interface\n");

    D3D12_RESOURCE_DESC resDesc;
    FillResourceDescForBuffer(resDesc, 0x10000);

    for(uint32_t i = 0; i < 2; ++i)
    {
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        if(i == 1)
        {
            allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
        }

        D3D12MA::Allocation* alloc = nullptr;
        CComPtr<ID3D12Pageable> pageable;
        CHECK_HR(ctx.allocator->CreateResource(
            &allocDesc,
            &resDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr, // pOptimizedClearValue
            &alloc,
            IID_PPV_ARGS(&pageable)));

        // Do something with the interface to make sure it's valid.
        CComPtr<ID3D12Device> device;
        CHECK_HR(pageable->GetDevice(IID_PPV_ARGS(&device)));
        CHECK_BOOL(device == ctx.device);

        alloc->Release();
    }
}

static void TestCustomPools(const TestContext& ctx)
{
    wprintf(L"Test custom pools\n");

    // # Fetch global stats 1

    D3D12MA::Stats globalStatsBeg = {};
    ctx.allocator->CalculateStats(&globalStatsBeg);

    // # Create pool, 1..2 blocks of 11 MB
    
    D3D12MA::POOL_DESC poolDesc = {};
    poolDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    poolDesc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    poolDesc.BlockSize = 11 * MEGABYTE;
    poolDesc.MinBlockCount = 1;
    poolDesc.MaxBlockCount = 2;

    D3D12MA::Pool* poolPtr;
    CHECK_HR( ctx.allocator->CreatePool(&poolDesc, &poolPtr) );
    PoolUniquePtr pool{poolPtr};

    D3D12MA::Allocation* allocPtr;

    // # Validate stats for empty pool

    D3D12MA::StatInfo poolStats = {};
    pool->CalculateStats(&poolStats);
    CHECK_BOOL( poolStats.BlockCount == 1 );
    CHECK_BOOL( poolStats.AllocationCount == 0 );
    CHECK_BOOL( poolStats.UsedBytes == 0 );
    CHECK_BOOL( poolStats.UnusedBytes == poolStats.BlockCount * poolDesc.BlockSize );

    // # SetName and GetName
    static const wchar_t* NAME = L"Custom pool name 1";
    pool->SetName(NAME);
    CHECK_BOOL( wcscmp(pool->GetName(), NAME) == 0 );

    // # SetMinBytes

    CHECK_HR( pool->SetMinBytes(15 * MEGABYTE) );
    pool->CalculateStats(&poolStats);
    CHECK_BOOL( poolStats.BlockCount == 2 );
    CHECK_BOOL( poolStats.AllocationCount == 0 );
    CHECK_BOOL( poolStats.UsedBytes == 0 );
    CHECK_BOOL( poolStats.UnusedBytes == poolStats.BlockCount * poolDesc.BlockSize );

    CHECK_HR( pool->SetMinBytes(0) );
    pool->CalculateStats(&poolStats);
    CHECK_BOOL( poolStats.BlockCount == 1 );
    CHECK_BOOL( poolStats.AllocationCount == 0 );
    CHECK_BOOL( poolStats.UsedBytes == 0 );
    CHECK_BOOL( poolStats.UnusedBytes == poolStats.BlockCount * poolDesc.BlockSize );

    // # Create buffers 2x 5 MB

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.CustomPool = pool.get();
    allocDesc.ExtraHeapFlags = (D3D12_HEAP_FLAGS)0xCDCDCDCD; // Should be ignored.
    allocDesc.HeapType = (D3D12_HEAP_TYPE)0xCDCDCDCD; // Should be ignored.

    const UINT64 BUFFER_SIZE = 5 * MEGABYTE;
    D3D12_RESOURCE_DESC resDesc;
    FillResourceDescForBuffer(resDesc, BUFFER_SIZE);

    AllocationUniquePtr allocs[4];
    for(uint32_t i = 0; i < 2; ++i)
    {
        CHECK_HR( ctx.allocator->CreateResource(&allocDesc, &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, // pOptimizedClearValue
            &allocPtr,
            __uuidof(ID3D12Resource), NULL) ); // riidResource, ppvResource
        allocs[i].reset(allocPtr);
    }

    // # Validate pool stats now

    pool->CalculateStats(&poolStats);
    CHECK_BOOL( poolStats.BlockCount == 1 );
    CHECK_BOOL( poolStats.AllocationCount == 2 );
    CHECK_BOOL( poolStats.UsedBytes == 2 * BUFFER_SIZE );
    CHECK_BOOL( poolStats.UnusedBytes == poolDesc.BlockSize - poolStats.UsedBytes );

    // # Check that global stats are updated as well

    D3D12MA::Stats globalStatsCurr = {};
    ctx.allocator->CalculateStats(&globalStatsCurr);

    CHECK_BOOL( globalStatsCurr.Total.AllocationCount == globalStatsBeg.Total.AllocationCount + poolStats.AllocationCount );
    CHECK_BOOL( globalStatsCurr.Total.BlockCount == globalStatsBeg.Total.BlockCount + poolStats.BlockCount );
    CHECK_BOOL( globalStatsCurr.Total.UsedBytes == globalStatsBeg.Total.UsedBytes + poolStats.UsedBytes );

    // # NEVER_ALLOCATE and COMMITTED should fail

    for(uint32_t i = 0; i < 2; ++i)
    {
        allocDesc.Flags = i == 0 ?
            D3D12MA::ALLOCATION_FLAG_NEVER_ALLOCATE:
            D3D12MA::ALLOCATION_FLAG_COMMITTED;
        const HRESULT hr = ctx.allocator->CreateResource(&allocDesc, &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, // pOptimizedClearValue
            &allocPtr,
            __uuidof(ID3D12Resource), NULL); // riidResource, ppvResource
        CHECK_BOOL( FAILED(hr) );
    }

    // # 3 more buffers. 3rd should fail.

    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
    for(uint32_t i = 2; i < 5; ++i)
    {
        HRESULT hr = ctx.allocator->CreateResource(&allocDesc, &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, // pOptimizedClearValue
            &allocPtr,
            __uuidof(ID3D12Resource), NULL); // riidResource, ppvResource
        if(i < 4)
        {
            CHECK_HR( hr );
            allocs[i].reset(allocPtr);
        }
        else
        {
            CHECK_BOOL( FAILED(hr) );
        }
    }

    pool->CalculateStats(&poolStats);
    CHECK_BOOL( poolStats.BlockCount == 2 );
    CHECK_BOOL( poolStats.AllocationCount == 4 );
    CHECK_BOOL( poolStats.UsedBytes == 4 * BUFFER_SIZE );
    CHECK_BOOL( poolStats.UnusedBytes == poolStats.BlockCount * poolDesc.BlockSize - poolStats.UsedBytes );

    // # Make room, AllocateMemory, CreateAliasingResource

    allocs[3].reset();
    allocs[0].reset();

    D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = {};
    resAllocInfo.SizeInBytes = 5 * MEGABYTE;
    resAllocInfo.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    CHECK_HR( ctx.allocator->AllocateMemory(&allocDesc, &resAllocInfo, &allocPtr) );
    allocs[0].reset(allocPtr);

    resDesc.Width = 1 * MEGABYTE;
    CComPtr<ID3D12Resource> res;
    CHECK_HR( ctx.allocator->CreateAliasingResource(allocs[0].get(),
        0, // AllocationLocalOffset
        &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        NULL, // pOptimizedClearValue
        IID_PPV_ARGS(&res)) );
}

static void TestDefaultPoolMinBytes(const TestContext& ctx)
{
    D3D12MA::Stats stats;
    ctx.allocator->CalculateStats(&stats);
    const UINT64 gpuAllocatedBefore = stats.HeapType[0].UsedBytes + stats.HeapType[0].UnusedBytes;

    const UINT64 gpuAllocatedMin = gpuAllocatedBefore * 105 / 100;
    CHECK_HR( ctx.allocator->SetDefaultHeapMinBytes(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,            CeilDiv(gpuAllocatedMin, 3ull)) );
    CHECK_HR( ctx.allocator->SetDefaultHeapMinBytes(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, CeilDiv(gpuAllocatedMin, 3ull)) );
    CHECK_HR( ctx.allocator->SetDefaultHeapMinBytes(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES,     CeilDiv(gpuAllocatedMin, 3ull)) );

    ctx.allocator->CalculateStats(&stats);
    const UINT64 gpuAllocatedAfter = stats.HeapType[0].UsedBytes + stats.HeapType[0].UnusedBytes;
    CHECK_BOOL(gpuAllocatedAfter >= gpuAllocatedMin);

    CHECK_HR( ctx.allocator->SetDefaultHeapMinBytes(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,            0) );
    CHECK_HR( ctx.allocator->SetDefaultHeapMinBytes(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, 0) );
    CHECK_HR( ctx.allocator->SetDefaultHeapMinBytes(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES,     0) );
}

static void TestAliasingMemory(const TestContext& ctx)
{
    wprintf(L"Test aliasing memory\n");

    D3D12_RESOURCE_DESC resDesc1 = {};
    resDesc1.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc1.Alignment = 0;
    resDesc1.Width = 1920;
    resDesc1.Height = 1080;
    resDesc1.DepthOrArraySize = 1;
    resDesc1.MipLevels = 1;
    resDesc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resDesc1.SampleDesc.Count = 1;
    resDesc1.SampleDesc.Quality = 0;
    resDesc1.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc1.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_RESOURCE_DESC resDesc2 = {};
    resDesc2.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc2.Alignment = 0;
    resDesc2.Width = 1024;
    resDesc2.Height = 1024;
    resDesc2.DepthOrArraySize = 1;
    resDesc2.MipLevels = 0;
    resDesc2.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resDesc2.SampleDesc.Count = 1;
    resDesc2.SampleDesc.Quality = 0;
    resDesc2.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc2.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    const D3D12_RESOURCE_ALLOCATION_INFO allocInfo1 =
        ctx.device->GetResourceAllocationInfo(0, 1, &resDesc1);
    const D3D12_RESOURCE_ALLOCATION_INFO allocInfo2 =
        ctx.device->GetResourceAllocationInfo(0, 1, &resDesc2);

    D3D12_RESOURCE_ALLOCATION_INFO finalAllocInfo = {};
    finalAllocInfo.Alignment = std::max(allocInfo1.Alignment, allocInfo2.Alignment);
    finalAllocInfo.SizeInBytes = std::max(allocInfo1.SizeInBytes, allocInfo2.SizeInBytes);

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    allocDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;

    D3D12MA::Allocation* alloc = NULL;
    CHECK_HR( ctx.allocator->AllocateMemory(&allocDesc, &finalAllocInfo, &alloc) );
    CHECK_BOOL(alloc != NULL && alloc->GetHeap() != NULL);

    ID3D12Resource* res1 = NULL;
    CHECK_HR( ctx.allocator->CreateAliasingResource(
        alloc,
        0, // AllocationLocalOffset
        &resDesc1,
        D3D12_RESOURCE_STATE_COMMON,
        NULL, // pOptimizedClearValue
        IID_PPV_ARGS(&res1)) );
    CHECK_BOOL(res1 != NULL);

    ID3D12Resource* res2 = NULL;
    CHECK_HR( ctx.allocator->CreateAliasingResource(
        alloc,
        0, // AllocationLocalOffset
        &resDesc2,
        D3D12_RESOURCE_STATE_COMMON,
        NULL, // pOptimizedClearValue
        IID_PPV_ARGS(&res2)) );
    CHECK_BOOL(res2 != NULL);

    // You can use res1 and res2, but not at the same time!

    res2->Release();
    res1->Release();
    alloc->Release();
}

static void TestMapping(const TestContext& ctx)
{
    wprintf(L"Test mapping\n");

    const UINT count = 10;
    const UINT64 bufSize = 32ull * 1024;
    ResourceWithAllocation resources[count];

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    for(UINT i = 0; i < count; ++i)
    {
        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &alloc,
            IID_PPV_ARGS(&resources[i].resource)) );
        resources[i].allocation.reset(alloc);

        void* mappedPtr = NULL;
        CHECK_HR( resources[i].resource->Map(0, &EMPTY_RANGE, &mappedPtr) );

        FillData(mappedPtr, bufSize, i);

        // Unmap every other buffer. Leave others mapped.
        if((i % 2) != 0)
        {
            resources[i].resource->Unmap(0, NULL);
        }
    }
}

static inline bool StatInfoEqual(const D3D12MA::StatInfo& lhs, const D3D12MA::StatInfo& rhs)
{
    return lhs.BlockCount == rhs.BlockCount &&
        lhs.AllocationCount == rhs.AllocationCount &&
        lhs.UnusedRangeCount == rhs.UnusedRangeCount &&
        lhs.UsedBytes == rhs.UsedBytes &&
        lhs.UnusedBytes == rhs.UnusedBytes &&
        lhs.AllocationSizeMin == rhs.AllocationSizeMin &&
        lhs.AllocationSizeMax == rhs.AllocationSizeMax &&
        lhs.AllocationSizeAvg == rhs.AllocationSizeAvg &&
        lhs.UnusedRangeSizeMin == rhs.UnusedRangeSizeMin &&
        lhs.UnusedRangeSizeMax == rhs.UnusedRangeSizeMax &&
        lhs.UnusedRangeSizeAvg == rhs.UnusedRangeSizeAvg;
}

static void CheckStatInfo(const D3D12MA::StatInfo& statInfo)
{
    if(statInfo.AllocationCount > 0)
    {
        CHECK_BOOL(statInfo.AllocationSizeAvg >= statInfo.AllocationSizeMin &&
            statInfo.AllocationSizeAvg <= statInfo.AllocationSizeMax);
    }
    if(statInfo.UsedBytes > 0)
    {
        CHECK_BOOL(statInfo.AllocationCount > 0);
    }
    if(statInfo.UnusedRangeCount > 0)
    {
        CHECK_BOOL(statInfo.UnusedRangeSizeAvg >= statInfo.UnusedRangeSizeMin &&
            statInfo.UnusedRangeSizeAvg <= statInfo.UnusedRangeSizeMax);
        CHECK_BOOL(statInfo.UnusedRangeSizeMin > 0);
        CHECK_BOOL(statInfo.UnusedRangeSizeMax > 0);
    }
}

static void TestStats(const TestContext& ctx)
{
    wprintf(L"Test stats\n");

    D3D12MA::Stats begStats = {};
    ctx.allocator->CalculateStats(&begStats);

    const UINT count = 10;
    const UINT64 bufSize = 64ull * 1024;
    ResourceWithAllocation resources[count];

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    for(UINT i = 0; i < count; ++i)
    {
        if(i == count / 2)
            allocDesc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED;
        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &alloc,
            IID_PPV_ARGS(&resources[i].resource)) );
        resources[i].allocation.reset(alloc);
    }

    D3D12MA::Stats endStats = {};
    ctx.allocator->CalculateStats(&endStats);

    CHECK_BOOL(endStats.Total.BlockCount >= begStats.Total.BlockCount);
    CHECK_BOOL(endStats.Total.AllocationCount == begStats.Total.AllocationCount + count);
    CHECK_BOOL(endStats.Total.UsedBytes == begStats.Total.UsedBytes + count * bufSize);
    CHECK_BOOL(endStats.Total.AllocationSizeMin <= bufSize);
    CHECK_BOOL(endStats.Total.AllocationSizeMax >= bufSize);

    CHECK_BOOL(endStats.HeapType[1].BlockCount >= begStats.HeapType[1].BlockCount);
    CHECK_BOOL(endStats.HeapType[1].AllocationCount >= begStats.HeapType[1].AllocationCount + count);
    CHECK_BOOL(endStats.HeapType[1].UsedBytes >= begStats.HeapType[1].UsedBytes + count * bufSize);
    CHECK_BOOL(endStats.HeapType[1].AllocationSizeMin <= bufSize);
    CHECK_BOOL(endStats.HeapType[1].AllocationSizeMax >= bufSize);

    CHECK_BOOL(StatInfoEqual(begStats.HeapType[0], endStats.HeapType[0]));
    CHECK_BOOL(StatInfoEqual(begStats.HeapType[2], endStats.HeapType[2]));

    CheckStatInfo(endStats.Total);
    CheckStatInfo(endStats.HeapType[0]);
    CheckStatInfo(endStats.HeapType[1]);
    CheckStatInfo(endStats.HeapType[2]);

    D3D12MA::Budget gpuBudget = {}, cpuBudget = {};
    ctx.allocator->GetBudget(&gpuBudget, &cpuBudget);
    
    CHECK_BOOL(gpuBudget.AllocationBytes <= gpuBudget.BlockBytes);
    CHECK_BOOL(gpuBudget.AllocationBytes == endStats.HeapType[0].UsedBytes);
    CHECK_BOOL(gpuBudget.BlockBytes == endStats.HeapType[0].UsedBytes + endStats.HeapType[0].UnusedBytes);
    
    CHECK_BOOL(cpuBudget.AllocationBytes <= cpuBudget.BlockBytes);
    CHECK_BOOL(cpuBudget.AllocationBytes == endStats.HeapType[1].UsedBytes + endStats.HeapType[2].UsedBytes);
    CHECK_BOOL(cpuBudget.BlockBytes == endStats.HeapType[1].UsedBytes + endStats.HeapType[1].UnusedBytes +
        endStats.HeapType[2].UsedBytes + endStats.HeapType[2].UnusedBytes);
}

static void TestTransfer(const TestContext& ctx)
{
    wprintf(L"Test mapping\n");

    const UINT count = 10;
    const UINT64 bufSize = 32ull * 1024;
    
    ResourceWithAllocation resourcesUpload[count];
    ResourceWithAllocation resourcesDefault[count];
    ResourceWithAllocation resourcesReadback[count];

    D3D12MA::ALLOCATION_DESC allocDescUpload = {};
    allocDescUpload.HeapType = D3D12_HEAP_TYPE_UPLOAD;
    D3D12MA::ALLOCATION_DESC allocDescDefault = {};
    allocDescDefault.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12MA::ALLOCATION_DESC allocDescReadback = {};
    allocDescReadback.HeapType = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    // Create 3 sets of resources.
    for(UINT i = 0; i < count; ++i)
    {
        D3D12MA::Allocation* alloc = nullptr;
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDescUpload,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &alloc,
            IID_PPV_ARGS(&resourcesUpload[i].resource)) );
        resourcesUpload[i].allocation.reset(alloc);

        CHECK_HR( ctx.allocator->CreateResource(
            &allocDescDefault,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            NULL,
            &alloc,
            IID_PPV_ARGS(&resourcesDefault[i].resource)) );
        resourcesDefault[i].allocation.reset(alloc);

        CHECK_HR( ctx.allocator->CreateResource(
            &allocDescReadback,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            NULL,
            &alloc,
            IID_PPV_ARGS(&resourcesReadback[i].resource)) );
        resourcesReadback[i].allocation.reset(alloc);
    }

    // Map and fill data in UPLOAD.
    for(UINT i = 0; i < count; ++i)
    {
        void* mappedPtr = nullptr;
        CHECK_HR( resourcesUpload[i].resource->Map(0, &EMPTY_RANGE, &mappedPtr) );

        FillData(mappedPtr, bufSize, i);

        // Unmap every other resource, leave others mapped.
        if((i % 2) != 0)
        {
            resourcesUpload[i].resource->Unmap(0, NULL);
        }
    }

    // Transfer from UPLOAD to DEFAULT, from there to READBACK.
    ID3D12GraphicsCommandList* cmdList = BeginCommandList();
    for(UINT i = 0; i < count; ++i)
    {
        cmdList->CopyBufferRegion(resourcesDefault[i].resource, 0, resourcesUpload[i].resource, 0, bufSize);
    }
    D3D12_RESOURCE_BARRIER barriers[count] = {};
    for(UINT i = 0; i < count; ++i)
    {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource = resourcesDefault[i].resource;
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(count, barriers);
    for(UINT i = 0; i < count; ++i)
    {
        cmdList->CopyBufferRegion(resourcesReadback[i].resource, 0, resourcesDefault[i].resource, 0, bufSize);
    }
    EndCommandList(cmdList);

    // Validate READBACK buffers.
    for(UINT i = count; i--; )
    {
        const D3D12_RANGE mapRange = {0, bufSize};
        void* mappedPtr = nullptr;
        CHECK_HR( resourcesReadback[i].resource->Map(0, &mapRange, &mappedPtr) );

        CHECK_BOOL( ValidateData(mappedPtr, bufSize, i) );

        // Unmap every 3rd resource, leave others mapped.
        if((i % 3) != 0)
        {
            resourcesReadback[i].resource->Unmap(0, &EMPTY_RANGE);
        }
    }
}

static void TestZeroInitialized(const TestContext& ctx)
{
    wprintf(L"Test zero initialized\n");

    const UINT64 bufSize = 128ull * 1024;
    D3D12MA::Allocation* alloc = nullptr;

    D3D12_RESOURCE_DESC resourceDesc;
    FillResourceDescForBuffer(resourceDesc, bufSize);

    // # Create upload buffer and fill it with data.

    D3D12MA::ALLOCATION_DESC allocDescUpload = {};
    allocDescUpload.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    ResourceWithAllocation bufUpload;
    CHECK_HR( ctx.allocator->CreateResource(
        &allocDescUpload,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        NULL,
        &alloc,
        IID_PPV_ARGS(&bufUpload.resource)) );
    bufUpload.allocation.reset(alloc);

    {
        void* mappedPtr = nullptr;
        CHECK_HR( bufUpload.resource->Map(0, &EMPTY_RANGE, &mappedPtr) );
        FillData(mappedPtr, bufSize, 5236245);
        bufUpload.resource->Unmap(0, NULL);
    }

    // # Create readback buffer
    
    D3D12MA::ALLOCATION_DESC allocDescReadback = {};
    allocDescReadback.HeapType = D3D12_HEAP_TYPE_READBACK;

    ResourceWithAllocation bufReadback;
    CHECK_HR( ctx.allocator->CreateResource(
        &allocDescReadback,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        NULL,
        &alloc,
        IID_PPV_ARGS(&bufReadback.resource)) );
    bufReadback.allocation.reset(alloc);

    auto CheckBufferData = [&](const ResourceWithAllocation& buf)
    {
        const bool shouldBeZero = buf.allocation->WasZeroInitialized() != FALSE;

        {
            ID3D12GraphicsCommandList* cmdList = BeginCommandList();
            cmdList->CopyBufferRegion(bufReadback.resource, 0, buf.resource, 0, bufSize);
            EndCommandList(cmdList);
        }

        bool isZero = false;
        {
            const D3D12_RANGE readRange{0, bufSize}; // I could pass pReadRange = NULL but it generates D3D Debug layer warning: EXECUTION WARNING #930: MAP_INVALID_NULLRANGE
            void* mappedPtr = nullptr;
            CHECK_HR( bufReadback.resource->Map(0, &readRange, &mappedPtr) );
            isZero = ValidateDataZero(mappedPtr, bufSize);
            bufReadback.resource->Unmap(0, &EMPTY_RANGE);
        }

        wprintf(L"Should be zero: %u, is zero: %u\n", shouldBeZero ? 1 : 0, isZero ? 1 : 0);

        if(shouldBeZero)
        {
            CHECK_BOOL(isZero);
        }
    };

    // # Test 1: Committed resource. Should always be zero initialized.

    {
        D3D12MA::ALLOCATION_DESC allocDescDefault = {};
        allocDescDefault.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        allocDescDefault.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

        ResourceWithAllocation bufDefault;
        CHECK_HR( ctx.allocator->CreateResource(
            &allocDescDefault,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            NULL,
            &alloc,
            IID_PPV_ARGS(&bufDefault.resource)) );
        bufDefault.allocation.reset(alloc);

        wprintf(L"  Committed: ");
        CheckBufferData(bufDefault);
        CHECK_BOOL( bufDefault.allocation->WasZeroInitialized() );
    }

    // # Test 2: (Probably) placed resource.

    ResourceWithAllocation bufDefault;
    for(uint32_t i = 0; i < 2; ++i)
    {
        // 1. Create buffer

        D3D12MA::ALLOCATION_DESC allocDescDefault = {};
        allocDescDefault.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        CHECK_HR( ctx.allocator->CreateResource(
            &allocDescDefault,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            NULL,
            &alloc,
            IID_PPV_ARGS(&bufDefault.resource)) );
        bufDefault.allocation.reset(alloc);

        // 2. Check it

        wprintf(L"  Normal #%u: ", i);
        CheckBufferData(bufDefault);

        // 3. Upload some data to it

        {
            ID3D12GraphicsCommandList* cmdList = BeginCommandList();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = bufDefault.resource;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);

            cmdList->CopyBufferRegion(bufDefault.resource, 0, bufUpload.resource, 0, bufSize);
            
            EndCommandList(cmdList);
        }

        // 4. Delete it

        bufDefault.Reset();
    }
}

static void TestMultithreading(const TestContext& ctx)
{
    wprintf(L"Test multithreading\n");

    const UINT threadCount = 32;
    const UINT bufSizeMin = 1024ull;
    const UINT bufSizeMax = 1024ull * 1024;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    // Launch threads.
    std::thread threads[threadCount];
    for(UINT threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        auto threadFunc = [&, threadIndex]()
        {
            RandomNumberGenerator rand(threadIndex);

            std::vector<ResourceWithAllocation> resources;
            resources.reserve(256);

            // Create starting number of buffers.
            const UINT bufToCreateCount = 32;
            for(UINT bufIndex = 0; bufIndex < bufToCreateCount; ++bufIndex)
            {
                ResourceWithAllocation res = {};
                res.dataSeed = (threadIndex << 16) | bufIndex;
                res.size = AlignUp<UINT>(rand.Generate() % (bufSizeMax - bufSizeMin) + bufSizeMin, 16);

                D3D12_RESOURCE_DESC resourceDesc;
                FillResourceDescForBuffer(resourceDesc, res.size);

                D3D12MA::Allocation* alloc = nullptr;
                CHECK_HR( ctx.allocator->CreateResource(
                    &allocDesc,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    NULL,
                    &alloc,
                    IID_PPV_ARGS(&res.resource)) );
                res.allocation.reset(alloc);
                
                void* mappedPtr = nullptr;
                CHECK_HR( res.resource->Map(0, &EMPTY_RANGE, &mappedPtr) );

                FillData(mappedPtr, res.size, res.dataSeed);

                // Unmap some of them, leave others mapped.
                if(rand.GenerateBool())
                {
                    res.resource->Unmap(0, NULL);
                }

                resources.push_back(std::move(res));
            }
            
            Sleep(20);

            // Make a number of random allocate and free operations.
            const UINT operationCount = 128;
            for(UINT operationIndex = 0; operationIndex < operationCount; ++operationIndex)
            {
                const bool removePossible = !resources.empty();
                const bool remove = removePossible && rand.GenerateBool();
                if(remove)
                {
                    const UINT indexToRemove = rand.Generate() % resources.size();
                    resources.erase(resources.begin() + indexToRemove);
                }
                else // Create new buffer.
                {
                    ResourceWithAllocation res = {};
                    res.dataSeed = (threadIndex << 16) | operationIndex;
                    res.size = AlignUp<UINT>(rand.Generate() % (bufSizeMax - bufSizeMin) + bufSizeMin, 16);
                    D3D12_RESOURCE_DESC resourceDesc;
                    FillResourceDescForBuffer(resourceDesc, res.size);

                    D3D12MA::Allocation* alloc = nullptr;
                    CHECK_HR( ctx.allocator->CreateResource(
                        &allocDesc,
                        &resourceDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        NULL,
                        &alloc,
                        IID_PPV_ARGS(&res.resource)) );
                    res.allocation.reset(alloc);

                    void* mappedPtr = nullptr;
                    CHECK_HR( res.resource->Map(0, NULL, &mappedPtr) );

                    FillData(mappedPtr, res.size, res.dataSeed);

                    // Unmap some of them, leave others mapped.
                    if(rand.GenerateBool())
                    {
                        res.resource->Unmap(0, NULL);
                    }

                    resources.push_back(std::move(res));
                }
            }

            Sleep(20);

            // Validate data in all remaining buffers while deleting them.
            for(size_t resIndex = resources.size(); resIndex--; )
            {
                void* mappedPtr = nullptr;
                CHECK_HR( resources[resIndex].resource->Map(0, NULL, &mappedPtr) );

                ValidateData(mappedPtr, resources[resIndex].size, resources[resIndex].dataSeed);

                // Unmap some of them, leave others mapped.
                if((resIndex % 3) == 1)
                {
                    resources[resIndex].resource->Unmap(0, &EMPTY_RANGE);
                } 

                resources.pop_back();
            }
        };
        threads[threadIndex] = std::thread(threadFunc);
    }

    // Wait for threads to finish.
    for(UINT threadIndex = threadCount; threadIndex--; )
    {
        threads[threadIndex].join();
    }
}

static void TestGroupVirtual(const TestContext& ctx)
{
    TestVirtualBlocks(ctx);
}

static void TestGroupBasics(const TestContext& ctx)
{
    TestFrameIndexAndJson(ctx);
    TestCommittedResourcesAndJson(ctx);
    TestCustomHeapFlags(ctx);
    TestPlacedResources(ctx);
    TestOtherComInterface(ctx);
    TestCustomPools(ctx);
    TestDefaultPoolMinBytes(ctx);
    TestAliasingMemory(ctx);
    TestMapping(ctx);
    TestStats(ctx);
    TestTransfer(ctx);
    TestZeroInitialized(ctx);
    TestMultithreading(ctx);
}

void Test(const TestContext& ctx)
{
    wprintf(L"TESTS BEGIN\n");

    if(false)
    {
        ////////////////////////////////////////////////////////////////////////////////
        // Temporarily insert custom tests here:
        return;
    }

    TestGroupVirtual(ctx);
    TestGroupBasics(ctx);

    wprintf(L"TESTS END\n");
}
