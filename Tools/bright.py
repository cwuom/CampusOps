# Mean brightness of the top band of each frame matching a glob: python bright.py "fv1_*" [band height px]
import cv2, glob, os, sys
import numpy as np

src = os.path.join(os.path.dirname(__file__), "..", "Saved", "AutoShots")
band = int(sys.argv[2]) if len(sys.argv) > 2 else 200
vals = []
for f in sorted(glob.glob(os.path.join(src, sys.argv[1] + ".png"))):
	im = cv2.imread(f).astype(np.float32)
	vals.append("%.0f" % im[0:band, :].mean())
print(sys.argv[1], " ".join(vals))
