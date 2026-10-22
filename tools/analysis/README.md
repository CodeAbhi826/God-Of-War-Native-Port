# Analysis tools

Static analysis helpers that run on the *user's own* copy of the game. Nothing
here contains game data and nothing here is needed to build the Android APK.

## `elf_fn_match.py`

Matches function addresses between the USA (`SCUS_973.99`) and PAL
(`SCES_531.33`) builds of God of War.

The USA port carries ~6,400 hand-checked function addresses in
`port/overrides-usa/gow_overrides.cpp`; the PAL build links the same routines at
different addresses. Most compiled functions are byte-identical except for
absolute addresses (jump targets, branch offsets, `lui` immediates), so the
script normalises those fields and compares instruction fingerprints. No
decompiler, no emulator, no game data in the repository.

Requirements: none (falls back to a built-in ELF32 parser; `pyelftools` is used
when installed).

```bash
python3 tools/analysis/elf_fn_match.py \
    --usa SCUS_973.99 --pal SCES_531.33 \
    --usa-functions functions-usa.csv \
    --pal-functions port/functions.csv \
    --check 0x00183360,0x0027B7E8 \
    --out pal_map.csv
```

Inputs:

| file | source |
|---|---|
| `SCUS_973.99`, `SCES_531.33` | the user's own disc dumps |
| `functions-usa.csv` | Ghidra export kept in the private builder |
| `port/functions.csv` | PAL Ghidra export, published in this repository |

Output columns: `usa_addr,usa_name,pal_addr,method,score`.

* `exact` — identical normalised instruction sequence; the mapping is safe.
* `exact-dup` — identical to more than one PAL function (shared stubs); the
  nearest address is chosen, review before patching.
* `fuzzy` — matched by instruction-shape similarity (≥ 0.90); review.

The `--check` list prints the PAL address for specific USA addresses, which is
how the PAL override file is translated from the USA one.

## `pal_fn_map_colab.ipynb`

The same matcher as a Google Colab notebook: upload the two ELFs and the USA
function list, get `pal_map.csv` back. No local build, no local compute.
