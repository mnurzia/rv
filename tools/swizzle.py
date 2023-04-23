import itertools
import operator
import sys

l = int(sys.argv[1])

# s = "_[2:0]|offset[5:3]|rs1[2:0]|offset[2:2,6:6]|rs2[2:0]|_[1:0]"
s = sys.argv[2]

# split fields
fields = s.split("|")

i = l - 1

mapping = []

while i >= 0:
    f = fields.pop(0)
    name, spec = f.split("[")
    assert spec.endswith("]")
    spec = spec.rstrip("]")
    rngs = spec.split(",")
    for rng in rngs:
        nums = list(map(int, rng.split(":")))
        if len(nums) == 1:
            nums = [nums[0], nums[0]]
        assert nums[1] <= nums[0]
        for j in range(nums[0], nums[1] - 1, -1):
            assert i >= 0  # spec too big
            i -= 1
            mapping.append((name, j))

assert len(fields) == 0  # spec too big

od = {}

for j, (s, i) in enumerate(mapping):
    k = l - j - 1
    if s == "_":
        continue
    od[s] = od.get(s, []) + [(i, k)]


for k, v in od.items():
    sv = list(enumerate(sorted(v, reverse=True)))
    print(f"{k} = ", end="")
    for i, (_, g) in enumerate(itertools.groupby(sv, key=lambda x: (l - x[0]) - x[1][1])):
        g = list(map(operator.itemgetter(1), g))
        assert len(g) != 0
        if i != 0:
            print(" | ", end="")
        sh = "" if g[-1][0] == 0 else f" << {g[-1][0]}"
        if len(g) > 1:
            print(f"rv_ibf(c, {g[0][1]}, {g[-1][1]}){sh}", end="")
        else:
            print(f"rv_ib(c, {g[0][1]}) << {g[-1][0]}", end="")
    print()
