#!/usr/bin/env python3
"""Match PS2 functions between two regional builds of the same game.

God of War's USA port has ~6400 hand-checked function addresses in
port/overrides-usa/gow_overrides.cpp; the PAL build has different addresses.
Most compiled functions are identical except for absolute addresses, so a
normalised instruction fingerprint maps USA addresses to PAL addresses without
decompiling anything.

Typical use (Google Colab, after recompiling the PAL ELF with ps2_recomp):

    python elf_fn_match.py \
        --usa SCUS_973.99 --pal SCES_531.33 \
        --usa-functions functions-usa.csv \
        --pal-functions port/output/register_functions.cpp \
        --check 0x00279600,0x0028C0C8,0x0014C278 \
        --out pal_map.csv

Only the two ELFs and the two function lists are needed; nothing is uploaded.
Requires: pyelftools (pip install pyelftools).
"""

from __future__ import annotations

import argparse
import csv
import re
import struct
import sys
from dataclasses import dataclass
from typing import Iterable

try:
    from elftools.elf.elffile import ELFFile
except ImportError:  # optional: a minimal parser below keeps the script dependency free
    ELFFile = None  # type: ignore[assignment]


# ---------------------------------------------------------------------------
# ELF text


@dataclass
class Text:
    vaddr: int
    data: bytes

    def words(self, addr: int, size: int) -> list[int]:
        off = addr - self.vaddr
        if off < 0 or off + size > len(self.data):
            return []
        count = size // 4
        return list(struct.unpack_from(f"<{count}I", self.data, off))


