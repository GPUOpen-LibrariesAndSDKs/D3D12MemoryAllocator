//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
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

#include "Tests.h"
#include "Common.h"
#include <thread>

extern ID3D12GraphicsCommandList* BeginCommandList();
extern void EndCommandList(ID3D12GraphicsCommandList* cmdList);

struct AllocationDeleter
{
    void operator()(D3D12MA::Allocation* obj) const
    {
        if(obj)
        {
            obj->Release();
        }
    }
};

typedef std::unique_ptr<D3D12MA::Allocation, AllocationDeleter> AllocationUniquePtr;

struct ResourceWithAllocation
{
    CComPtr<ID3D12Resource> resource;
    AllocationUniquePtr allocation;
    UINT64 size = UINT64_MAX;
    UINT dataSeed = 0;
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

static void TestCommittedResources(const TestContext& ctx)
{
    wprintf(L"Test committed resources\n");
    
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
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

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
}

static void TestPlacedResources(const TestContext& ctx)
{
    wprintf(L"Test placed resources\n");

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
        CHECK_BOOL( resources[i].allocation->GetHeap() != NULL );
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
    CHECK_BOOL(sameHeapFound);

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
        CHECK_HR( resources[i].resource->Map(0, NULL, &mappedPtr) );

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
        CHECK_HR( resourcesUpload[i].resource->Map(0, NULL, &mappedPtr) );

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
            const D3D12_RANGE writtenRange = {0, 0};
            resourcesReadback[i].resource->Unmap(0, &writtenRange);
        }
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
            const UINT bufToCreateCount = 64;
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
                CHECK_HR( res.resource->Map(0, NULL, &mappedPtr) );

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
                    D3D12_RANGE writtenRange = {0, 0};
                    resources[resIndex].resource->Unmap(0, &writtenRange);
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

static void TestGroupBasics(const TestContext& ctx)
{
    TestCommittedResources(ctx);
    TestPlacedResources(ctx);
    TestMapping(ctx);
    TestStats(ctx);
    TestTransfer(ctx);
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

    TestGroupBasics(ctx);

    wprintf(L"TESTS END\n");
}
