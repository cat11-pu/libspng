#!/usr/bin/env python3
"""Deterministic PNG corpus generator for the libspng decoder audit.

The corpus is split into:
  valid/      500 spec-compliant images
  malformed/  3000 images, 600 in each category:
              truncated, length_mismatch, oversized_dimension,
              illegal_filter, cyclic_chunk

Every malformed image is associated with the chunk type and file offset
that was changed.  The decoder harness records the chunk type and offset
where decoding stopped.
"""

from __future__ import annotations

import argparse
import binascii
import os
import random
import struct
import zlib

PNG_SIG = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
COMBOS = [
    (0, 1), (0, 2), (0, 4), (0, 8), (0, 16),
    (2, 8), (2, 16),
    (3, 1), (3, 2), (3, 4), (3, 8),
    (4, 8), (4, 16),
    (6, 8), (6, 16),
]
ADAM7 = [
    (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8),
    (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2),
    (0, 1, 1, 2),
]

N_VALID = 500
N_MALFORMED_PER_CATEGORY = 600


def chunk(name: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + name + data +
            struct.pack(">I", binascii.crc32(name + data) & 0xFFFFFFFF))


def parse_chunks(data: bytes) -> list[dict]:
    chunks = []
    if not data.startswith(PNG_SIG):
        return chunks
    pos = len(PNG_SIG)
    while pos + 8 <= len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        name = data[pos + 4:pos + 8]
        payload = pos + 8
        end = payload + length + 4
        item = {
            "type": name.decode("latin1"),
            "offset": pos,
            "length": length,
            "payload": payload,
        }
        if end > len(data):
            item["truncated"] = True
        chunks.append(item)
        if end > len(data):
            break
        pos = end
    return chunks


def chunk_at(data: bytes, offset: int) -> tuple[str, int]:
    if offset < 8:
        return "SIG", 0
    for item in parse_chunks(data):
        if item["offset"] <= offset < item["offset"] + 12 + item["length"]:
            return item["type"], item["offset"]
    return "EOF", min(offset, len(data))


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def filter_row(raw: bytes, prev: bytes, bpp: int, ftype: int) -> bytes:
    out = bytearray(len(raw))
    for i, value in enumerate(raw):
        left = raw[i - bpp] if i >= bpp else 0
        up = prev[i] if prev else 0
        up_left = prev[i - bpp] if prev and i >= bpp else 0
        if ftype == 0:
            predict = 0
        elif ftype == 1:
            predict = left
        elif ftype == 2:
            predict = up
        elif ftype == 3:
            predict = (left + up) // 2
        else:
            predict = paeth(left, up, up_left)
        out[i] = (value - predict) & 0xFF
    return bytes(out)


def pack_row(samples: list[int], bit_depth: int) -> bytes:
    if bit_depth == 8:
        return bytes(samples)
    if bit_depth == 16:
        out = bytearray()
        for value in samples:
            out += struct.pack(">H", value & 0xFFFF)
        return bytes(out)

    out = bytearray()
    acc = 0
    used = 0
    mask = (1 << bit_depth) - 1
    for value in samples:
        acc = (acc << bit_depth) | (value & mask)
        used += bit_depth
        while used >= 8:
            used -= 8
            out.append((acc >> used) & 0xFF)
            acc &= (1 << used) - 1
    if used:
        out.append((acc << (8 - used)) & 0xFF)
    return bytes(out)


def make_samples(width: int, height: int, color_type: int, bit_depth: int,
                 palette_entries: int, seed: int) -> list[list[list[int]]]:
    channels = CHANNELS[color_type]
    max_value = (1 << bit_depth) - 1
    image = []
    for y in range(height):
        row = []
        for x in range(width):
            if color_type == 0:
                pixel = [(x * 13 + y * 7 + seed) & max_value]
            elif color_type == 2:
                pixel = [(x * 5 + seed) & max_value,
                         (y * 3 + seed * 2) & max_value,
                         ((x ^ y) * 7 + seed) & max_value]
            elif color_type == 3:
                pixel = [(x * 3 + y * 5 + seed) % palette_entries]
            elif color_type == 4:
                pixel = [(x * 11 + y * 3 + seed) & max_value,
                         (x * 7 + y * 17 + seed * 3) & max_value]
            else:
                pixel = [(x * 5 + y + seed) & max_value,
                         (y * 9 + x * 2 + seed) & max_value,
                         ((x + y) * 7 + seed) & max_value,
                         (x * 17 + y * 13 + seed * 5) & max_value]
            assert len(pixel) == channels
            row.append(pixel)
        image.append(row)
    return image


