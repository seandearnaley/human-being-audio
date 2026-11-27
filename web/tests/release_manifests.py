"""Write the factory's manifests for small test packages to stdout."""
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "factory/release"))
import release


def manifests():
    with TemporaryDirectory() as tmp:
        directory = Path(tmp)
        for slug in release.PRODUCTS:
            item = release.product(slug)
            for filename in item["assets"].values():
                (directory / filename).write_bytes(filename.encode())
            release.record(item, directory, "a" * 40)
            yield json.loads((directory / "release.json").read_text())


if __name__ == "__main__":
    print(json.dumps(list(manifests())))
