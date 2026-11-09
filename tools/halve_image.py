from __future__ import annotations

from collections import Counter

import numpy as np

HALF_METHODS_LIST = ["topleft", "topleft2", "stagger", "dither", "salient", "rare"]

def halve_topleft(image: list[list[int]]) -> list[list[int]]:
    new_image = []
    y_size = len(image)
    x_size = len(image[0])
    for y in range(0, y_size, 2):
        upper_row = []
        for x in range(0, x_size, 2):
            upper_row.append(image[y  ][x  ])
        new_image.append(upper_row)
    return new_image

def halve_topleft2(image: list[list[int]]) -> list[list[int]]:
    new_image = []
    y_size = len(image)
    x_size = len(image[0])
    for y in range(0, y_size, 4):
        upper_row = []
        lower_row = []
        for x in range(0, x_size, 4):
            upper_row.append(image[y  ][x  ])
            upper_row.append(image[y  ][x+1])
            lower_row.append(image[y+1][x  ])
            lower_row.append(image[y+1][x+1])
        new_image.append(upper_row)
        new_image.append(lower_row)
    return new_image

def halve_stagger(image: list[list[int]]) -> list[list[int]]:
    new_image = []
    y_size = len(image)
    x_size = len(image[0])
    for y in range(0, y_size, 4):
        upper_row = []
        lower_row = []
        for x in range(0, x_size, 4):
            upper_row.append(image[y  ][x  ])
            upper_row.append(image[y+1][x+2])
            lower_row.append(image[y+2][x+1])
            lower_row.append(image[y+3][x+3])
        new_image.append(upper_row)
        new_image.append(lower_row)
    return new_image

def halve_dither(image: list[list[int]]) -> list[list[int]]:
    y_size = len(image)
    x_size = len(image[0])
    dither_template = np.zeros((y_size // 2, x_size // 2, 16))
    for y in range(0, y_size):
        for x in range(0, x_size):
            dither_template[y // 2, x // 2, image[y][x]] += 0.25
    new_image = []
    for y in range(0, y_size // 2):
        new_row = []
        for x in range(0, x_size // 2):
            chosen_index = np.argmax(dither_template[y,x])
            new_row.append(chosen_index)
            diff = np.identity(16)[chosen_index] - dither_template[y,x]
            if x + 1 < x_size / 2:
                dither_template[y,x+1] -= diff * (7/16)
            if y + 1 < y_size / 2:
                dither_template[y+1,x] -= diff * (5/16)
                if x + 1 < x_size / 2:
                    dither_template[y+1,x+1] -= diff * (1/16)
                if x > 0:
                    dither_template[y+1,x-1] -= diff * (3/16)
        new_image.append(new_row)
    return new_image

def halve_salient(image: list[list[int]], palette: list[int]) -> list[list[int]]:
    """
    Majority-color downscale that keeps thin high-contrast details alive:
    whenever a 2x2 block contains a color that stands out against the
    block's majority color (a 1px line, a rim light, a sprite edge), the
    most frequent such "salient" color wins instead of the majority.
    Plain fills still come out smooth like halve_dither.

    `palette` holds one packed color per index (any RGB layout works;
    channels are extracted per layout below). Needs the palette to judge
    contrast -- index distance alone is meaningless.
    """
    y_size = len(image)
    x_size = len(image[0])

    def channels(i: int) -> tuple[int, int, int]:
        c = palette[i]
        return ((c >> 10) & 31, (c >> 5) & 31, c & 31)  # b, g, r

    def dist(a: int, b: int) -> int:
        ba, ga, ra = channels(a)
        bb, gb, rb = channels(b)
        return abs(ra - rb) + abs(ga - gb) + abs(ba - bb)

    # Manhattan distance on 5-bit channels; 0..93. A gray line on black
    # is ~72, subtle shading stays below and keeps the majority result.
    SALIENT_THRESHOLD = 24

    new_image = []
    for y in range(0, y_size, 2):
        new_row = []
        for x in range(0, x_size, 2):
            block = [
                image[y    ][x    ],
                image[y    ][x + 1],
                image[y + 1][x    ],
                image[y + 1][x + 1],
            ]
            counts = Counter(block)
            majority, _ = counts.most_common(1)[0]
            best = majority
            best_score = None
            for color, count in counts.items():
                if color == majority:
                    continue
                d = dist(color, majority)
                if d >= SALIENT_THRESHOLD:
                    score = (count, d)  # most frequent salient, then contrast
                    if best_score is None or score > best_score:
                        best = color
                        best_score = score
            new_row.append(best)
        new_image.append(new_row)
    return new_image

def halve_rare(image: list[list[int]]) -> list[list[int]]:
    """
    For each 2x2 block, pick the block color that is rarest in the image
    as a whole. Fill colors are globally frequent; 1px lines, outlines
    and rims are globally rare, so thin detail wins its blocks and
    survives the downscale while solid areas stay solid.

    Ties go to the color used fewest times within the block (the
    odd-one-out), then to the lower index, so the result is stable.
    """
    global_count = Counter(c for row in image for c in row)

    new_image = []
    for y in range(0, len(image), 2):
        new_row = []
        for x in range(0, len(image[0]), 2):
            block = Counter((
                image[y    ][x    ],
                image[y    ][x + 1],
                image[y + 1][x    ],
                image[y + 1][x + 1],
            ))
            best = min(block, key=lambda c: (global_count[c], block[c], c))
            new_row.append(best)
        new_image.append(new_row)
    return new_image

def shrink_image(image: list[list[int]], method: str, palette: list[int] | None = None) -> list[list[int]]:
    if not method:
        return image
    if method == "topleft":
        return halve_topleft(image)
    if method == "topleft2":
        return halve_topleft2(image)
    if method == "stagger":
        return halve_stagger(image)
    if method == "dither":
        return halve_dither(image)
    if method == "salient":
        if palette is None:
            raise ValueError("the 'salient' method needs a palette")
        return halve_salient(image, palette)
    if method == "rare":
        return halve_rare(image)
    raise ValueError(f"invalid halving method {method}")