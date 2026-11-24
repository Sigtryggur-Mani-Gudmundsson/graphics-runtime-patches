# Pipeline cache test verdicts under Wine

The integration branch reports a silent checker exit 9 in its pipeline cache seed test. I ran Senne Cools's checker and cache reader from commit `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b` against the branch's synthetic counted-dispatch shader. Wine 11.22 on an RTX 4070 Ti produced a cache hit in the runtime with both the original and patched test. The local Windows build used LLVM-MinGW and the existing cross-built runtime objects. The cause of the original exit 9 on the reporter's machine remains open. [Mumogg's integration report](https://github.com/ubiquitarianism/leonida/pull/321)

A separate regression lets a failing checker pass if it prints the successful pipeline summary and writes the cache before exiting. The wrapper also skipped genuine failures when logs mentioned the loader or contained a skip message. The [patch](cache-verdicts.mbox) checks the exit status, recognizes the checker's specific skip conditions, and uses a wrapper-specific CTest skip marker. In the runtime reader it distinguishes a failed same-device control pipeline from a completed control that reported no cache hit.

## Reproduce

Apply the mailbox to the integration commit above and run the fixture from this directory. It uses Python 3 and CMake/CTest 3.24 or later. Applying the mailbox produced tree `607528e1faed209e2ff1d114a072d6193b23b065`.

```sh
git -C /path/to/checkout am "$PWD/cache-verdicts.mbox"
python3 check_results.py /path/to/checkout
```

The [fixture](check_results.py) invokes the branch's CMake driver and CTest skip property with controlled checker and reader outcomes. Four of 13 verdicts were wrong on the original branch. All 13 matched expectations after the patch.

| Condition | Original CTest | Patched CTest |
| --- | --- | --- |
| Checker saved a cache, printed success, then exited 9 | Passed | Failed |
| Compilation failed after the log mentioned vulkan-1.dll | Skipped | Failed |
| Checker skipped a pipeline, then failed to save | Skipped | Failed |
| Reader exited 1 and an earlier diagnostic contained SKIP | Skipped | Failed |
| Checker exited 9 without output | Failed | Failed |

For the hardware test, build the branch's `pipeline_cache_seed_test` target and run `ctest -R '^pipeline_cache_seed$' -V` in its build directory. I also instrumented the reader locally to force a seed miss and exercise its control path on the GPU. A failed control pipeline changed from exit 77 to exit 1. A completed control with no reported hit still exited 77. A reported hit in the control paired with a forced miss in the original seed still exited 1. The hardware and diagnostic tests used synthetic data. Developed and tested with Codex.
