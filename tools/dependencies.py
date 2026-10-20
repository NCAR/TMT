#!/usr/bin/env python3
"""Extract pinned local Git objects without changing their repositories."""
import argparse
import io
import json
from pathlib import Path
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repositories", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    repositories = args.repositories or root.parent
    for name, spec in json.loads((root / "dependencies.lock").read_text()).items():
        destination = args.output.resolve() / name
        marker = destination / ".tmt-source.json"
        if marker.exists() and json.loads(marker.read_text()) == spec:
            continue
        if destination.exists():
            raise SystemExit(f"Refusing to overwrite unverified dependency directory: {destination}")
        source = repositories / spec["repository"]
        archive = subprocess.check_output(["git", "-C", str(source), "archive", spec["revision"]])
        destination.mkdir(parents=True)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            for member in tar.getmembers():
                target = (destination / member.name).resolve()
                if destination not in target.parents and target != destination:
                    raise SystemExit(f"Invalid archive path: {member.name}")
                if member.issym() or member.islnk():
                    linked = (target.parent / member.linkname).resolve()
                    if destination not in linked.parents:
                        raise SystemExit(f"Invalid archive link: {member.name}")
            if hasattr(tarfile, 'data_filter'):
                tar.extractall(destination, filter='data')
            else:
                tar.extractall(destination)
        marker.write_text(json.dumps(spec, indent=2) + "\n")
        print(f"{name}: {spec['revision']}")


if __name__ == "__main__":
    main()
