# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Layout

This repo uses a single-context domain documentation layout:

- `CONTEXT.md` at the repo root for project domain language and glossary entries
- `docs/adr/` for architecture decision records

## Before exploring, read these

- `CONTEXT.md` at the repo root
- ADRs in `docs/adr/` that touch the area about to be changed

If any of these files do not exist, proceed silently. Do not flag their absence and do not suggest creating them upfront. Producer skills such as `/grill-with-docs` can create them lazily when terms or decisions actually get resolved.

## Use the glossary's vocabulary

When your output names a domain concept in an issue title, refactor proposal, hypothesis, or test name, use the term as defined in `CONTEXT.md`. Do not drift to synonyms the glossary explicitly avoids.

If the concept you need is not in the glossary yet, that is a signal: either you are inventing language the project does not use, or there is a real gap to note for `/grill-with-docs`.

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding.
