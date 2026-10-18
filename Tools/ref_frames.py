# Reference study helper: samples a local video into labelled contact sheets / frame strips (internal analysis only).
# Usage: python ref_frames.py <video> <out_dir> sheets <step_seconds>
#        python ref_frames.py <video> <out_dir> strip <start_s> <end_s> <fps>
import sys
import os
import cv2

def label(img, text):
    cv2.rectangle(img, (0, 0), (110, 22), (0, 0, 0), -1)
    cv2.putText(img, text, (4, 16), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1, cv2.LINE_AA)

def grab(cap, t, w, h):
    cap.set(cv2.CAP_PROP_POS_MSEC, t * 1000.0)
    ok, frame = cap.read()
    if not ok:
        return None
    return cv2.resize(frame, (w, h), interpolation=cv2.INTER_AREA)

def stamp(t):
    return "%d:%02d.%d" % (int(t) // 60, int(t) % 60, int((t * 10) % 10))

def sheets(cap, out, step, cols=6, rows=5, w=320, h=180):
    import numpy as np
    duration = cap.get(cv2.CAP_PROP_FRAME_COUNT) / max(cap.get(cv2.CAP_PROP_FPS), 1)
    t = 0.0
    index = 0
    while t < duration:
        canvas = np.zeros((rows * h, cols * w, 3), dtype="uint8")
        for i in range(cols * rows):
            if t >= duration:
                break
            img = grab(cap, t, w, h)
            if img is not None:
                label(img, stamp(t))
                canvas[(i // cols) * h:(i // cols + 1) * h, (i % cols) * w:(i % cols + 1) * w] = img
            t += step
        cv2.imwrite(os.path.join(out, "sheet_%02d.jpg" % index), canvas, [cv2.IMWRITE_JPEG_QUALITY, 82])
        index += 1
    print("sheets", index, "duration", round(duration, 1))

def strip(cap, out, start, end, fps, w=640, h=360, cols=3):
    import numpy as np
    times = []
    t = start
    while t <= end:
        times.append(t)
        t += 1.0 / fps
    rows = (len(times) + cols - 1) // cols
    canvas = np.zeros((rows * h, cols * w, 3), dtype="uint8")
    for i, t in enumerate(times):
        img = grab(cap, t, w, h)
        if img is not None:
            label(img, stamp(t))
            canvas[(i // cols) * h:(i // cols + 1) * h, (i % cols) * w:(i % cols + 1) * w] = img
    name = "strip_%04d_%04d.jpg" % (int(start * 10), int(end * 10))
    cv2.imwrite(os.path.join(out, name), canvas, [cv2.IMWRITE_JPEG_QUALITY, 85])
    print(name, len(times), "frames")

if __name__ == "__main__":
    video, out, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out, exist_ok=True)
    cap = cv2.VideoCapture(video)
    if mode == "sheets":
        sheets(cap, out, float(sys.argv[4]))
    else:
        strip(cap, out, float(sys.argv[4]), float(sys.argv[5]), float(sys.argv[6]))
