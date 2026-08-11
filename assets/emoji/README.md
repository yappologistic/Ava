# Unicode data used by Ava

Ava's Emoji & Symbols picker vendors the following authoritative data files:

- `emoji-test.txt`: Unicode Emoji 17.0 keyboard/display test data, retrieved
  from `https://www.unicode.org/Public/UCD/latest/emoji/emoji-test.txt`.
- `annotations-en.json`: English CLDR annotations, retrieved from the
  `unicode-org/cldr-json` repository's `main` branch.
- `annotations-derived-en.json`: derived English CLDR annotations (including
  skin-tone sequences), retrieved from the same repository.

The files were retrieved on 2026-08-10 and are covered by `LICENSE` (Unicode
License v3). Update all three data files together so names, keywords, and
emoji sequences stay compatible.
