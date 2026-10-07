"""UE X-forward/Y-right/Z-up (left-handed) -> MC X/Y-up/Z (right-handed).
The GTA example's -Y mapping mirrors UE's right vector; this conversion does not.
UE [X,Y,Z] centimetres -> MC [X,Z,Y]/100 + origin; yawMC=yawUE-90.
"""
from dataclasses import dataclass
import math

@dataclass(frozen=True)
class Transform:
    ox: float = 0.0
    oy: float = 64.0
    oz: float = 0.0

    def to_mc(self, p):
        return [p[0] / 100 + self.ox, p[2] / 100 + self.oy, p[1] / 100 + self.oz]

    def to_ue(self, p):
        return [(p[0] - self.ox) * 100, (p[2] - self.oz) * 100, (p[1] - self.oy) * 100]

    def box_to_mc(self, box):
        a, b = self.to_mc(box[:3]), self.to_mc(box[3:])
        return [min(a[i], b[i]) for i in range(3)] + [max(a[i], b[i]) for i in range(3)]

def mc_yaw(ue_yaw):
    return (ue_yaw - 90) % 360

def ue_yaw(yaw):
    return (yaw + 90) % 360

def vertical_fov(horizontal, width, height):
    return math.degrees(2 * math.atan(math.tan(math.radians(horizontal) / 2) * height / width))

def horizontal_fov(vertical, width, height):
    return math.degrees(2 * math.atan(math.tan(math.radians(vertical) / 2) * width / height))

def scaled_view(width, height, max_pixels=1920*1080):
    scale = min(1.0, math.sqrt(max_pixels / max(1, width*height)))
    return max(320, int(width*scale)), max(200, int(height*scale))
