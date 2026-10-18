# Contact sheet of captured frames: python sheet.py out.jpg width cols name1 name2 ... (names without .png; globs ok)
import cv2, glob, os, sys
import numpy as np

SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "Saved", "AutoShots"))


def tile(path, w):
    im = cv2.imread(path)
    h, W = im.shape[:2]
    im = cv2.resize(im, (w, int(h * w / W)), interpolation=cv2.INTER_AREA)
    cv2.putText(im, os.path.splitext(os.path.basename(path))[0], (5, 16), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1, cv2.LINE_AA)
    return im


def main():
    out, w, cols = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    files = []
    for n in sys.argv[4:]:
        files += sorted(glob.glob(os.path.join(SRC, n + ".png")))
    ims = [tile(f, w) for f in files]
    if not ims:
        print("no frames")
        return
    blank = np.zeros_like(ims[0])
    while len(ims) % cols:
        ims.append(blank)
    rows = [np.hstack(ims[i:i + cols]) for i in range(0, len(ims), cols)]
    os.makedirs(os.path.join(SRC, "small"), exist_ok=True)
    cv2.imwrite(os.path.join(SRC, "small", out), np.vstack(rows), [cv2.IMWRITE_JPEG_QUALITY, 82])
    print(f"{len(files)} frames -> {out}")


if __name__ == "__main__":
    main()
