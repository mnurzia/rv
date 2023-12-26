from argparse import ArgumentParser, FileType
from re import match
from sys import stdin

IFDEF_RE = r"^[ \t]*#[ \t]*if(?:def)?[ \t]*(.*)[ \t]*(?:\/\*.*\*\/)?$"
ENDIF_RE = r"^[ \t]*#[ \t]*endif[ \t]*$"


if __name__ == "__main__":
  parser = ArgumentParser()
  parser.add_argument("input", type=FileType('r'), default=stdin, nargs='?')
  parser.add_argument("output", type=str, default='--', nargs='?')
  
  args = parser.parse_args()

  ifdef_stk = []
  lines = args.input.readlines()
  args.input.close()
  out_file = FileType('w')(args.output)
  for line in lines:
    if (match_obj := match(IFDEF_RE, line)):
      ifdef_stk.append(match_obj.groups()[0])
    elif (match_obj := match(ENDIF_RE, line)):
      line = line.rstrip("\n") + f" /* {ifdef_stk.pop()} */\n"
    out_file.write(line)
