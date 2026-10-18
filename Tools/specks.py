# Isolated bright specks ("snow", fireflies): python specks.py name_glob [x0 y0 x1 y1] [threshold]
# Counts pixels brighter than their 7x7 median by more than threshold (luma 0..255, default 10), per frame, in the
# region (fractions); writes small/specks_<first>.jpg with the specks circled on a brightened crop.
import cv2, glob, os, sys
import numpy as np

SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "Saved", "AutoShots"))


def main():
    pat = sys.argv[1]
    box = [float(v) for v in sys.argv[2:6]] if len(sys.argv) >= 6 else [0.0, 0.0, 1.0, 1.0]
    thr = float(sys.argv[6]) if len(sys.argv) >= 7 else 10.0
    files = sorted(glob.glob(os.path.join(SRC, pat + ".png")))
    counts = []
    for i, f in enumerate(files):
        im = cv2.imread(f)
        h, w = im.shape[:2]
        c = im[int(box[1] * h):int(box[3] * h), int(box[0] * w):int(box[2] * w)]
        y = cv2.cvtColor(c, cv2.COLOR_BGR2GRAY).astype(np.int16)
        med = cv2.medianBlur(y.astype(np.uint8), 7).astype(np.int16)
        mask = (y - med) > thr
        counts.append(int(mask.sum()))
        if i == 0:
            vis = np.clip(c.astype(np.float32) * 3.0, 0, 255).astype(np.uint8)
            for yy, xx in zip(*np.nonzero(mask)):
                cv2.circle(vis, (int(xx), int(yy)), 4, (0, 255, 255), 1)
            os.makedirs(os.path.join(SRC, "small"), exist_ok=True)
            cv2.imwrite(os.path.join(SRC, "small", "specks_" + os.path.splitext(os.path.basename(f))[0] + ".jpg"), vis, [cv2.IMWRITE_JPEG_QUALITY, 85])
    print("%s: specks per frame %s (mean %.0f)" % (pat, counts, np.mean(counts) if counts else 0))


if __name__ == "__main__":
    main()
