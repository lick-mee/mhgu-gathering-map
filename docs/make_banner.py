"""README banner: a small pixel-art area map with gathering markers, and the tagline, on a rounded dark card."""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H, R = 2400, 640, 36
FONT = r'C:\Windows\Fonts\bahnschrift.ttf'
PARCH, SLATE = (239, 229, 204), (163, 172, 182)
MARKS = [(96, 200, 80), (80, 128, 245), (240, 220, 64), (245, 145, 45), (235, 235, 235), (245, 125, 185), (165, 165, 165)]


def card():
    img = Image.new('RGBA', (W, H))
    top, bottom = (28, 26, 30), (20, 18, 22)
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    glow = Image.new('L', (W, H))
    ImageDraw.Draw(glow).ellipse((-300, -200, 900, 900), fill=60)
    glow = glow.filter(ImageFilter.GaussianBlur(160))
    img = Image.composite(Image.new('RGBA', (W, H), (70, 52, 30, 255)), img, glow)
    mask = Image.new('L', (W * 4, H * 4))
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, W * 4 - 1, H * 4 - 1), R * 4, fill=255)
    img.putalpha(mask.resize((W, H), Image.LANCZOS))
    return img


def pixel_map():
    """A map like the game's, at 1/8 scale, enlarged with hard pixel edges."""
    s = 8
    m = Image.new('RGBA', (80, 72))
    d = ImageDraw.Draw(m)
    areas = [((6, 8, 30, 30), (122, 150, 58)), ((40, 4, 72, 28), (196, 186, 96)), ((10, 42, 36, 66), (92, 120, 66)),
             ((44, 38, 74, 64), (96, 128, 128))]
    centers = []
    for (x0, y0, x1, y1), col in areas:
        d.rounded_rectangle((x0 - 1, y0 - 1, x1 + 1, y1 + 1), 6, fill=(12, 12, 14, 255))
        d.rounded_rectangle((x0, y0, x1, y1), 5, fill=col + (255,))
        centers.append(((x0 + x1) // 2, (y0 + y1) // 2))
    for a, b in ((0, 1), (0, 2), (1, 3), (2, 3)):
        (ax, ay), (bx, by) = centers[a], centers[b]
        n = max(abs(bx - ax), abs(by - ay))
        for i in range(0, n, 3):
            x, y = ax + (bx - ax) * i // n, ay + (by - ay) * i // n
            d.rectangle((x, y, x + 1, y + 1), fill=(230, 215, 120, 255))
    marks = [(14, 15), (23, 22), (52, 12), (63, 20), (18, 52), (28, 58), (54, 46), (66, 54), (12, 25)]
    for i, (x, y) in enumerate(marks):
        d.rectangle((x - 3, y - 3, x + 3, y + 3), fill=(12, 12, 14, 255))
        d.rectangle((x - 2, y - 2, x + 2, y + 2), fill=MARKS[i % len(MARKS)] + (255,))
    return m.resize((m.width * s, m.height * s), Image.NEAREST)


def main(out):
    img = card()
    m = pixel_map()
    img.alpha_composite(m, (60, (H - m.height) // 2))
    d = ImageDraw.Draw(img)
    x = 60 + m.width + 70
    d.text((x, 205), 'Gathering spots on the map', font=ImageFont.truetype(FONT, 96), fill=PARCH)
    d.text((x, 362), 'for Monster Hunter Generations Ultimate on MHGU Recomp', font=ImageFont.truetype(FONT, 48),
           fill=SLATE)
    img.save(out)


if __name__ == '__main__':
    main(sys.argv[1])
