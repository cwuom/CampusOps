# Solves camera-space additive hand turns (UE FRotator) mapping a measured base hand frame onto desired frames.
# usage: python hand_turn.py   (edit BASE / POSES below)
import math
import numpy as np

# Measured with throwturn 0: fingers dir and palm normal (camera space, X fwd, Y right, Z up).
BASE_F = np.array([0.88, 0.34, -0.33])
BASE_P = np.array([0.03, 0.65, 0.76])

POSES = {
    # name: (fingers, palm)
    "hold": ((0.30, 0.05, 0.95), (0.30, 0.95, -0.10)),
    "wind": ((-0.15, 0.10, 0.98), (0.90, 0.35, 0.05)),
    "release": ((0.45, 0.00, 0.89), (0.85, 0.20, -0.45)),
    "follow": ((0.65, 0.45, -0.60), (0.25, 0.35, -0.90)),
}


def frame(f, p):
    f = np.array(f, float); f /= np.linalg.norm(f)
    p = np.array(p, float); p -= f * f.dot(p); p /= np.linalg.norm(p)
    return np.column_stack([f, p, np.cross(f, p)])


def rot_from_matrix(m):
    x, y, z = m[:, 0], m[:, 1], m[:, 2]
    pitch = math.degrees(math.atan2(x[2], math.hypot(x[0], x[1])))
    yaw = math.degrees(math.atan2(x[1], x[0]))
    sy = matrix_from_rot(pitch, yaw, 0.0)[:, 1]
    roll = math.degrees(math.atan2(z.dot(sy), y.dot(sy)))
    return pitch, yaw, roll


def matrix_from_rot(pitch, yaw, roll):
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sr, cr = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    X = [cp * cy, cp * sy, sp]
    Y = [sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp]
    Z = [-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp]
    return np.column_stack([X, Y, Z])


B = frame(BASE_F, BASE_P)
for name, (f, p) in POSES.items():
    D = frame(f, p)
    R = D @ B.T
    pr = rot_from_matrix(R)
    check = matrix_from_rot(*pr)
    err = np.abs(check - R).max()
    print(f"{name:8s} FRotator({pr[0]:.1f}f, {pr[1]:.1f}f, {pr[2]:.1f}f)   err={err:.3f}")
