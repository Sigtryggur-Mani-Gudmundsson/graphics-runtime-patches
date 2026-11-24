"""Run the two-device fixture and verify each device's own arena allocation."""

import os
import re
import subprocess
import sys

if len(sys.argv) < 2:
    sys.exit("usage: python check-ring.py [wine] geometry-ring-test.exe")

run = subprocess.run(
    sys.argv[1:],
    env={**os.environ, "LEONIDA_ARENA_LOG": "1"},
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    errors="replace",
)
print(run.stdout, end="")
if run.returncode:
    sys.exit(run.returncode)

# Each device reports its arena before destruction. The process may reuse the same VA.
reports = run.stdout.split("[rt] d3d12: GPU address arena: ")
if len(reports) != 3:
    sys.exit("expected two device arena reports")
for device, report in enumerate(reports[:2], start=1):
    allocations = re.findall(r"arena shader VA 0x[0-9a-fA-F]+ \+ 0x1000000\b", report)
    if len(allocations) != 1 or report.count("Geometry readback ") != 1:
        sys.exit(f"device {device} did not reserve one geometry ring and complete a readback")
print("Both devices reserved their own geometry ring and passed pixel readback.")
