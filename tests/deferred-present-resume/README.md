# Deferred Present resumes after the queue drains

On rizzzz64’s Reforge PR #86, the existing GPU test fills eight presentation slots, cancels a ninth request and checks that the eight admitted frames drain. The attached test also starts a caller that wants to keep waiting. Its abort callback signals a promise when it observes the full queue. The test releases the held work, requires the caller to return `VK_SUCCESS` and checks that the presented-frame count increases by exactly nine. The callback has a 20-second deadline for the wait. [Upstream PR](https://github.com/LeonidaReforge/Leonida/pull/86)

| Revision or fixture | Wine/RTX 4070 Ti runs | Result |
| --- | ---: | --- |
| Unmodified parent `2ebde81ec07e`, parent test | 1 | Passed |
| Unmodified PR `b84739e39aee`, PR test | 4 | Cancellation and all existing cases passed |
| Same PR with the attached test | 6 | Waiting caller observed capacity, resumed and presented its frame in every run |

Tests used LLVM-MinGW 23.1.6, Wine 11.22 and Xwayland. One run in each PR row enabled the Linux Khronos validation layer 1.4.363 with synchronization validation beneath Wine. Each reported one host device-extension warning, VUID 01387. A separate zero-extension Wine control had independently isolated the same warning. [Control and native Linux comparison](https://github.com/Sigtryggur-Mani-Gudmundsson/graphics-runtime-patches/blob/cebaf9c036bd26878bfcc08c2d85b7bc6149f25f/tests/presentation-validation/host-layer.md)

To repeat, check out `b84739e39aee69d2611dfc3982cc2ef35f876916`, apply `presenter-resume.mbox` with `git am`, rebuild `vk_presenter_sync_test` and run it on a Vulkan presentation device. The added case prints `full queue drained and the waiting frame was presented once`. The process must return 0 and reach the final test success marker. Exit 77 means the GPU path was skipped. PortBridge’s earlier Windows measurements and standalone build instructions give a second platform for comparison. [Native Windows report](https://github.com/LeonidaReforge/Leonida/pull/86#issuecomment-7130696664)
