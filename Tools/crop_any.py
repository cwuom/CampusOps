# Crop part of a capture: python crop_any.py name x0 y0 x1 y1 [width]  (fractions of the frame); writes small/crop_<name>.jpg
import cv2, os, sys
src = os.path.join(os.path.dirname(__file__), "..", "Saved", "AutoShots")
name = sys.argv[1]
x0, y0, x1, y1 = (float(v) for v in sys.argv[2:6])
w = int(sys.argv[6]) if len(sys.argv) > 6 else 900
im = cv2.imread(os.path.join(src, name + ".png"))
h, W = im.shape[:2]
c = im[int(y0 * h):int(y1 * h), int(x0 * W):int(x1 * W)]
c = cv2.resize(c, (w, int(c.shape[0] * w / c.shape[1])), interpolation=cv2.INTER_CUBIC)
os.makedirs(os.path.join(src, "small"), exist_ok=True)
cv2.imwrite(os.path.join(src, "small", "crop_" + name + ".jpg"), c, [cv2.IMWRITE_JPEG_QUALITY, 85])