def _load_text_minimal(path: str) -> Text:
    """ELF32 little-endian executable segment, no external dependency."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise SystemExit(f"{path}: expected ELF32 little-endian")
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    best: tuple[int, int, int] | None = None  # filesz, offset, vaddr
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if off + 0x20 > len(data):
            break
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz = struct.unpack_from("<IIIII", data, off)
        p_flags = struct.unpack_from("<I", data, off + 24)[0]
        if p_type != 1 or not (p_flags & 1) or p_filesz == 0:
            continue
        if best is None or p_filesz > best[0]:
            best = (p_filesz, p_offset, p_vaddr)
    if best is None:
        raise SystemExit(f"{path}: no executable PT_LOAD segment")
    filesz, offset, vaddr = best
    return Text(vaddr, data[offset:offset + filesz])


def load_text(path: str) -> Text:
    if ELFFile is None:
        return _load_text_minimal(path)
    with open(path, "rb") as fh:
        elf = ELFFile(fh)
        section = elf.get_section_by_name(".text")
        if section is None:
            for candidate in elf.iter_sections():
                if candidate["sh_flags"] & 0x4 and candidate["sh_size"]:
                    section = candidate
                    break
        if section is None:
            return _load_text_minimal(path)
        return Text(section["sh_addr"], section.data())


# ---------------------------------------------------------------------------
# Instruction normalisation
#
# Absolute addresses live in J/JAL targets, branch offsets, LUI immediates and
# load/store offsets.  Mask those fields: two copies of the same routine then
# produce the same fingerprint even when linked at different addresses.

_MASK_IMM_ALU = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}
_MASK_MEM = set(range(0x20, 0x40))


def norm_word(w: int) -> tuple[int, ...]:
    op = w >> 26
    rs = (w >> 21) & 0x1F
    rt = (w >> 16) & 0x1F
    if op == 0:  # SPECIAL
        funct = w & 0x3F
        if funct in (0x08, 0x09):  # JR / JALR
            return (0, rs, funct)
        if funct == 0x0D:  # BREAK
            return (0, 0, funct)
        rd = (w >> 11) & 0x1F
        sa = (w >> 6) & 0x1F
        return (0, rs, rt, rd, sa, funct)
    if op in (0x02, 0x03):  # J / JAL
        return (op,)
    if op == 0x01 or 0x04 <= op <= 0x07 or 0x14 <= op <= 0x17:  # branches
        return (op, rs, rt)
    if op == 0x0F:  # LUI
        return (op, rt)
    if op in _MASK_IMM_ALU or op in _MASK_MEM:  # imm ALU / loads / stores
        return (op, rs, rt)
    # COP2 (VU macro mode), MMI and anything else: keep the full word.  These
    # carry register/immediate payloads but no absolute addresses.
    return (w,)


def norm_seq(words: Iterable[int]) -> tuple[int, ...]:
    return tuple(norm_word(w) for w in words)


def trim_nops(seq: tuple[int, ...]) -> tuple[int, ...]:
    end = len(seq)
    while end and seq[end - 1] == (0, 0, 0, 0, 0, 0):
        end -= 1
    return seq[:end]


# ---------------------------------------------------------------------------
# Function lists


@dataclass
class Func:
    addr: int
    end: int
    name: str = ""


def load_ghidra_csv(path: str) -> list[Func]:
    """Name,Start,End,Size - the format exported from Ghidra (case insensitive)."""
    out: list[Func] = []
    with open(path, newline="") as fh:
        reader = csv.DictReader(fh)
        fields = {name.lower(): name for name in (reader.fieldnames or [])}
        start_key = fields.get("start")
        end_key = fields.get("end")
        name_key = fields.get("name")
        if not start_key or not end_key:
            raise SystemExit(f"{path}: expected Start/End columns")
        for row in reader:
            try:
                start = int(row[start_key], 0)
                end = int(row[end_key], 0)
            except (KeyError, ValueError):
                continue
            if end <= start:
                continue
            out.append(Func(start, end, row.get(name_key, "") if name_key else ""))
    out.sort(key=lambda f: f.addr)
    return out


_RE_ENTRY = re.compile(r"sub_([0-9A-Fa-f]+)_0x([0-9A-Fa-f]+)")
_RE_ENTRY_ALT = re.compile(r"0x([0-9A-Fa-f]{6,8})\s*,\s*sub_")


def load_register_functions(path: str) -> list[Func]:
    """Entries of ps2_recomp's generated register_functions.cpp.

    Sizes are unknown there, so each function runs to the next entry (capped at
    2 KB) and trailing NOPs are trimmed before fingerprinting.
    """
    starts: list[tuple[int, str]] = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m = _RE_ENTRY.search(line)
            if m:
                starts.append((int(m.group(1), 16), m.group(0)))
                continue
            m = _RE_ENTRY_ALT.search(line)
            if m:
                starts.append((int(m.group(1), 16), ""))
    seen: set[int] = set()
    uniq = []
    for addr, name in sorted(starts):
        if addr not in seen:
            seen.add(addr)
            uniq.append((addr, name))
    out = []
    for i, (addr, name) in enumerate(uniq):
        end = min(addr + 0x1000, uniq[i + 1][0]) if i + 1 < len(uniq) else addr + 0x1000
        out.append(Func(addr, end, name))
    return out


def load_pal_functions(path: str) -> list[Func]:
    """Accept a Ghidra CSV or a ps2_recomp register_functions.cpp."""
    with open(path, "r", errors="replace") as fh:
        first = fh.readline().strip().lower()
    if first.startswith("name,") or "start" in first and "end" in first:
        return load_ghidra_csv(path)
    return load_register_functions(path)


# ---------------------------------------------------------------------------
# Matching


def fingerprint(text: Text, func: Func) -> tuple[int, ...]:
    words = text.words(func.addr, func.end - func.addr)
    return trim_nops(norm_seq(words))


def similarity(a: tuple[int, ...], b: tuple[int, ...]) -> float:
    if not a or not b:
        return 0.0
    n = min(len(a), len(b))
    same = sum(1 for i in range(n) if a[i] == b[i])
    return same / max(len(a), len(b))


def match(usa_text: Text, usa_funcs: list[Func], pal_text: Text, pal_funcs: list[Func]):
    pal_fp = [(f, fingerprint(pal_text, f)) for f in pal_funcs]
    exact: dict[tuple[int, ...], list[Func]] = {}
    for f, fp in pal_fp:
        if len(fp) >= 3:
            exact.setdefault(fp, []).append(f)

    # Prefix index for routines that changed slightly.
    prefix: dict[tuple[int, ...], list[Func]] = {}
    for f, fp in pal_fp:
        if len(fp) >= 12:
            prefix.setdefault(fp[:12], []).append(f)

    results = []
    for uf in usa_funcs:
        uf_fp = fingerprint(usa_text, uf)
        if len(uf_fp) < 2:
            continue
        cands = exact.get(uf_fp)
        if cands and len(cands) == 1:
            results.append((uf, cands[0], "exact", 1.0))
            continue
        if cands:
            results.append((uf, min(cands, key=lambda f: abs(f.addr - uf.addr)), "exact-dup", 1.0))
            continue
        cands = prefix.get(uf_fp[:12], [])
        best = None
        best_score = 0.0
        for pf in cands:
            score = similarity(uf_fp, fingerprint(pal_text, pf))
            if score > best_score:
                best, best_score = pf, score
        if best is not None and best_score >= 0.90:
            results.append((uf, best, "fuzzy", best_score))
    return results


def parse_addr_list(value: str) -> list[int]:
    out = []
    for item in value.replace(";", ",").split(","):
        item = item.strip()
        if item:
            out.append(int(item, 0))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--usa", required=True, help="USA ELF (SCUS_973.99)")
    ap.add_argument("--pal", required=True, help="PAL ELF (SCES_531.33)")
    ap.add_argument("--usa-functions", required=True, help="Ghidra CSV: Name,Start,End,Size")
    ap.add_argument("--pal-functions", required=True, help="ps2_recomp register_functions.cpp")
    ap.add_argument("--out", default="pal_map.csv")
    ap.add_argument("--check", default="", help="comma separated USA addresses to print")
    ap.add_argument("--min-score", type=float, default=0.90)
    args = ap.parse_args()

    usa_text = load_text(args.usa)
    pal_text = load_text(args.pal)
    usa_funcs = load_ghidra_csv(args.usa_functions)
    pal_funcs = load_pal_functions(args.pal_functions)
    if not usa_funcs or not pal_funcs:
        raise SystemExit("empty function list; check the input files")

    results = match(usa_text, usa_funcs, pal_text, pal_funcs)
    by_addr = {}
    for uf, pf, method, score in results:
        if score >= args.min_score:
            by_addr[uf.addr] = (pf, method, score)

    with open(args.out, "w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["usa_addr", "usa_name", "pal_addr", "method", "score", "usa_size", "pal_size"])
        for uf, pf, method, score in results:
            if score < args.min_score:
                continue
            writer.writerow([f"0x{uf.addr:08x}", uf.name, f"0x{pf.addr:08x}", method, f"{score:.3f}",
                             uf.end - uf.addr, pf.end - pf.addr])

    total = len(usa_funcs)
    matched = len(by_addr)
    print(f"USA functions: {total}")
    print(f"PAL functions: {len(pal_funcs)}")
    print(f"matched:       {matched} ({100.0 * matched / total:.1f}%)")
    print(f"written:       {args.out}")

    for addr in parse_addr_list(args.check):
        hit = by_addr.get(addr)
        if hit:
            pf, method, score = hit
            print(f"  0x{addr:08x} -> 0x{pf.addr:08x}  ({method}, {score:.3f})")
        else:
            print(f"  0x{addr:08x} -> no match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
