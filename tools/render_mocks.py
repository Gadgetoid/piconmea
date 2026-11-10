# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///

import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-mock"
DOCS = ROOT / "docs"
SCALE = 2

SOURCES = [
    ROOT / "tools/mock/mock_main.c",
    ROOT / "tools/mock/display_mock.c",
    ROOT / "src/gpsview.c",
    ROOT / "src/nmea.c",
    ROOT / "src/font8x8.c",
]

SYSTEM_IDS = {"GP": 1, "GL": 2, "GA": 3, "GB": 4}

SCENARIOS = {
    "stonehenge": {
        "time": "035200.00",
        "date": "210626",
        "latitude": 51.17886,
        "longitude": -1.82621,
        "altitude": 102.0,
        "speed_mph": 2.0,
        "course": 52.0,
        "fix": True,
        "dop": (1.4, 0.8, 1.1),
        "satellites": {
            "GP": [(5, 72, 110, 44, True), (13, 58, 290, 41, True), (15, 41, 62, 38, True), (18, 33, 198, 35, True), (20, 24, 320, 31, True), (24, 15, 145, 27, False), (29, 62, 20, 46, True), (10, 8, 250, 18, False)],
            "GL": [(65, 55, 80, 40, True), (66, 30, 150, 34, True), (72, 40, 250, 36, True), (81, 12, 330, 22, False), (88, 20, 30, 29, True)],
            "GA": [(4, 48, 175, 39, True), (9, 26, 300, 33, True), (11, 66, 230, 42, True), (36, 10, 95, 20, False)],
            "GB": [(19, 35, 120, 30, True), (22, 18, 210, 24, False)],
        },
        "simulating": False,
        "bluetooth": True,
        "battery": (True, 3.95, 78, False),
    },
    "acquiring": {
        "time": "034112.00",
        "date": "210626",
        "fix": False,
        "dop": (99.9, 99.9, 99.9),
        "satellites": {
            "GP": [(5, 70, 108, 21, False), (13, 57, 288, 0, False), (29, 61, 22, 26, False), (15, 40, 60, 0, False)],
            "GL": [(65, 54, 82, 17, False), (72, 41, 248, 0, False)],
        },
        "simulating": False,
        "bluetooth": False,
        "battery": (True, 4.12, 92, True),
    },
}


def sentence(body):
    checksum = 0
    for character in body:
        checksum ^= ord(character)
    return f"${body}*{checksum:02X}"


def coordinate(value, degree_digits, positive, negative):
    hemisphere = positive if value >= 0 else negative
    value = abs(value)
    degrees = int(value)
    minutes = (value - degrees) * 60
    return f"{degrees:0{degree_digits}d}{minutes:07.4f},{hemisphere}"


def scenario_nmea(scenario):
    lines = []
    used = sum(1 for satellites in scenario["satellites"].values() for satellite in satellites if satellite[4])
    pdop, hdop, vdop = scenario["dop"]

    for talker, satellites in scenario["satellites"].items():
        groups = [satellites[i:i + 4] for i in range(0, len(satellites), 4)]
        for index, group in enumerate(groups, start=1):
            fields = [f"{talker}GSV", str(len(groups)), str(index), str(len(satellites))]
            for prn, elevation, azimuth, snr, _ in group:
                fields += [f"{prn:02d}", str(elevation), f"{azimuth:03d}", f"{snr:02d}" if snr else ""]
            lines.append(sentence(",".join(fields)))

        prns = [f"{satellite[0]:02d}" for satellite in satellites if satellite[4]][:12]
        prns += [""] * (12 - len(prns))
        mode = "3" if scenario["fix"] else "1"
        lines.append(sentence(",".join(["GNGSA", "A", mode, *prns, f"{pdop:.1f}", f"{hdop:.1f}", f"{vdop:.1f}", str(SYSTEM_IDS[talker])])))

    if scenario["fix"]:
        latitude = coordinate(scenario["latitude"], 2, "N", "S")
        longitude = coordinate(scenario["longitude"], 3, "E", "W")
        knots = scenario["speed_mph"] * 0.868976
        lines.append(sentence(f"GNGGA,{scenario['time']},{latitude},{longitude},1,{used:02d},{hdop:.1f},{scenario['altitude']:.1f},M,47.0,M,,"))
        lines.append(sentence(f"GNRMC,{scenario['time']},A,{latitude},{longitude},{knots:.2f},{scenario['course']:.2f},{scenario['date']},,,A"))
    else:
        lines.append(sentence(f"GNGGA,{scenario['time']},,,,,0,00,{hdop:.1f},,M,,M,,"))
        lines.append(sentence(f"GNRMC,{scenario['time']},V,,,,,,,{scenario['date']},,,N"))
    return "\r\n".join(lines) + "\r\n"


def main():
    BUILD.mkdir(exist_ok=True)
    DOCS.mkdir(exist_ok=True)
    harness = BUILD / "mock"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-O2", f"-I{ROOT / 'src'}", "-o", str(harness), *map(str, SOURCES), "-lm"], check=True)

    for name, scenario in SCENARIOS.items():
        nmea_path = BUILD / f"{name}.nmea"
        frame_path = BUILD / f"{name}.ppm"
        nmea_path.write_text(scenario_nmea(scenario))
        present, voltage, percent, usb_power = scenario["battery"]
        subprocess.run([str(harness), str(nmea_path), str(frame_path), str(int(scenario["simulating"])), str(int(scenario["bluetooth"])), str(int(present)), str(voltage), str(percent), str(int(usb_power))], check=True)

        image = Image.open(frame_path)
        image = image.resize((image.width * SCALE, image.height * SCALE), Image.Resampling.NEAREST)
        output = DOCS / f"display-{name}.png"
        image.save(output, optimize=True)
        print(output.relative_to(ROOT))


if __name__ == "__main__":
    main()
