# Review gate & hooks

`git config core.hooksPath .githooks` (local, per-clone — re-run after a
fresh clone). The pipeline every commit rides:

1. **commit-msg** — strips AI-attribution jargon (Co-Authored-By: Claude,
   "Generated with …", 🤖 lines). History reads as the project's.
2. **post-commit / post-merge** — auto-push the current branch to origin in
   the BACKGROUND; everything logs to `.git/autopush.log`. A review block or
   network failure is silent otherwise — CHECK THE LOG after committing.
   Bypass: `NO_AUTOPUSH=1`.
3. **pre-push** — stage 1: clang-format (blocking) + clang-tidy (advisory)
   on changed Source files. Stage 2: the `adversarial-reviewer` agent
   (`.claude/agents/adversarial-reviewer.md`) reviews the outgoing range
   headless; the verdict is parsed from the LAST `VERDICT:` token (findings
   may quote the contract — a whole-transcript grep was spoofable and the
   reviewer itself caught it). BLOCKER findings stop the push. Bypass:
   `SKIP_REVIEW=1 git push` (reserve for mechanical commits, e.g. the tree
   reformat).

## Track record (why this exists)

The gate's first runs found: an ANR from session rebuilds joining download
threads on the UI thread; path traversal via API-supplied ids; a
mutate-while-RT-reads race in demo buffers; raw mic leaking through the
unwritten block tail; a stale-index misbind in async callbacks; a retired-
model memory ratchet — and then it correctly BLOCKED two of the fixes for
those findings (count-based reclamation without a block gate; shared_ptr
atomics that take a mutex on the audio thread). Treat a BLOCK as a real
finding until refuted.
