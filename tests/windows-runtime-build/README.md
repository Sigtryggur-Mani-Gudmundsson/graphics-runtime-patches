# Windows runtime build script on the integration branch

On integration head `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`, PowerShell rejects line 84 of `scripts/build_runtime.ps1` before it can choose CMake or direct MSVC. A merge placed the names of the DirectStorage and Xbox BVH builder source files inside the expression that quotes each path. The [mailbox](runtime-build.mbox) packages Tyrannys's published one-line correction. Tyrannys also reported a successful Windows 11/MSVC runtime build. Otaviosza independently reported the parser failure. [Original repair](https://github.com/ubiquitarianism/leonida/pull/321#issuecomment-7123053598) · [Independent report](https://github.com/ubiquitarianism/leonida/pull/321#issuecomment-7133406698)

## Apply and verify

From this directory with the integration head checked out, run the following. The expected Git tree after applying the patch is `dcf10144f4578aeaa266d40c0641e54cfa1ea4d2`.

```sh
git -C /path/to/checkout am "$PWD/runtime-build.mbox"
pwsh -NoProfile -File ./check-build-commands.ps1 -Checkout /path/to/checkout
python -I /path/to/checkout/tests/test_build_runtime_fallback.py
```

The [PowerShell test](check-build-commands.ps1) parses the complete build script, evaluates its command-generation branches, and checks the direct compiler command against the CMake launcher sources, the linked root-signature source and the four task-queue sources. It uses a checkout path containing spaces and an ampersand.

| Check | Original integration head | With Tyrannys's repair |
| --- | --- | --- |
| PowerShell 7.7 parser | Unexpected token on line 84 | Passed |
| Upstream Python fallback suite on Linux | 1 failed, 8 passed, 1 Windows-only skip | 9 passed, 1 Windows-only skip |
| Build command generation | Blocked by parser | Both CMake/Ninja and direct compiler commands generated |

I also sent the generated direct compiler line through Wine's `cmd.exe`, replacing the compiler with a Windows executable that compared its actual source arguments to the expected paths. All 28 matched byte for byte. This checks batch quoting and argument delivery. A native direct-`cl.exe` compilation remains unverified here. Verification and packaging were AI-assisted with Codex.
