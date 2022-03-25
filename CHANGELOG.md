# 2.0.0 (2022-03-25)

So much has changed since the first release that it doesn’t make much sense to compare the differences. Here are the most important features that the library now provides:

- Powerful custom pools, which give an opportunity to not only keep certain resources together, reserve some minimum or limit the maximum amount of memory they can take, but also to pass additional allocation parameters unavailable to simple allocations. Among them, probably the most interesting is `POOL_DESC::HeapProperties`, which allows you to specify parameters of a custom memory type, which may be useful on UMA platforms. Committed allocations can now also be created in custom pools.
- The API for statistics and budget has been redesigned - see structures `Statistics`, `Budget`, `DetailedStatistics`, `TotalStatistics`.
- The library exposes its core allocation algorithm via the “virtual allocator” interface. This can be used to allocate pieces of custom memory or whatever you like, even something completely unrelated to graphics.
- The allocation algorithm has been replaced with the new, more efficient TLSF.
- Added support for defragmentation.
- Objects of the library can be used with smart pointers designed for COM objects.

# 1.0.0 (2019-09-02)

First published version.
