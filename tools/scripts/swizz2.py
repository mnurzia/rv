from argparse import ArgumentParser
from dataclasses import dataclass

@dataclass
class Field:
  hi: int
  lo: int

  def __init__(self, s: str):
    start, *end = s.split(":")
    self.hi = int(start)
    self.lo = self.hi if not len(end) else int(end[0])
  
  def width(self) -> int:
    return self.hi - self.lo + 1

if __name__ == "__main__":
  ap = ArgumentParser()
  ap.add_argument("-n", "--name", type=str, default="i")
  ap.add_argument("offset", type=int)
  ap.add_argument("field", type=Field, nargs='+')

  args = ap.parse_args()

  src_field_width = sum(field.width() for field in args.field)

  expresh: str = ""

  for i, field in enumerate(args.field):
    src_hi = src_field_width + args.offset - 1
    src_lo = src_hi - field.width() + 1
    if i:
      expresh += " | "
    if field.width() > 1:
      expresh += f"rv_tbf({args.name}, {src_hi}, {src_lo}, {field.lo})"
    else:
      expresh += f"rv_tb({args.name}, {src_hi}, {field.lo})"
    src_field_width -= field.width()
  
  print(expresh)

  