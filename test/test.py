import glob
import subprocess
import sys

tests = sorted([x for x in glob.glob("vectors/*") if not x.endswith(".dmp")])

for test in tests:
    print(test, end="", flush=True)
    p = subprocess.run(["./run_test", test], timeout=2, capture_output=True)
    if p.returncode != 0:
        print("...FAIL")
    else:
        print("...OK")
