"""Optional developer oracle for native multipage vector drawings.

Run with a developer Python containing pypdf, Pillow and either pypdfium2 or
Poppler: python tests/drawing_oracle.py /absolute/path/agent-3d-cad
No product runtime calls Python or these packages. All CAD uses an isolated
workspace; persisted evidence is independently parsed and rasterized.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from PIL import Image
from pypdf import PdfReader


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def near(actual: float, expected: float, tolerance: float = 1e-4) -> None:
    require(abs(actual - expected) < tolerance, f"Expected {expected}, got {actual}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("service", type=pathlib.Path)
    parser.add_argument("--evidence", type=pathlib.Path,
                        default=pathlib.Path(".local/evidence/visual-workflow/drawings"))
    parser.add_argument("--pdftoppm", help="Optional explicit developer Poppler executable")
    args = parser.parse_args()
    service = args.service.resolve(strict=True)
    executable_sha256 = hashlib.sha256(service.read_bytes()).hexdigest()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    model = {
        "schema_version": 1, "units": "mm", "parameters": {},
        "features": [
            {"id": "base", "type": "box", "size": [20, 10, 6]},
            {"id": "bored", "type": "hole", "input": "base",
             "origin": [10, 5, -1], "axis": [0, 0, 1], "radius": 2, "depth": 20},
        ], "output": "bored",
    }
    recipe = {
        "title": "Independent drawing oracle", "scale": 4,
        "formats": ["pdf", "svg", "dxf"], "layout": "manual",
        "pages": [
            {"sheet": "A4", "sheet_orientation": "landscape", "views": [
                {"id": "p1top", "orientation": "top", "cell_mm": [12, 12, 270, 105]},
                {"id": "p1front", "orientation": "front", "cell_mm": [12, 123, 270, 45]},
             ], "dimensions": [{"view": "p1top", "kind": "width"}],
             "hole_callouts": [{"view": "p1top", "centers": [[10, 5]], "radius": 2,
                                "termination": "through", "fit": "H7", "label": [24, 8]}]},
            {"sheet": "A3", "sheet_orientation": "portrait", "views": [
                {"id": "p2left", "orientation": "left", "cell_mm": [12, 12, 270, 120]},
                {"id": "p2iso", "orientation": "custom", "direction": [1, -1, 1],
                 "up": [0, 0, 1], "cell_mm": [12, 140, 270, 190]},
             ]},
        ],
    }
    with tempfile.TemporaryDirectory(prefix="cad-drawing-oracle-") as temporary:
        workspace = pathlib.Path(temporary).resolve()

        def call(tool: str, value: dict) -> dict:
            source = workspace / "input.json"
            source.write_text(json.dumps(value), encoding="utf-8")
            result = subprocess.run([str(service), "call", tool, "--workspace", str(workspace),
                                     "--input", str(source)], text=True, capture_output=True,
                                    timeout=90, check=False)
            require(result.returncode == 0,
                    f"{tool} failed: {result.stderr.strip()} {result.stdout.strip()}")
            return json.loads(result.stdout)

        created = call("cad_create", {"document_id": "drawing_oracle", "model": model})
        require(created["revision"] == 1, "Fixture commits exactly one source revision")
        before = call("cad_read", {"document_id": "drawing_oracle"})
        generated = call("cad_drawing", {"document_id": "drawing_oracle", "revision": 1,
                                         "drawing": recipe})
        require(generated["document_id"] == created["document_id"] and
                generated["revision"] == created["revision"],
                "Drawing identity retains the committed fixture source revision")
        require(call("cad_read", {"document_id": "drawing_oracle"}) == before,
                "Drawing generation changes no source revision or editable data")
        require(len(generated["pages"]) == 2 and len(generated["view_layouts"]) == 4,
                "Two sheets preserve four uniquely identified views")
        hole = generated["hole_callouts"][0]
        require(hole["through"] and hole["count"] == 1 and hole["fit"] == "H7",
                "Hole designation retains individually checked termination/count/fit")
        near(hole["diameter_mm"], 4)
        near(hole["depth_mm"], 6)
        near(generated["dimensions"][0]["value_mm"], 20)
        for item in generated["artifacts"]:
            source = pathlib.Path(item["path"])
            require(source.stat().st_size == item["bytes"], "Artifact ledger byte count matches")
            shutil.copyfile(source, evidence / source.name)
        for key in ("path", "recipe_path"):
            source = pathlib.Path(generated[key])
            shutil.copyfile(source, evidence / source.name)
        (evidence / "cli-result.json").write_text(json.dumps(generated, indent=2), encoding="utf-8")
        (evidence / "created.json").write_text(json.dumps(created, indent=2), encoding="utf-8")
        (evidence / "source.json").write_text(json.dumps(model, indent=2), encoding="utf-8")

    expected_sizes = [(297, 210), (297, 420)]
    pdf = evidence / "drawing.pdf"
    reader = PdfReader(str(pdf), strict=True)
    require(len(reader.pages) == 2, "Independent PDF parser resolves a complete two-page tree")
    extracted = []
    for i, page in enumerate(reader.pages):
        width, height = expected_sizes[i]
        near(float(page.mediabox.width) * 25.4 / 72, width)
        near(float(page.mediabox.height) * 25.4 / 72, height)
        text = page.extract_text()
        extracted.append(text)
        require("Independent drawing oracle" in text and f"Sheet {i+1}/2" in text,
                "PDF text retains title and correct sheet numbering")
        require("drawing_oracle" in text and "8.0.1" in text,
                "PDF title blocks preserve native document/kernel qualification")
    require("DIA 4 H7 THRU" in extracted[0], "PDF contains actual measured bore callout text")
    (evidence / "pdf-text.txt").write_text("\n\n".join(extracted), encoding="utf-8")

    namespace = {"s": "http://www.w3.org/2000/svg"}
    circles = []
    for i, size in enumerate(expected_sizes):
        root = ET.parse(evidence / f"drawing-{i+1}.svg").getroot()
        require(root.attrib["width"] == f"{size[0]}mm" and
                root.attrib["height"] == f"{size[1]}mm", "SVG page units match ISO orientation")
        require(root.attrib["viewBox"] == f"0 0 {size[0]} {size[1]}", "SVG viewBox matches sheet")
        text = [node.text for node in root.findall("s:text", namespace)]
        require(any(f"Sheet {i+1}/2" in value for value in text if value),
                "SVG references the matching numbered PDF page")
        page_ids = [view["id"] for view in recipe["pages"][i]["views"]]
        require(all(any(view in value for value in text if value) for view in page_ids),
                "SVG names the correct globally qualified page views")
        for circle in root.findall("s:circle", namespace):
            if circle.attrib.get("data-layer") == "VISIBLE":
                circles.append(tuple(float(circle.attrib[k]) for k in ("cx", "cy", "r")))
    require(len(circles) >= 1, "SVG retains a visible native projected hole circle")
    cx, cy, radius = circles[0]
    near(radius, 8)
    placement = next(v for v in generated["view_layouts"] if v["view"] == "p1top")
    near(cx, placement["origin_mm"][0] + 4 * 10)
    near(cy, placement["origin_mm"][1] - 4 * 5)

    dpi = 120
    poppler = args.pdftoppm or shutil.which("pdftoppm")
    if poppler:
        subprocess.run([poppler, "-r", str(dpi), "-png", str(pdf), str(evidence / "page")],
                       check=True, timeout=60, capture_output=True)
        renderer = "Poppler pdftoppm"
    else:
        import pypdfium2
        document = pypdfium2.PdfDocument(str(pdf))
        for i in range(len(document)):
            document[i].render(scale=dpi/72).to_pil().save(evidence / f"page-{i+1}.png")
        document.close()
        renderer = "PDFium (pypdfium2)"
    for i, size in enumerate(expected_sizes):
        with Image.open(evidence / f"page-{i+1}.png") as image:
            require(abs(image.width - size[0] * dpi/25.4) <= 1 and
                    abs(image.height - size[1] * dpi/25.4) <= 1,
                    "Independent raster dimensions match measured ISO PDF pages")
            gray = image.convert("L")
            require(sum(1 for value in gray.tobytes() if value < 180) > 10000,
                    "Independent PDF raster contains actual geometry and annotation pixels")
    with Image.open(evidence / "page-1.png") as image:
        gray = image.convert("L")
        scale = dpi/25.4
        require(gray.getpixel((round((cx+radius*.35)*scale), round((cy+radius*.35)*scale))) > 245,
                "Independently rendered bore interior away from center marks remains empty")
        witnesses = 0
        for angle in range(0, 360, 15):
            x = round((cx+radius*math.cos(math.radians(angle)))*scale)
            y = round((cy+radius*math.sin(math.radians(angle)))*scale)
            if min(gray.getpixel((x+dx, y+dy)) for dx in (-1, 0, 1) for dy in (-1, 0, 1)) < 180:
                witnesses += 1
        require(witnesses >= 22, "Independent PDF raster paints the measured circular hole outline")

    dxf_reader = "stdlib ASCII group-pair checks; ezdxf unavailable"
    if importlib.util.find_spec("ezdxf"):
        import ezdxf
        drawing = ezdxf.readfile(evidence / "p1top.dxf")
        require(drawing.header["$INSUNITS"] == 4, "Independent DXF parser reads millimetres")
        exact = [entity for entity in drawing.modelspace().query("CIRCLE")
                 if abs(entity.dxf.radius-2) < 1e-8 and
                 abs(entity.dxf.center.x-10) < 1e-8 and abs(entity.dxf.center.y-5) < 1e-8]
        require(exact, "Independent DXF parser confirms the exact 1:1 mm native hole circle")
        require(drawing.modelspace().query('TEXT[layer=="HOLE_CALLOUTS"]'),
                "Independent DXF parser reads dedicated callout text entities")
        dxf_reader = "ezdxf"
    else:
        lines = (evidence / "p1top.dxf").read_text(encoding="ascii").splitlines()
        require(len(lines) % 2 == 0, "DXF has complete code/value pairs")
        pairs = [(int(lines[n].strip()), lines[n+1].strip()) for n in range(0, len(lines), 2)]
        units = next(n for n, pair in enumerate(pairs) if pair == (9, "$INSUNITS"))
        require(pairs[units+1] == (70, "4"), "DXF declares millimetre units")
        entities = []
        for code, value in pairs:
            if code == 0:
                entities.append({"type": value})
            elif entities:
                entities[-1][code] = value
        require(any(e["type"] == "CIRCLE" and abs(float(e.get(40, "-1"))-2) < 1e-8 and
                    abs(float(e.get(10, "-1"))-10) < 1e-8 and abs(float(e.get(20, "-1"))-5) < 1e-8
                    for e in entities), "Independent DXF group pairs retain exact 1:1 circle geometry")
        require(any(e["type"] == "TEXT" and e.get(8) == "HOLE_CALLOUTS" and
                    "DIA 4 H7 THRU" in e.get(1, "") for e in entities),
                "Independent DXF pairs preserve dedicated measured hole callout text")

    require(hashlib.sha256(service.read_bytes()).hexdigest() == executable_sha256,
            "Native executable bytes remain unchanged during oracle verification")
    report = {"executable": str(service), "executable_sha256": executable_sha256,
              "document_id": created["document_id"], "source_revision": created["revision"],
              "model_sha256": created["model_sha256"], "kernel_version": created["kernel_version"],
              "pdf_reader": "pypdf strict=True", "pdf_renderer": renderer,
              "dxf_reader": dxf_reader, "pages": expected_sizes,
              "views": [v["view"] for v in generated["view_layouts"]],
              "hole_callout": hole, "circle_raster_witnesses": witnesses,
              "source_revision_unchanged": True,
              "artifacts": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in evidence.iterdir() if p.suffix in (".pdf", ".svg", ".dxf", ".png")}}
    (evidence / "oracle-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Independent drawing oracle passed: pypdf + {renderer}, two ISO pages, "
          f"4 views, exact hole text/geometry; evidence {evidence}")


if __name__ == "__main__":
    main()
