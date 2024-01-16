# Read comments in a source file for annotations.
from argparse import ArgumentParser, FileType
import re
import sys

ANNO_REGEX = r".*\/\*I(.*)\*\/.*"


def anno_type(c):
    return ANNO_REGEX.replace("I", c)


if __name__ == "__main__":
    ap = ArgumentParser()
    ap.add_argument("-t", "--annotation_type", type=str, default='I')
    ap.add_argument("-c", "--columns", type=int, default=8)
    ap.add_argument("in_file", type=FileType('r'), default=sys.stdin)
    
    args = ap.parse_args()
    
    annotations = {}
    
    for i, line in enumerate(args.in_file):
        if not (m := re.match(anno_type(args.annotation_type), line)):
            continue
        for inst in m.group(1).split("->")[0].strip().split(", "):
            annotations[inst.strip()] = i+1

    annotations = list(sorted(annotations.items()))

    rows = [annotations[i:i+args.columns] for i in range(0, len(annotations), args.columns)]

    column_widths = list(
        map(max, [[len(r[i][0]) for r in rows if i < len(r)] for i in range(0, args.columns)]))
    
    for chunk in rows:
        print("- ", end="")
        for i, (inst, line) in enumerate(chunk):
            print(f"[`{inst.ljust(max(column_widths))}`]({args.in_file.name}#L{line})", end="")
        print()
