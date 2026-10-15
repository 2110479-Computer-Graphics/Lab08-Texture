# Predictions

Fill this in BEFORE you run the measurement it predicts, and commit it before you
run. Wrong predictions cost nothing. Missing ones do, and so do predictions written
after the measurement. Your write-ups compare these against what you measured.

## Part III: filter cost

The panel times the floor with 16 texture reads per pixel, once per filter. Rank the
five filters from cheapest to most expensive, and guess the cost of anisotropic x16
as a multiple of trilinear's. One line of reasoning each.

| rank | filter | why |
|------|--------|-----|
| 1 (cheapest) | | |
| 2 | | |
| 3 | | |
| 4 | | |
| 5 (most expensive) | | |

aniso16 / trilinear = ____ x, because:

## Part IV: what grows fastest

From 1000 to 10000 particles, which method's time grows the most (unsorted,
sorted, WBOIT), and why?
