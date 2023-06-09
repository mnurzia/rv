# Read comments in a source file for annotations.
# argv[1]: source file
# argv[2]: annotation type

import re
import sys

ANNO_REGEX = r".*\/\*I(.*)\*\/.*"


def anno_type(c):
    return ANNO_REGEX.replace("I", c)


if __name__ == "__main__":
    annos = {}
    import itertools
    with open(sys.argv[1], 'r') as f:
        for i, l in enumerate(f.readlines()):
            if not (m := re.match(anno_type(sys.argv[2]), l)):
                continue
            for inst in m.group(1).split("->")[0].strip().split(", "):
                annos[inst.strip()] = i+1
    annos = list(sorted(annos.items()))
    rows = [annos[i:i+8] for i in range(0, len(annos), 8)]
    col_w = list(
        map(max, [[len(r[i][0]) for r in rows if i < len(r)] for i in range(0, 8)]))
    for chnk in rows:
        print("- ", end="")
        for i, (inst, l) in enumerate(chnk):
            print(f"[`{inst.ljust(max(col_w))}`](rv.c#L{l})", end="")
        print()
