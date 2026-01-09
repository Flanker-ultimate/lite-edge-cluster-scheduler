from __future__ import annotations

import os
import time
import shutil
from pathlib import Path
from typing import List

import numpy as np
from PIL import Image

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff"}


def list_sub_req_dirs(ready_root: Path, service_name: str) -> List[Path]:
    service_dir = ready_root / service_name
    if not service_dir.is_dir():
        return []
    dirs = [p for p in service_dir.iterdir() if p.is_dir()]
    return sorted(dirs)


def list_images(root: Path) -> List[Path]:
    items: List[Path] = []
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in IMAGE_EXTS:
            items.append(path)
    return items


def has_any_files(root: Path) -> bool:
    for path in root.rglob("*"):
        if path.is_file() and not path.name.endswith(".part"):
            return True
    return False


def is_file_ready(path: Path, stability_check: float, max_wait: float) -> bool:
    try:
        elapsed = 0.0
        while elapsed < max_wait:
            if not path.exists():
                return False
            size1 = path.stat().st_size
            time.sleep(stability_check)
            size2 = path.stat().st_size
            if size1 == size2 and size1 > 0:
                return True
            elapsed += stability_check
        return False
    except Exception:
        return False


def segment_image(image_path: Path, output_path: Path) -> None:
    img = Image.open(image_path).convert("L")
    arr = np.array(img)
    threshold = int(arr.mean())
    mask = (arr > threshold).astype(np.uint8) * 255
    out_img = Image.fromarray(mask)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    out_img.save(output_path)


def main() -> None:
    input_dir = Path(os.getenv("INPUT_DIR", "/app/input"))
    output_dir = Path(os.getenv("OUTPUT_DIR", "/app/output"))
    service_name = os.getenv("SERVICE_NAME", "Segmentation")
    scan_interval = float(os.getenv("SCAN_INTERVAL", "1.0"))
    stability_check = float(os.getenv("STABILITY_CHECK", "0.05"))
    max_wait = float(os.getenv("MAX_WAIT", "5.0"))

    ready_root = input_dir / "_sub_reqs_ready"
    output_root = output_dir / "mask"

    print(f"[seg] input_dir={input_dir}")
    print(f"[seg] output_dir={output_dir}")
    print(f"[seg] service_name={service_name}")
    print(f"[seg] ready_root={ready_root}")

    while True:
        sub_req_dirs = list_sub_req_dirs(ready_root, service_name)
        if not sub_req_dirs:
            time.sleep(scan_interval)
            continue

        sub_req_dir = sub_req_dirs[0]
        images = list_images(sub_req_dir)
        if not images:
            if not has_any_files(sub_req_dir):
                shutil.rmtree(sub_req_dir, ignore_errors=True)
            time.sleep(scan_interval)
            continue

        for image_path in images:
            if not is_file_ready(image_path, stability_check, max_wait):
                continue
            rel_path = image_path.relative_to(sub_req_dir)
            out_name = rel_path.with_suffix(".png")
            output_path = output_root / rel_path.parent / out_name.name
            try:
                segment_image(image_path, output_path)
                image_path.unlink(missing_ok=True)
            except Exception as exc:
                print(f"[seg] error processing {image_path}: {exc}")

        if not has_any_files(sub_req_dir):
            shutil.rmtree(sub_req_dir, ignore_errors=True)

        time.sleep(scan_interval)


if __name__ == "__main__":
    main()
