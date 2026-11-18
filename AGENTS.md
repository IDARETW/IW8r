# Repository instructions

## Keep the GitHub wiki current

- Every commit to this repository must have a corresponding update in the
  [IW8r wiki](https://github.com/IDARETW/IW8r/wiki). Prepare the documentation with
  the change, then publish the wiki update when the source commit is available.
- Update the affected user-facing pages for changes to behavior, UI, setup,
  limitations, or workflows. For internal-only changes, record the impact and
  validation in the wiki's Development History instead of inventing a user-facing change.
- Link the source commit from Development History. Clearly label features that
  are only available on a branch or in an unmerged pull request.
- The wiki is a separate Git repository: `https://github.com/IDARETW/IW8r.wiki.git`.
  Fetch its current state before editing, preserve unrelated edits, and verify
  the pushed revision. Keep any local wiki checkout under ignored workspace output.
- Keep the in-repository user guide consistent with the wiki. Report an inability
  to publish the wiki explicitly; do not claim the documentation is synchronized.
