"""生成 OpenPuls 应用图标（多尺寸 ICO）。"""
import os
from PIL import Image, ImageDraw

SS = 8  # 超采样倍数
BASE = 256
S = BASE * SS

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# 背景：竖向渐变的圆角方块
top = (10, 116, 232)
bottom = (63, 169, 255)
grad = Image.new("RGB", (1, S))
for y in range(S):
    t = y / (S - 1)
    grad.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
grad = grad.resize((S, S))

mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.235), fill=255)
img.paste(grad, (0, 0), mask)

d = ImageDraw.Draw(img)

# 白色脉冲折线（响应/心跳）
w = int(S * 0.085)
pts = [
    (S * 0.16, S * 0.56),
    (S * 0.34, S * 0.56),
    (S * 0.43, S * 0.33),
    (S * 0.56, S * 0.72),
    (S * 0.66, S * 0.49),
    (S * 0.84, S * 0.49),
]
d.line(pts, fill=(255, 255, 255, 255), width=w, joint="curve")
r = w // 2
for p in (pts[0], pts[-1]):
    d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=(255, 255, 255, 255))

img = img.resize((BASE, BASE), Image.LANCZOS)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "app.ico")
img.save(out, format="ICO", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)])
print("written", os.path.abspath(out))
