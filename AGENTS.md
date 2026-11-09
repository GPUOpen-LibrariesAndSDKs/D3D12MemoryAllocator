This repository contains D3D12 Memory Allocator (D3D12MA) - a software library implemented in C++, intended for developers who use Direct3D 12 on Windows. It supports any D3D12-compatible GPU. It is distributed under the MIT license.

# Requirements

Using D3D12MA requires:

- C++ compiler with at least C++14 support
- DirectX 12 headers available - either headers supplied by the Windows SDK or the DirectX 12 Agility SDK (both Retail and Preview versions are supported)
  - When using Agility SDK, set `D3D12MA_AGILITY_SDK_DIRECTORY` Cmake variable to appropriate directory.
  - When using preview Agility SDK, also set `D3D12MA_AGILITY_SDK_PREVIEW` Cmake option to `ON`.

# Scope

Using this library is not required to use D3D12, but it is helpful and recommended, as it simplifies some aspects of the D3D12 API: allocating heaps, creating buffers and textures. The library:

- Allocates larger heaps, tracks occupied and free ranges, and selects suitable ranges for placed resources.
- Automatically respects size and alignment requirements for created resources.
- Automatically handles the resource heap tier - whether it's `D3D12_RESOURCE_HEAP_TIER_1` that requires certain resource classes to be kept separate or `D3D12_RESOURCE_HEAP_TIER_2` that allows keeping them all together.

# How to use

D3D12MA is distributed in source form. The library is implemented entirely in two files: the header `include/D3D12MemAlloc.h` and the implementation `src/D3D12MemAlloc.cpp`. Other C++ source/header files you can see in this repository are sample, support, and test code, so you don't need to use them when developing software using this library.

# Library API

Full documentation of the library API is provided in Doxygen-style comments in `include/D3D12MemAlloc.h`.

The API is similar in style to D3D12. Errors are reported using `HRESULT` error codes returned from most functions `SUCCEEDED(hr)` / `FAILED(hr)` macros are recommended for checking them. C++ exceptions are not used. Library symbols are defined in the `D3D12MA` namespace.

The API is object-oriented. Classes derive from `IUnknown` and implement its interface, including thread-safe reference counting, so they follow a style similar to COM interfaces. They can be used with smart pointers designed for COM interfaces, like `Microsoft::WRL::ComPtr`. However, they are not true COM interfaces, they don't have GUIDs.

The most important classes are:

- `D3D12MA::Allocator` - represents the main allocator object. Creating only one per `ID3D12Device` and keeping it alive throughout the application's use of D3D12 is recommended.
  - The allocator must outlive allocations, pools, and other objects that depend on it.
- `D3D12MA::Allocation` - represents a specific region of allocated D3D12 heap memory and (optionally) a D3D12 resource. It may hold a reference to:
  - `ID3D12Heap` memory block or a subregion of such block (it is managed by D3D12MA internally)
  - `ID3D12Resource` created as a placed resource in a heap memory block or created as a committed resource (which then has its own implicit heap)
  - An allocation is, thus, either using a shared heap, owning a dedicated heap, or representing a committed resource with an implicit heap.

A typical workflow for using objects of this library is:

1. Initialize a description structure such as `D3D12MA::ALLOCATION_DESC`.
2. Call an object creation method like `D3D12MA::Allocator::CreateResource`. Receive a pointer to the created object. Check the result for success or error.
3. Use the object by calling its methods. Keep it alive as long as needed, carefully considering its lifetime and usage on both CPU and GPU.
4. When no longer needed, call its `Release` method, which decrements reference counter, destroying the object when the counter reaches 0.

Example showing the creation and usage of a buffer intended for data uploading:

