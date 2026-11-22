#!/usr/bin/env python3
"""Check the installed final SISX in an already-booted EKA2L1 Nokia 5320 via its local API."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
from time import monotonic, sleep
from urllib.error import URLError
from urllib.request import Request, urlopen

from PIL import Image, ImageChops

args = argparse.ArgumentParser(description=__doc__)
args.add_argument("package", type=Path)
args.add_argument("artwork", type=Path)
args.add_argument("selector_reference", type=Path)
args.add_argument("output", type=Path)
args.add_argument("--previous-package", type=Path)
args.add_argument("--installed-exe", type=Path)
args.add_argument("--installed-asset", type=Path)
settings = args.parse_args()
output = settings.output
output.mkdir(parents=True, exist_ok=True)

base = "http://127.0.0.1:27041/v1/"
expected = Image.open(settings.artwork).convert("RGB")
portrait = expected.transpose(Image.Transpose.ROTATE_270)
selector = Image.open(settings.selector_reference).convert("RGB")


def call(path, data=None):
    request = Request(base + path, None if data is None else json.dumps(data).encode(),
                      headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=10) as response:
        return response.read()


def info(path, data=None):
    return json.loads(call(path, data))


def screen():
    return Image.open(BytesIO(call("screenshot"))).convert("RGB")


def state(wanted, seconds=16):
    deadline = monotonic() + seconds
    while monotonic() < deadline:
        try:
            session = info("session")
            if session["state"] in wanted:
                return session
        except (URLError, TimeoutError):
            pass
        sleep(.09)
    raise RuntimeError(f"Expected {wanted}, got {info('session')}")


def comparison_region(image, target, box):
    if image.size != target.size:
        return 1.0
    diff = ImageChops.difference(image.crop(box), target.crop(box)).convert("L")
    return sum(diff.histogram()[25:]) / (diff.width * diff.height)


def launch_and_capture(prefix):
    start = monotonic()
    deadline = start + 14
    frames = {}
    with ThreadPoolExecutor(max_workers=1) as pool:
        launch = pool.submit(info, "apps/0xE93C60A3/launch", {})
        frame = None
        sampled_frames = set()
        while monotonic() < deadline:
            try:
                frame = screen()
            except (URLError, TimeoutError):
                sleep(.035)
                continue
            elapsed = round(1000 * (monotonic() - start))
            signature = sha256(frame.tobytes()).digest()
            if signature not in sampled_frames and len(sampled_frames) < 12:
                sampled_frames.add(signature)
                (output / "samples").mkdir(exist_ok=True)
                frame.save(output / "samples" / f"{prefix}_{elapsed:05}.png")
            if "splash_ms" not in frames:
                errors = {
                    "portrait_clockwise": comparison_region(frame, portrait, (90, 56, 155, 264)),
                    "portrait_counterclockwise": comparison_region(frame, expected.transpose(Image.Transpose.ROTATE_90), (90, 56, 155, 264)),
                    "landscape": comparison_region(frame, expected, (56, 85, 264, 155)),
                }
                orientation, error = min(errors.items(), key=lambda pair: pair[1])
                if error < .12 and frame.getbbox():
                    frames.update(splash_ms=elapsed, splash_orientation=orientation,
                                  differing_splash_pixels=round(error, 5))
                    frame.save(output / f"{prefix}_Splash_Raw.png")
                    if orientation == "portrait_clockwise":
                        frame = frame.transpose(Image.Transpose.ROTATE_90)
                    elif orientation == "portrait_counterclockwise":
                        frame = frame.transpose(Image.Transpose.ROTATE_270)
                    frame.save(output / f"{prefix}_Splash.png")
                elif elapsed >= 450 and "debug" not in frames:
                    frame.save(output / f"{prefix}_During_Start.png")
                    frames["debug"] = True
            if comparison_region(frame, selector, (20, 5, 230, 174)) < .012:
                frames["selector_ms"] = elapsed
                frame.save(output / f"{prefix}_Selector_Raw.png")
                frame.transpose(Image.Transpose.ROTATE_90).save(output / f"{prefix}_Selector.png")
                break
            sleep(.035)
        frames.pop("debug", None)
        frames["launch_session"] = launch.result()
    if "selector_ms" not in frames and frame is not None:
        frame.save(output / f"{prefix}_No_Selector.png")
    return frames


state({"none", "closed"})
if settings.previous_package:
    previous = info("apps", {"path": str(settings.previous_package.resolve())})
    assert len(previous["results"]) == 1 and previous["results"][0]["ok"], previous
installed = info("apps", {"path": str(settings.package.resolve())})
assert len(installed["results"]) == 1 and installed["results"][0]["ok"], installed
print(json.dumps({"installation": installed}), flush=True)
sleep(.30)
launches = []
for prefix in ("First_Launch", "Relaunch"):
    result = launch_and_capture(prefix)
    print(json.dumps({prefix: result}), flush=True)
    assert "splash_ms" in result and "selector_ms" in result, result
    sleep(.55)
    result["end_key"] = info("input/key", {"key": "no", "action": "tap"})
    result["after_end_key"] = state({"closed"})
    assert result["after_end_key"]["lastExit"]["fatal"] is None, result
    launches.append(result)
    sleep(.35)

report = {
    "package_sha256": sha256(settings.package.read_bytes()).hexdigest(),
    "installation": installed,
    "devices": info("devices"),
    "launches": launches,
    "physical_e63_tested": False,
}
if settings.previous_package:
    report["previous_package_sha256"] = sha256(settings.previous_package.read_bytes()).hexdigest()
for name, path in (("installed_exe", settings.installed_exe),
                   ("installed_asset", settings.installed_asset)):
    if path is not None:
        report[name + "_sha256"] = sha256(path.read_bytes()).hexdigest()
(output / "DARK_STARTUP_GUEST_CHECK.json").write_text(json.dumps(report, indent=2) + "\n")
