import glob
import subprocess
import sys

tests = [x for x in glob.glob("tests/*") if not x.endswith(".dmp")]

for test in tests:
    print(test, end="", flush=True)
    p = subprocess.run(["./bin/rv", test], timeout=2, capture_output=True)
    if p.returncode != 0:
        print("...FAIL")
    else:
        print("...OK")
