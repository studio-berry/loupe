"""EasyOCR reader cache and request processing for LoopOcrService."""

from __future__ import annotations

import io
import math
import os
import re
import stat
from typing import Any

DEFAULT_LANGUAGES = ["en"]
DEFAULT_MEDIA_BOX = {"x": 0.0, "y": 0.0, "width": 612.0, "height": 792.0}
MAX_DPI = 1200

# A staged page raster at 1200 dpi is large but bounded; anything past this is
# not something PdfTool produced, so refuse it rather than loading it.
MAX_IMAGE_BYTES = 512 * 1024 * 1024

# Language codes are ISO 639-1/639-2 style tokens, optionally with a script or
# region suffix ("ch_sim", "en"). Codes reach easyocr.Reader, which uses them to
# build model file names, so they are shape-checked here: a value like
# "../../etc" must never get that far.
_LANGUAGE_PATTERN = re.compile(r"^[a-z]{2,3}(?:_[a-z]{2,4})?$")
_readers: dict[tuple[str, ...], object] = {}


def _finite(value: Any, default: float = 0.0) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return default
    return number if math.isfinite(number) else default


def normalize_languages(value: object) -> list[str]:
    if value is None:
        return list(DEFAULT_LANGUAGES)

    if not isinstance(value, list):
        raise ValueError("languages must be an array")

    languages = [
        str(language).strip().lower()
        for language in value
        if str(language).strip()
    ]

    if not languages:
        return list(DEFAULT_LANGUAGES)

    for language in languages:
        if not _LANGUAGE_PATTERN.match(language):
            raise ValueError(f"language code is not a valid identifier: {language!r}")

    return sorted(set(languages))


