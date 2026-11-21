"""Add the studio welcome panel to Apple's synthesized component distribution."""
from html import escape
import shutil
import sys
from pathlib import Path
import xml.etree.ElementTree as ET
from release import product, ROOT, STUDIO

directory = Path(sys.argv[1])
item = product(sys.argv[2])
name = escape(item["name"])
publisher = escape(STUDIO["publisher"])
resources = directory / "resources"
resources.mkdir()
shutil.copyfile(ROOT / "design/studio/welcome.png", resources / "welcome.png")
(resources / "welcome.html").write_text(f"""<!doctype html><html lang="en"><meta charset="utf-8">
<style>body {{font: 13px -apple-system, sans-serif}} header {{background:#21211f;color:#eabe72;padding:12px;font-weight:bold}}</style>
<header>{publisher}</header><h2>{name}</h2><p>Version {item["version"]} · {item["license"]}</p>
<p>Install the Audio Unit, VST3 plug-in and standalone app. Close your audio host before installing.</p>
<p>The plug-ins go into the system Audio Plug-Ins folder and the app goes into Applications.</p>
<p><a href="{STUDIO["website"]}">{publisher}</a></p></html>""", encoding="utf-8")
file = directory / "distribution.xml"
xml = ET.parse(file)
root = xml.getroot()
ET.SubElement(root, "title").text = f"{item['name']} · {STUDIO['publisher']}"
ET.SubElement(root, "welcome", {"file": "welcome.html", "mime-type": "text/html"})
for element in ("background", "background-darkAqua"):
    ET.SubElement(root, element, {"file": "welcome.png", "mime-type": "image/png", "alignment": "bottomleft", "scaling": "none"})
xml.write(file, encoding="utf-8", xml_declaration=True)
