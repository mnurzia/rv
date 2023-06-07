# Read comments in a source file for annotations.

import re
import sys

ANNO_REGEX = r".*\/\*I(.*)\*\/.*"


def anno_type(c):
    return ANNO_REGEX.replace("I", c)


annos = {}

if __name__ == "__main__":
    with open(sys.argv[1], 'r') as f:
        for i, l in enumerate(f.readlines()):
            if not (m := re.match(anno_type(sys.argv[2]), l)):
                continue
            for inst in m.group(1).split("->")[0].strip().split(", "):
                annos[inst.strip()] = i+1
    for inst, l in sorted(annos.items()):
        print(f"{inst:<16}\t{l}")
