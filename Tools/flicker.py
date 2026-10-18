# Temporal noise of a static-camera sequence: python flicker.py out.jpg name_glob [x0 y0 x1 y1]
# Prints the mean and 99th percentile of the per-pixel luma standard deviation (0..255) inside the region (fractions),
# and writes small/out.jpg: the first frame beside the heat map (white = flickering).
import cv2, glob, os, sys
import numpy as np

SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "Saved", "AutoShots"))


def main():
    out, pat = sys.argv[1], sys.argv[2]
    box = [float(v) for v in sys.argv[3:7]] if len(sys.argv) >= 7 else [0.0, 0.0, 1.0, 1.0]
    files = sorted(glob.glob(os.path.join(SRC, pat + ".png")))
    if len(files) < 2:
        print("need 2+ frames")
        return
    ims = [cv2.imread(f) for f in files]
    h, w = ims[0].shape[:2]
    x0, y0, x1, y1 = int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)
    luma = np.stack([cv2.cvtColor(i, cv2.COLOR_BGR2GRAY).astype(np.float32) for i in ims])[:, y0:y1, x0:x1]
    sd = luma.std(axis=0)
    print("%s: %d frames, temporal sd mean %.2f, p99 %.2f, pixels over 8: %.2f%%" % (pat, len(files), sd.mean(), np.percentile(sd, 99), (sd > 8).mean() * 100))
    heat = cv2.applyColorMap(np.clip(sd * 10, 0, 255).astype(np.uint8), cv2.COLORMAP_INFERNO)
    crop = ims[0][y0:y1, x0:x1]
    both = np.hstack([crop, heat])
    scale = min(1.0, 1600.0 / both.shape[1])
    both = cv2.resize(both, (int(both.shape[1] * scale), int(both.shape[0] * scale)), interpolation=cv2.INTER_AREA)
    os.makedirs(os.path.join(SRC, "small"), exist_ok=True)
    cv2.imwrite(os.path.join(SRC, "small", out), both, [cv2.IMWRITE_JPEG_QUALITY, 85])


if __name__ == "__main__":
    main()
