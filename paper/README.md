# Manuscript

`main.tex` is the range-add/range-sum correctness paper, moved here unchanged
from local notes so the manuscript is version-controlled. It is currently
IEEE-conference formatted and predates the pilot campaign; retargeting it to
the journal format and reconciling it with the empirical report happens in the
manuscript pull request of the research plan, not here.

The canonical title for that forthcoming Route B manuscript is:

> Partial Persistence Strategies for In-Memory Segment Trees under Additive Range Updates:
> Structural Space Characterizations and a Controlled Space–Time Study

The existing `main.tex` title remains the historical pre-Phase-3 scaffold title until the
planned manuscript rewrite; changing only its title would make its current abstract and evidence
claims inconsistent.

Build: `latexmk -pdf -cd paper/main.tex`.
