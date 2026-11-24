# Imported ray-tracing geometry on an RTX 4070 Ti

The test builds one triangle and a top-level acceleration structure through the Leonida Vulkan queue. A compute shader casts a ray through the first instance of the triangle and a second ray outside the scene. The expected pair is (1, 0).

| Instance descriptor or pointer-list layout | Before the fix | After the fix |
| --- | --- | --- |
| Guest and Vulkan device addresses match (control) | (1, 0) | (1, 0) |
| Guest descriptor address differs from its backing buffer's device address | (0, 0) | (1, 0) |
| Same descriptors, with an eight-byte alias over a sixteen-byte pointer list | Queue rejected the build with invalid_copy | (1, 0) |
| Descriptor buffer imported through VK_EXT_external_memory_host, guest VA equal to host pointer | (0, 0) | (1, 0) |

Both runs used the same Linux executable source, Vulkan device selection and shader. To distinguish the translations, the test arranges for the previous address calculation to point to a second allocated buffer holding masked instances. A ray that reaches those instances misses. Both candidate addresses refer to valid GPU memory.

The imported-memory case uses Vulkan directly to import a coherent host allocation. It exercises the runtime queue with the binding used for title-placed buffers. The Windows COM resource-creation path and retail boot remain unverified. Vulkan validation layers were disabled for the recorded runs.

## Direct geometry addresses

These cases compare the ten-commit runtime series with the same series plus direct-geometry.mbox. The first four cases above continued to pass on both revisions.

| Hardware measurement | Ten-commit runtime | With direct-geometry.mbox |
| --- | --- | --- |
| KHR, contiguous imported instance array | hit 0, miss 0 | hit 1, miss 0 |
| KHR, imported vertex view with an overlapping eight-byte alias | hit 0, miss 0 | hit 1, miss 0 |
| Default XDXR, triangle bounds from imported vertex data | (4, 0, 0) to (5, 1, 0) | (0, 0, 0) to (1, 1, 0) |

The vertex view starts sixteen bytes into the imported buffer. The alternate buffer holds the same triangle translated by four units along x. The XDXR test reads the resulting acceleration structure's bounds from GPU memory. A separate CPU queue regression checks index, transform and scratch addresses as well as vertices. It passed on native Linux and as a Windows executable under Wine. The full native runtime suite passed 69 tests and skipped two (translated shader corpus and opt-in synchronization validation).

## Reproduce on Linux

Use a C++20 compiler, Vulkan-Headers, GNU patch, glslangValidator, spirv-val and a Vulkan GPU that supports acceleration structures and ray queries. Starting in this directory, run

```sh
bash run.sh /path/to/Leonida /path/to/Vulkan-Headers/include
```

The header-directory argument is optional when Vulkan headers are on the compiler's search path. The script builds in a temporary directory. Its helper patch enables ray queries and acceleration structures on a temporary copy of Leonida's Vulkan test device. The source checkout stays clean. The shader uses the same hit and miss ray origins as the project's runtime/test/glsl/cs_rayquery.comp.

Run the command against the ten-commit series for the direct-geometry baseline (expected exit 1). Apply direct-geometry.mbox and run it again for the fixed behavior (expected exit 0). The source trees without the independent archive-selection patch are 039706d73610f47bb0cd7f2b387395f2df856533 and 004dbb2a7a52977f907370ba47662ea93e213a65 respectively. The first table's earlier baseline used the first nine commits, tree 42d7053bc0bfe2746d25e0aa8e4f9f646fd5b814. The executable exits 77 for an unavailable GPU or ray-query feature. It prints a skip when coherent host import is unavailable. All seven cases ran on the RTX 4070 Ti.
