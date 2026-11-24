# Windows presenter test initialization

Two tests on Mumogg’s integration branch convert every Vulkan device initialization error into CTest's skip code 77. This can hide a broken validation run. The patch uses the same three-way status check already present in `d3d12_present_test.cpp`. It changes only the two test entry points. [Integration PR #321](https://github.com/ubiquitarianism/leonida/pull/321)

## Observed process exits

Both before and after columns apply independently to each executable, `vk_presenter_sync_test` and `xbox_present_test`. The matrix covers 16 process launches. CTest registers 77 as the skip code for both tests.

| Setup | Before | After |
| --- | ---: | ---: |
| RTX 4070 Ti, validation disabled | 0 | 0 |
| Vulkan loader disabled in Wine | 77 | 77 |
| Explicit GPU selector matches no adapter | 77 | 1 |
| Requested Khronos validation layer missing from the Windows Vulkan loader | 77 | 1 |

I compiled the Windows test translation units with LLVM-MinGW and ran them under Wine on Xwayland. The Xbox test linked against the same previously cross-built runtime fixture objects in both builds. The GPU run exercised swapchain destruction in the sync test and 30 paced frames in the Xbox test. The adapter and validation-layer runs printed their respective initialization diagnostics. Dezmonius reported a separate crash after initialization with an installed Windows validation layer on an RTX 5070. Its cause remains open. [Original Windows report](https://github.com/ubiquitarianism/leonida/pull/321#issuecomment-7133087537)

## Reproduce

Check out integration commit `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. Build the two test targets and run the following from the configured build directory. Use a Windows host with a working Vulkan adapter or an equivalent Wine setup. These PowerShell commands select only the two presenter tests.

```powershell
$env:LEONIDA_VK_VALIDATION = '0'
Remove-Item Env:LEONIDA_VK_DEVICE -ErrorAction SilentlyContinue
ctest -C Release -R '^(vk_presenter_sync|xbox_present)$' --output-on-failure

$env:LEONIDA_VK_DEVICE = 'absent-adapter-375694'
ctest -C Release -R '^(vk_presenter_sync|xbox_present)$' --output-on-failure
Remove-Item Env:LEONIDA_VK_DEVICE
```

With a loader that lacks the Khronos layer, also repeat with `LEONIDA_VK_VALIDATION=1`. For the unavailable-loader control under Wine, disable validation and add `vulkan-1=d` to `WINEDLLOVERRIDES` for those processes. Apply `presenter-status.mbox` with `git am`, rebuild both targets and repeat.