```cpp
D3D12MA::Allocator* allocator = ...

// 1. Initialize description structures.
const UINT64 bufSize = 65536;

D3D12_RESOURCE_DESC resourceDesc = {};
resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
resourceDesc.Alignment = 0;
resourceDesc.Width = bufSize;
resourceDesc.Height = 1;
resourceDesc.DepthOrArraySize = 1;
resourceDesc.MipLevels = 1;
resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
resourceDesc.SampleDesc.Count = 1;
resourceDesc.SampleDesc.Quality = 0;
resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
 
D3D12MA::ALLOCATION_DESC allocationDesc = {};
allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
 
// 2. Create the allocation object, along with its resource.
D3D12MA::Allocation* allocation = NULL;
ID3D12Resource* resource = NULL;
HRESULT hr = allocator->CreateResource(&allocationDesc, &resourceDesc,
    D3D12_RESOURCE_STATE_COMMON, NULL, &allocation, IID_PPV_ARGS(&resource));
// Check hr...
 
// 3. Use the resource, for example:
const float* bufData = ...;
D3D12_RANGE emptyRange = {0, 0};
void* mappedPtr = NULL;
hr = resource->Map(0, &emptyRange, &mappedPtr);
// Check hr...
memcpy(mappedPtr, bufData, bufSize);
resource->Unmap(0, NULL);

// ...

// 4. When no longer needed or used, destroy the objects.
resource->Release();
allocation->Release();
```

DirectX 12 Agility SDK offers a library of helpers in files `build\native\include\d3dx12\*.h`. They include structures that help with complete and concise initialization of the core D3D12 `*_DESC` structures. For example, the structure `CD3DX12_RESOURCE_DESC` can be used to conveniently fill in structure `D3D12_RESOURCE_DESC`. Similarly, this library provides a set of helper structures that aid in initialization of some of the `*_DESC` structures defined in the library. Their usage is recommended, along with the usage of some smart pointers for both D3D12 and D3D12MA objects. For example:

```cpp
D3D12MA::CALLOCATION_DESC allocationDesc{D3D12_HEAP_TYPE_UPLOAD};

CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
 
ComPtr<D3D12MA::Allocation> allocation;
ComPtr<ID3D12Resource> resource;
HRESULT hr = allocator->CreateResource(&allocationDesc, &resourceDesc,
    D3D12_RESOURCE_STATE_COMMON, NULL, &allocation, IID_PPV_ARGS(&resource));
```

# Object references, ownership, and lifetime

Care must be taken to correctly manage object lifetime and references, to avoid memory leaks or use-after-free bugs.

- If an `Allocation` has been created together with a resource (using methods like `Allocator::CreateResource`), the allocation object holds a reference to the resource and releases it when the allocation is destroyed.
  - An allocation object without a resource may be created with `Allocator::AllocateMemory`, but it is not recommended for typical usage.
- If an `Allocation` has been created with its own dedicated `ID3D12Heap` object, the allocation object holds a reference to the heap and releases it when the allocation is destroyed.
  - Such an allocation may be created, for example, by using `Allocator::AllocateMemory` with `ALLOCATION_FLAG_COMMITTED`, but it is not recommended for typical usage.
- If an `Allocation` object points to a subregion of a larger `ID3D12Heap`, that heap is managed internally by D3D12MA and may be used for other non-overlapping allocations.
- If you receive a pointer to a resource created together with the allocation using methods such as `Allocator::CreateResource` (by passing a non-null `ppvResource` argument), the reference count of the resource is incremented.
  - To release the objects properly, you then need to call `Release` on both the allocation and the resource objects.
- The `ID3D12Resource` and `ID3D12Heap` associated with an allocation can be queried later by calling methods `Allocation::GetResource`, `Allocation::GetHeap`, respectively.
  - `GetHeap` returns null for committed resources with implicit heaps.
  - `GetResource` can return null for raw allocations with no resource attached.
  - Calling these methods does not increment the returned object's reference count. Callers must not release that borrowed reference unless they separately acquired ownership, for example through `AddRef` or a smart-pointer copy.

Resources and allocations shouldn't be released when they may still be in use on the GPU device, so CPU-side C++ code may need to wait for the GPU to finish (e.g. using a D3D12 fence) before releasing such objects.
