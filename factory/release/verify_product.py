"""Check the installed VST3 factory and class identities against pre-migration builds."""
import argparse
import json
from pathlib import Path
import re
from release import product, STUDIO

# From the six native reference bundles before the publisher change.
CLASS_IDS = {
    "livecut-redux": ("ABCDEF019182FAEB5364726E4C766374", "ABCDEF011234ABCD5364726E4C766374"),
    "tract-form": ("ABCDEF019182FAEB48626D755472666D", "ABCDEF011234ABCD48626D755472666D"),
    "pole-field": ("ABCDEF019182FAEB48626D75506C6664", "ABCDEF011234ABCD48626D75506C6664"),
    "cutline": ("ABCDEF019182FAEB48626D7543746C6E", "ABCDEF011234ABCD48626D7543746C6E"),
}


def verify(slug, bundle):
    config = product(slug)
    source = (bundle / "Contents/Resources/moduleinfo.json").read_text(encoding="utf-8-sig")
    # Steinberg's moduleinfo permits trailing commas after the last member.
    info = json.loads(re.sub(r",\s*([}\]])", r"\1", source))
    expected = {"Name": config["name"], "Version": config["version"]}
    for key, value in expected.items():
        if info[key] != value:
            raise ValueError(f"{key}: {info[key]} != {value}")
    if (info["Factory Info"]["Vendor"], info["Factory Info"]["URL"]) != (STUDIO["publisher"], STUDIO["website"]):
        raise ValueError(f"Unexpected VST3 publisher or URL for {slug}")
    classes = {item["Category"]: item for item in info["Classes"]}
    for category, cid in zip(("Audio Module Class", "Component Controller Class"), CLASS_IDS[slug]):
        entry = classes[category]
        if (entry["CID"], entry["Vendor"], entry["Name"]) != (cid, STUDIO["publisher"], config["name"]):
            raise ValueError(f"Unexpected {category} identity: {entry}")
    print(f"{config['name']} {config['version']} · {STUDIO['publisher']} · verified processor and controller IDs")


if __name__ == "__main__":
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("slug", choices=CLASS_IDS)
    args.add_argument("bundle", type=Path)
    parsed = args.parse_args()
    verify(parsed.slug, parsed.bundle)
