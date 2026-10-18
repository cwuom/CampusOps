"""Crops the weapon area (lower right) of full-size shots into a labelled grid.

usage: crop_sheet.py out.jpg cols name1 name2 ...   (names without .png, in Saved/AutoShots)
"""
import os
import sys
import cv2
import numpy as np

SRC = r"E:\game-project\Saved\AutoShots"
out, cols, names = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
tiles = []
for n in names:
    im = cv2.imread(os.path.join(SRC, n + ".png"))
    if im is None:
        continue
    h, w = im.shape[:2]
    c = im[int(h * 0.3):h, int(w * 0.35):int(w * 0.95)]
    c = cv2.resize(c, (640, int(640 * c.shape[0] / c.shape[1])), interpolation=cv2.INTER_AREA)
    cv2.putText(c, n, (6, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2, cv2.LINE_AA)
    tiles.append(c)
while len(tiles) % cols:
    tiles.append(np.zeros_like(tiles[0]))
rows = [np.hstack(tiles[i:i + cols]) for i in range(0, len(tiles), cols)]
cv2.imwrite(os.path.join(SRC, "small", out), np.vstack(rows), [cv2.IMWRITE_JPEG_QUALITY, 86])
print(len(names), "->", out)
