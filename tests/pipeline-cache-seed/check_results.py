"""Exercise the integration branch's cache test through CTest without requiring a GPU."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


GOOD = "cs pipelines: 1 ok, 0 failed, 0 skipped"
SKIPPED = "cs pipelines: 0 ok, 0 failed, 1 skipped"
# Checker exit, checker output, cache exists, reader exit, reader output, CTest verdict.
CASES = {
    "both_pass": (0, GOOD, True, 0, "cache hit", "pass"),
    "checker_crashed_after_save": (9, GOOD, True, 0, "cache hit", "fail"),
    "missing_cache": (0, GOOD, False, 0, "", "fail"),
    "missing_summary": (0, "", True, 0, "", "fail"),
    "checker_silent_exit9": (9, "", False, 0, "", "fail"),
    "incidental_loader": (1, "vulkan-1.dll loaded; pipeline compilation failed", False, 0, "", "fail"),
    "loader_unavailable": (1, "could not load vkGetInstanceProcAddr from vulkan-1.dll", False, 0, "", "skip"),
    "no_physical_device": (1, "no Vulkan physical devices found", False, 0, "", "skip"),
    "device_filter": (1, "LEONIDA_VK_DEVICE=none: no Vulkan physical device matches", False, 0, "", "skip"),
    "pipeline_skipped": (0, SKIPPED, True, 0, "", "skip"),
    "skip_followed_by_save_error": (1, SKIPPED + "\ncache write failed", False, 0, "", "fail"),
    "reader_unavailable": (0, GOOD, True, 77, "SKIP: no usable cache feedback", "skip"),
    "reader_failure_mentions_skip": (0, GOOD, True, 1, "SKIP: optional feature\ncache test failed", "fail"),
}


def run_double(case, args):
    checker_rc, checker_log, cache, reader_rc, reader_log, _ = CASES[case]
    if "--jobs" in args:
        if cache:
            Path(args[2]).write_bytes(b"fixture")
        print(checker_log)
        return checker_rc
    print(reader_log)
    return reader_rc


def verify(checkout):
    runtime = checkout.resolve() / "runtime"
    driver = runtime / "test/check_pipeline_cache_seed.cmake"
    property_line = re.search(r"set_tests_properties\(pipeline_cache_seed PROPERTIES SKIP_REGULAR_EXPRESSION [^\n]+",
                              (runtime / "CMakeLists.txt").read_text()).group()
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp)
        fixture = source / "fixture"
        fixture.touch()
        project = ["cmake_minimum_required(VERSION 3.24)", "project(verdicts NONE)", "enable_testing()"]
        for name in CASES:
            project += [f'add_test(NAME {name} COMMAND "${{CMAKE_COMMAND}}"',
                        f'  "-DRUNNER={Path(sys.executable).as_posix()};{Path(__file__).resolve().as_posix()};--double;{name}"',
                        f'  "-DCHECKER={fixture.as_posix()}" "-DTEST={fixture.as_posix()}" "-DSHADER={fixture.as_posix()}"',
                        f'  "-DTEST_DIR={source.as_posix()}/case-{name}" -P "{driver.as_posix()}")',
                        property_line.replace("set_tests_properties(pipeline_cache_seed ", f"set_tests_properties({name} ")]
        (source / "CMakeLists.txt").write_text("\n".join(project) + "\n")
        build = source / "build"
        subprocess.run(["cmake", "-S", source, "-B", build], check=True, stdout=subprocess.DEVNULL)
        # The matrix deliberately contains failing CTest cases. Inspect their individual verdicts.
        subprocess.run(["ctest", "--test-dir", build, "--output-junit", "results.xml"], capture_output=True)
        verdicts = {}
        for case in ET.parse(build / "results.xml").iter("testcase"):
            verdicts[case.get("name")] = ("skip" if case.find("skipped") is not None else
                                         "fail" if case.find("failure") is not None else "pass")
        for name, data in CASES.items():
            print(f"{name}: expected {data[-1]}, observed {verdicts.get(name, 'missing')}")
        return int(any(verdicts.get(name) != data[-1] for name, data in CASES.items()))


if __name__ == "__main__":
    if sys.argv[1:2] == ["--double"]:
        sys.exit(run_double(sys.argv[2], sys.argv[3:]))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    sys.exit(verify(parser.parse_args().checkout))
