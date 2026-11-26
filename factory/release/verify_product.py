"""Check the installed VST3 factory and class identities against the studio catalog."""
import argparse
import json
from pathlib import Path
import re
from release import PRODUCTS, STUDIO, product

# JUCE derives VST3 processor & controller CIDs deterministically from the 4-byte
# manufacturerCode and 4-byte pluginCode in studio.json.
PROCESSOR_PREFIX = "ABCDEF019182FAEB"
CONTROLLER_PREFIX = "ABCDEF011234ABCD"


def class_ids(slug):
    config = product(slug)
    mfr = config["manufacturerCode"].encode("ascii")
    code = config["pluginCode"].encode("ascii")
    if len(mfr) != 4 or len(code) != 4:
        raise ValueError(f"Expected 4-character codes for {slug}: {config['manufacturerCode']}/{config['pluginCode']}")
    suffix = (mfr + code).hex().upper()
    return (PROCESSOR_PREFIX + suffix, CONTROLLER_PREFIX + suffix)


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
    for category, cid in zip(("Audio Module Class", "Component Controller Class"), class_ids(slug)):
        entry = classes[category]
        if (entry["CID"], entry["Vendor"], entry["Name"]) != (cid, STUDIO["publisher"], config["name"]):
            raise ValueError(f"Unexpected {category} identity: {entry}")
    print(f"{config['name']} {config['version']} · {STUDIO['publisher']} · verified processor and controller IDs")


if __name__ == "__main__":
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("slug", choices=PRODUCTS)
    args.add_argument("bundle", type=Path)
    parsed = args.parse_args()
    verify(parsed.slug, parsed.bundle)