def filtered_stream(width: int, height: int, color_type: int, bit_depth: int,
                    interlace: int, image: list[list[list[int]]],
                    seed: int, force_filter: int | None) -> bytes:
    channels = CHANNELS[color_type]
    bits_per_pixel = channels * bit_depth
    bpp = max(1, (bits_per_pixel + 7) // 8)
    out = bytearray()

    passes = [(0, 0, 1, 1)] if not interlace else ADAM7
    pass_index = 0
    for x0, y0, dx, dy in passes:
        pw = 0 if width <= x0 else (width - x0 + dx - 1) // dx
        ph = 0 if height <= y0 else (height - y0 + dy - 1) // dy
        if pw == 0 or ph == 0:
            continue

        prev = b""
        for py in range(ph):
            y = y0 + py * dy
            samples = []
            for px in range(pw):
                x = x0 + px * dx
                samples.extend(image[y][x])
            raw = pack_row(samples, bit_depth)

            if force_filter is None:
                ftype = (seed + pass_index + py) % 5
            else:
                ftype = force_filter

            if force_filter is None:
                row = filter_row(raw, prev, bpp, ftype)
            else:
                row = raw
            out.append(ftype)
            out += row
            prev = raw
        pass_index += 1
    return bytes(out)


def build_png(width: int, height: int, color_type: int, bit_depth: int,
              interlace: int, seed: int, *, force_filter: int | None = None,
              force_filter_method: int = 0, trns: bool = False,
              ancillary: bool = True, idat_parts: int = 1,
              force_raw_stream: bytes | None = None) -> bytes:
    if color_type == 3:
        palette_entries = 1 << min(bit_depth, 5)
    else:
        palette_entries = 0

    image = make_samples(width, height, color_type, bit_depth,
                         palette_entries, seed)
    if force_raw_stream is None:
        raw = filtered_stream(width, height, color_type, bit_depth, interlace,
                              image, seed, force_filter)
    else:
        raw = force_raw_stream

    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type,
                       0, force_filter_method, interlace)
    pieces = [chunk(b"IHDR", ihdr)]

    if ancillary and seed % 4 == 0:
        pieces.append(chunk(b"gAMA", struct.pack(">I", 45455)))
    if ancillary and seed % 5 == 0:
        pieces.append(chunk(b"pHYs", struct.pack(">IIB", 2835, 2835, 1)))
    if ancillary and seed % 7 == 0:
        pieces.append(chunk(b"tEXt", b"Comment\x00generated audit image"))

    if color_type == 3:
        entries = bytearray()
        for i in range(palette_entries):
            entries += bytes(((i * 37 + seed) & 0xFF,
                              (i * 73 + seed * 3) & 0xFF,
                              (i * 109 + seed * 7) & 0xFF))
        pieces.append(chunk(b"PLTE", bytes(entries)))

        if trns:
            n_alpha = 1 + (seed % palette_entries)
            pieces.append(chunk(b"tRNS", bytes(
                (i * 53 + seed) & 0xFF for i in range(n_alpha))))
    elif trns and color_type == 0:
        max_value = (1 << bit_depth) - 1
        pieces.append(chunk(b"tRNS", struct.pack(">H", seed & max_value)))
    elif trns and color_type == 2:
        max_value = (1 << bit_depth) - 1
        pieces.append(chunk(b"tRNS", struct.pack(
            ">HHH", seed & max_value, (seed * 3) & max_value,
            (seed * 7) & max_value)))

    compressed = zlib.compress(raw, 9)
    parts = max(1, min(idat_parts, len(compressed)))
    cuts = [len(compressed) * i // parts for i in range(parts + 1)]
    for i in range(parts):
        pieces.append(chunk(b"IDAT", compressed[cuts[i]:cuts[i + 1]]))

    pieces.append(chunk(b"IEND", b""))
    return PNG_SIG + b"".join(pieces)


def valid_parameters(index: int) -> dict:
    color_type, bit_depth = COMBOS[index % len(COMBOS)]
    width = 1 + (index * 7 + 3) % 17
    height = 1 + (index * 5 + 1) % 13
    interlace = 1 if index % 2 else 0
    return {
        "width": width,
        "height": height,
        "color_type": color_type,
        "bit_depth": bit_depth,
        "interlace": interlace,
        "trns": (index % 3 == 0 and color_type in (0, 2, 3)),
        "ancillary": True,
        "idat_parts": 1 + index % 3,
    }


def save(path: str, data: bytes) -> None:
    with open(path, "wb") as f:
        f.write(data)


def mutate_ihdr(data: bytes, width: int | None = None,
                height: int | None = None,
                filter_method: int | None = None) -> bytes:
    chunks = parse_chunks(data)
    if not chunks or chunks[0]["type"] != "IHDR":
        raise ValueError("missing IHDR")
    ihdr = bytearray(data[chunks[0]["payload"]:chunks[0]["payload"] + 13])
    if width is not None:
        ihdr[0:4] = struct.pack(">I", width & 0xFFFFFFFF)
    if height is not None:
        ihdr[4:8] = struct.pack(">I", height & 0xFFFFFFFF)
    if filter_method is not None:
        ihdr[11] = filter_method & 0xFF
    rebuilt = (data[:chunks[0]["payload"]] + bytes(ihdr) +
               data[chunks[0]["payload"] + 13:])
    # The original CRC follows the fixed-size IHDR payload.
    crc_off = chunks[0]["payload"] + 13
    rebuilt = (rebuilt[:crc_off] +
               struct.pack(">I", binascii.crc32(b"IHDR" + bytes(ihdr)) & 0xFFFFFFFF) +
               rebuilt[crc_off + 4:])
    return rebuilt


def replace_chunk(data: bytes, index: int, name: bytes, payload: bytes) -> bytes:
    chunks = parse_chunks(data)
    target = chunks[index]
    return (data[:target["offset"]] + chunk(name, payload) +
            data[target["payload"] + target["length"] + 4:])


def insert_chunk(data: bytes, index: int, name: bytes, payload: bytes) -> bytes:
    chunks = parse_chunks(data)
    if index >= len(chunks):
        pos = len(data)
    else:
        pos = chunks[index]["offset"]
    return data[:pos] + chunk(name, payload) + data[pos:]


def malformed_truncated(seed_data: bytes, k: int, rng: random.Random) -> tuple[bytes, str, int, str]:
    chunks = parse_chunks(seed_data)
    length = len(seed_data)
    iend = next((c for c in chunks if c["type"] == "IEND"), None)
    max_cut = (iend["offset"] - 1) if iend is not None else length - 1
    if k < 6:
        cut = [0, 1, 4, 6, 7, 2][k]
    elif k < 130:
        cut = 8 + (k * 2) % 33
    elif k < 280:
        cut = chunks[(k * 11) % len(chunks)]["offset"]
    elif k < 430:
        item = chunks[(k * 7) % len(chunks)]
        cut = min(length - 1, item["payload"] + (k * 13) % max(1, item["length"]))
    elif k < 540:
        cut = max(0, length - 1 - (k % 17))
    else:
        cut = rng.randrange(0, max(1, length - 1))
    cut = max(0, min(max_cut, length - 1, cut))
    name, offset = chunk_at(seed_data, cut)
    detail = f"cut={cut}"
    return seed_data[:cut], name, offset, detail


def malformed_length(seed_data: bytes, k: int, rng: random.Random) -> tuple[bytes, str, int, str]:
    chunks = parse_chunks(seed_data)
    candidates = [i for i, c in enumerate(chunks)
                  if c["type"] in ("IHDR", "PLTE", "tRNS", "gAMA", "pHYs",
                                   "tEXt", "IDAT", "IEND")]
    index = candidates[k % len(candidates)]
    target = chunks[index]
    original = target["length"]
    if target["type"] == "IHDR":
        new_length = [0, 12, 14, 16, 8, 0xFFFFFFFF, 0x7FFFFFFF][k % 7]
    else:
        new_length = [0, max(0, original - 1), original + 1, original + 4,
                      original + 8, original * 2, 1, 2, 0x7FFFFFFF,
                      0xFFFFFFFF][k % 10]
    if new_length == original:
        new_length = original + 1
    # Leave the payload and CRC untouched: the declared length no longer
    # agrees with the chunk boundary, which is the condition under test.
    mutated = bytearray(seed_data)
    struct.pack_into(">I", mutated, target["offset"], new_length)
    detail = f"orig={original},new={new_length},boundary-mismatch"
    return bytes(mutated), target["type"], target["offset"], detail


def malformed_oversized(seed_data: bytes, k: int, rng: random.Random) -> tuple[bytes, str, int, str]:
    values = [16001, 16384, 65535, 100000, 1000000, 0x40000000,
              0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF]
    value = values[k % len(values)]
    axis = "width" if (k // len(values)) % 2 == 0 else "height"
    if axis == "width":
        mutated = mutate_ihdr(seed_data, width=value)
    else:
        mutated = mutate_ihdr(seed_data, height=value)
    return mutated, "IHDR", 8, f"{axis}={value}"


def malformed_filter(seed_data: bytes, k: int, rng: random.Random) -> tuple[bytes, str, int, str]:
    if k < 480:
        ftype = 5 + (k % 251)
        params = valid_parameters(k)
        mutated = build_png(params["width"], params["height"],
                            params["color_type"], params["bit_depth"],
                            params["interlace"], k + 1000,
                            force_filter=ftype,
                            trns=params["trns"],
                            idat_parts=1)
        chunks = [c for c in parse_chunks(mutated) if c["type"] == "IDAT"]
        return mutated, "IDAT", chunks[0]["offset"], f"filter_type={ftype}"

    method = 1 + (k - 480)
    mutated = mutate_ihdr(seed_data, filter_method=method)
    return mutated, "IHDR", 8, f"filter_method={method}"


def malformed_cyclic(seed_data: bytes, k: int, rng: random.Random) -> tuple[bytes, str, int, str]:
    chunks = parse_chunks(seed_data)
    family = k % 5
    variant = k // 5

    if family == 0:
        # Duplicate PLTE.  Indexed images are required for a first PLTE.
        candidates = [i for i, c in enumerate(chunks) if c["type"] == "PLTE"]
        if not candidates:
            params = valid_parameters(k)
            while params["color_type"] != 3:
                k += 1
                params = valid_parameters(k)
            seed_data = build_png(params["width"], params["height"],
                                  params["color_type"], params["bit_depth"],
                                  params["interlace"], k + 2000,
                                  trns=params["trns"])
            chunks = parse_chunks(seed_data)
            candidates = [i for i, c in enumerate(chunks) if c["type"] == "PLTE"]
        index = candidates[variant % len(candidates)]
        target = chunks[index]
        payload = seed_data[target["payload"]:
                            target["payload"] + target["length"]]
        mutated = insert_chunk(seed_data, index + 1, b"PLTE", payload)
        names = [c for c in parse_chunks(mutated)]
        inserted = names[index + 1]
        return mutated, "PLTE", inserted["offset"], "duplicate PLTE"

    if family == 1:
        names_available = [c["type"] for c in chunks
                           if c["type"] in ("gAMA", "pHYs", "tEXt", "tRNS")]
        if not names_available:
            params = dict(valid_parameters(k))
            params.update(color_type=3, bit_depth=8, trns=True,
                          idat_parts=1, interlace=0)
            seed_data = build_png(params["width"], params["height"],
                                  params["color_type"], params["bit_depth"],
                                  params["interlace"], k + 3000, trns=True,
                                  idat_parts=params["idat_parts"])
            chunks = parse_chunks(seed_data)
            names_available = ["tRNS"]
        name = names_available[variant % len(names_available)]
        index = next(i for i, c in enumerate(chunks) if c["type"] == name)
        target = chunks[index]
        payload = seed_data[target["payload"]:
                            target["payload"] + target["length"]]
        mutated = insert_chunk(seed_data, index + 1,
                               name.encode("latin1"), payload)
        inserted = parse_chunks(mutated)[index + 1]
        return mutated, name, inserted["offset"], f"duplicate {name}"

    if family == 2:
        candidates = [i for i, c in enumerate(chunks)
                      if c["type"] == "IDAT" and c["length"] > 0]
        if len(candidates) < 2:
            params = valid_parameters(k)
            seed_data = build_png(params["width"], params["height"],
                                  params["color_type"], params["bit_depth"],
                                  params["interlace"], k + 4000,
                                  trns=params["trns"], idat_parts=3)
            chunks = parse_chunks(seed_data)
            candidates = [i for i, c in enumerate(chunks)
                          if c["type"] == "IDAT" and c["length"] > 0]
        index = candidates[variant % (len(candidates) - 1)]
        target = chunks[index]
        payload = seed_data[target["payload"]:
                            target["payload"] + target["length"]]
        mutated = insert_chunk(seed_data, index + 1, b"IDAT", payload)
        inserted = parse_chunks(mutated)[index + 1]
        return mutated, "IDAT", inserted["offset"], "duplicate IDAT payload"

    if family == 3:
        candidates = [i for i, c in enumerate(chunks) if c["type"] == "IDAT"]
        if len(candidates) < 2:
            params = valid_parameters(k)
            seed_data = build_png(params["width"], params["height"],
                                  params["color_type"], params["bit_depth"],
                                  params["interlace"], k + 5000,
                                  trns=params["trns"], idat_parts=3)
            chunks = parse_chunks(seed_data)
            candidates = [i for i, c in enumerate(chunks)
                          if c["type"] == "IDAT"]
        index = candidates[variant % (len(candidates) - 1)]
        mutated = insert_chunk(seed_data, index + 1, b"tEXt",
                               b"Audit\x00interrupt")
        inserted = parse_chunks(mutated)[index + 1]
        return mutated, "tEXt", inserted["offset"], "ancillary chunk interrupts IDAT"

    count = 1 + (variant % 100)
    for _ in range(count):
        seed_data = insert_chunk(seed_data, 2, b"juNK", b"")
    inserted = parse_chunks(seed_data)[2]
    return seed_data, "juNK", inserted["offset"], f"zero-length unknown chunk repeated={count}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    valid_dir = os.path.join(args.out, "valid")
    malformed_dir = os.path.join(args.out, "malformed")
    os.makedirs(valid_dir, exist_ok=True)
    os.makedirs(malformed_dir, exist_ok=True)

    manifest = []
    valid_data = []
    for i in range(N_VALID):
        params = valid_parameters(i)
        data = build_png(**params, seed=i)
        name = f"v{i:04d}.png"
        save(os.path.join(valid_dir, name), data)
        detail = ",".join(f"{key}={value}" for key, value in params.items())
        # Use the requested ordering and coverage in a stable, readable form.
        detail = (f"ct={params['color_type']},bd={params['bit_depth']},"
                  f"il={params['interlace']},w={params['width']},"
                  f"h={params['height']},trns={int(params['trns'])},"
                  f"idat={params['idat_parts']}")
        manifest.append(("valid", "valid", "-", "0", f"valid/{name}", detail))
        valid_data.append(data)

    rng = random.Random(0x3592)
    categories = [
        ("truncated", malformed_truncated),
        ("length_mismatch", malformed_length),
        ("oversized_dimension", malformed_oversized),
        ("illegal_filter", malformed_filter),
        ("cyclic_chunk", malformed_cyclic),
    ]

    for category, builder in categories:
        for k in range(N_MALFORMED_PER_CATEGORY):
            seed_data = valid_data[(k * 17 + 11) % len(valid_data)]
            data, name, offset, detail = builder(seed_data, k, rng)
            filename = f"{k:04d}_{category}.png"
            save(os.path.join(malformed_dir, filename), data)
            manifest.append(("malformed", category, name, str(offset),
                             f"malformed/{filename}", detail))

    with open(os.path.join(args.out, "manifest.tsv"), "w", encoding="ascii") as f:
        for row in manifest:
            f.write("\t".join(row) + "\n")

    print(f"generated {N_VALID} valid and "
          f"{N_MALFORMED_PER_CATEGORY * len(categories)} malformed images")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
