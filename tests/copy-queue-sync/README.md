# Vulkan copy-queue synchronization

An enhanced D3D12 copy barrier lost the Vulkan `CLEAR` stage when Leonida restricted the barrier to the copy queue. This affected both `WriteBufferImmediate` (recorded as `vkCmdUpdateBuffer`) and the zero fill used when resolving a timestamp that was never written (recorded as `vkCmdFillBuffer`). The reproducer exercises the production `VulkanQueue` with a synthetic command list on a dedicated transfer family.

The copy queue first saves the original bytes, then writes an immediate and resolves the timestamp, then copies both results back. The test checks the original snapshot, the written word, the zero-filled result and adjacent sentinel words. Khronos synchronization validation checks the dependencies independently of the returned values.

| Source with the same queue probe | Result on the RTX 4070 Ti |
| --- | --- |
| PR 314 at `bbc8999d` | Four hazards. Both writes raced the preceding read and the following reads raced both writes. |
| PR 314 plus PR 354's buffer-copy barrier | Two hazards. The preceding read still raced both writes. |
| PR 314 plus the queue-filter correction | The immediate and query readbacks passed. The separate backlog case reported ten write-after-write hazards. |
| Both corrected series | The copy-queue probe and the 256-submit backlog passed with zero validation errors. |

The queue-filter correction preserves `CLEAR` in both directions. The image initialization path still copies a zero buffer on transfer queues. A CPU test checks this with the image-clear function both present and absent, and checks that a compute queue still uses the colour-clear function. The buffer-copy follow-up checks during preparation that the ordering barrier callback is available.

The test ran on Linux with an NVIDIA GeForce RTX 4070 Ti, driver 620.70.06 and Khronos validation layer 1.4.363. The driver reported graphics family 0, compute family 2 and transfer-only family 1. All measurements used synthetic input. Retail startup was untested.

## Reproduce

All seven tests below passed on a clean application of the public mailboxes. On the integrated restoration checkout, 71 of 72 portable tests passed with validation enabled. The shader-corpus test skipped because no translated corpus was configured.

From a source checkout at `de4c37383cad386eaa851047628d7b92c51009c5`, set `PATCHES` to the absolute path of this patch repository.

```sh
git am "$PATCHES/copy-queue-sync.mbox" "$PATCHES/buffer-copy-order.mbox"
git rev-parse 'HEAD^{tree}'
cmake -S runtime -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target vk_queue_test vk_command_replay_test vk_sync_validation_selftest d3d12_state_map_test replay_clear_gate_test replay_copy_offset_test predicated_copy_test
LEONIDA_VK_VALIDATION=1 ctest --test-dir build -V -R '^(vk_queue|vk_command_replay|vk_sync_validation|d3d12_state_map|replay_clear_gate|replay_copy_offset|predicated_copy)$'
```

The combined tree is `b4f78a88976f889518be27950620ad5aaf1f3f91`. To reproduce the four-hazard control, apply just the first commit of the copy-queue series to the base (or check out `bbc8999d`) and copy the patched `runtime/test/vk_queue_test.cpp` into that checkout before building. The validation self-test deliberately submits one unsynchronized pair and one synchronized pair to verify that the layer can detect the difference.

The two mailboxes retain the original authors. The copy-scope mapping comes from kykifox1337-source and the buffer-copy ordering change from natterman12. The queue-filter, preparation check and additional tests are AI-assisted follow-ups. [PR 314](https://github.com/ubiquitarianism/leonida/pull/314) · [PR 354](https://github.com/ubiquitarianism/leonida/pull/354)