def _finite_request_number(value: object, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(float(value)):
        raise ValueError(f"{field} must be a finite number")
    return float(value)


def validate_request(request: dict[str, Any]) -> dict[str, Any]:
    page = request.get("page")
    if isinstance(page, bool) or not isinstance(page, int) or page < 1:
        raise ValueError("page must be an integer >= 1")

    dpi = request.get("dpi")
    if isinstance(dpi, bool) or not isinstance(dpi, int) or dpi < 1 or dpi > MAX_DPI:
        raise ValueError(f"dpi must be an integer between 1 and {MAX_DPI}")

    image = request.get("image")
    if not isinstance(image, str) or not image.strip():
        raise ValueError("image must be a non-empty path")

    media_box_value = request.get("media_box")
    media_box = DEFAULT_MEDIA_BOX if media_box_value is None else media_box_value
    if not isinstance(media_box, dict):
        raise ValueError("media_box must be an object")

    normalized_media_box = {
        field: _finite_request_number(media_box.get(field), f"media_box.{field}")
        for field in ("x", "y", "width", "height")
    }
    if normalized_media_box["width"] <= 0 or normalized_media_box["height"] <= 0:
        raise ValueError("media_box width and height must be positive")

    rotation = request.get("rotation", 0)
    if isinstance(rotation, bool) or not isinstance(rotation, int) or rotation not in (0, 90, 180, 270):
        raise ValueError("rotation must be one of 0, 90, 180, or 270")

    return {
        "page": page,
        "dpi": dpi,
        "image": image.strip(),
        "languages": normalize_languages(request.get("languages")),
        "media_box": normalized_media_box,
        "rotation": rotation,
    }


def model_storage_directory() -> str:
    program_data = os.environ.get("PROGRAMDATA")
    if program_data:
        return os.path.join(program_data, "Loop", "ocr-models")
    return os.path.join(os.path.expanduser("~"), ".loop", "ocr-models")


def get_reader(languages: list[str], allow_download: bool = False):
    lang_key = tuple(normalize_languages(languages))
    if lang_key not in _readers:
        import easyocr

        os.makedirs(model_storage_directory(), exist_ok=True)
        _readers[lang_key] = easyocr.Reader(
            list(lang_key),
            gpu=False,
            model_storage_directory=model_storage_directory(),
            download_enabled=allow_download,
            verbose=False,
        )
    return _readers[lang_key]


def pixel_bbox_to_pdf(
    bbox_pixels: list[list[float]],
    image_width: int,
    image_height: int,
    media_box: dict[str, float],
    rotation: int = 0,
) -> dict[str, float]:
    media_x = _finite(media_box.get("x", 0.0))
    media_y = _finite(media_box.get("y", 0.0))
    media_w = _finite(media_box.get("width", 0.0))
    media_h = _finite(media_box.get("height", 0.0))

    if image_width <= 0 or image_height <= 0 or media_w <= 0 or media_h <= 0:
        return {"x": media_x, "y": media_y, "width": 0.0, "height": 0.0}

    points: list[tuple[float, float]] = []
    for point in bbox_pixels:
        if not isinstance(point, (list, tuple)) or len(point) < 2:
            return {"x": media_x, "y": media_y, "width": 0.0, "height": 0.0}
        x = _finite(point[0], float("nan"))
        y = _finite(point[1], float("nan"))
        if not math.isfinite(x) or not math.isfinite(y):
            return {"x": media_x, "y": media_y, "width": 0.0, "height": 0.0}
        points.append((x / image_width, y / image_height))

    if not points:
        return {"x": media_x, "y": media_y, "width": 0.0, "height": 0.0}

    left = min(point[0] for point in points)
    right = max(point[0] for point in points)
    top = min(point[1] for point in points)
    bottom = max(point[1] for point in points)

    if rotation == 90:
        x_min, x_max = top * media_w, bottom * media_w
        y_min, y_max = left * media_h, right * media_h
    elif rotation == 180:
        x_min, x_max = (1.0 - right) * media_w, (1.0 - left) * media_w
        y_min, y_max = (1.0 - bottom) * media_h, (1.0 - top) * media_h
    elif rotation == 270:
        x_min, x_max = (1.0 - bottom) * media_w, (1.0 - top) * media_w
        y_min, y_max = (1.0 - right) * media_h, (1.0 - left) * media_h
    else:
        x_min, x_max = left * media_w, right * media_w
        y_min, y_max = (1.0 - bottom) * media_h, (1.0 - top) * media_h

    x_pt = media_x + x_min
    y_pt = media_y + y_min
    width_pt = max(0.0, x_max - x_min)
    height_pt = max(0.0, y_max - y_min)

    return {
        "x": _finite(x_pt),
        "y": _finite(y_pt),
        "width": _finite(width_pt),
        "height": _finite(height_pt),
    }


def _read_staged_image(image_path: str) -> bytes:
    """Reads a staged page raster exactly once, by descriptor.

    PdfTool stages the raster into a private temporary directory and hands us the
    path. Re-resolving that path for every use - an existence check, then PIL,
    then the OCR reader - is three chances for the file behind the name to change
    between them. Opening once and passing the bytes onward removes the window,
    and O_NOFOLLOW (where the platform has it) refuses a name that has been
    turned into a symlink.
    """
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(image_path, flags)
    try:
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode):
            raise ValueError("staged image is not a regular file")
        if status.st_size > MAX_IMAGE_BYTES:
            raise ValueError(f"staged image exceeds {MAX_IMAGE_BYTES} bytes")

        with os.fdopen(descriptor, "rb") as handle:
            descriptor = -1
            return handle.read()
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def run_ocr(request: dict[str, Any]) -> dict[str, Any]:
    from PIL import Image

    normalized = validate_request(request)
    image_path = normalized["image"]
    page = normalized["page"]
    languages = normalized["languages"]
    media_box = normalized["media_box"]
    rotation = normalized["rotation"]

    if not image_path:
        return {"page": page, "ok": False, "error": "missing image path"}

    try:
        image_bytes = _read_staged_image(image_path)
    except FileNotFoundError:
        return {"page": page, "ok": False, "error": f"image not found: {image_path}"}
    except (IsADirectoryError, OSError, ValueError) as error:
        return {"page": page, "ok": False, "error": f"image could not be read: {error}"}

    reader = get_reader(list(languages))
    with Image.open(io.BytesIO(image_bytes)) as image:
        image_width, image_height = image.size

    results = reader.readtext(image_bytes)
    lines = []
    text_parts: list[str] = []
    for bbox_pixels, text, confidence in results:
        text = str(text)
        text_parts.append(text)
        lines.append(
            {
                "text": text,
                "confidence": _finite(confidence),
                "bbox": pixel_bbox_to_pdf(bbox_pixels, image_width, image_height, media_box, rotation),
            }
        )

    return {
        "page": page,
        "ok": True,
        "text": "\n".join(text_parts),
        "lines": lines,
    }
