#!/usr/bin/env python3
"""Manage Sentinel's local, plaintext Wi-Fi configuration.

Only record credentials for networks you own or are explicitly allowed to test.
Passwords are prompted interactively so they do not appear in shell history.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
from getpass import getpass
import os
from pathlib import Path
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[1]
HEADER_PATH = PROJECT_ROOT / "include" / "wifi_credentials.h"
JOURNAL_PATH = PROJECT_ROOT / "local" / "confirmed_wifi_credentials.csv"
CSV_FIELDS = ("ssid", "password", "source", "verified_at")


def validate(ssid: str, password: str) -> None:
    ssid_length = len(ssid.encode("utf-8"))
    password_length = len(password.encode("utf-8"))

    if not 1 <= ssid_length <= 32:
        raise ValueError("SSID must contain 1-32 UTF-8 bytes")
    if password and not 8 <= password_length <= 63:
        raise ValueError("WPA/WPA2 password must contain 8-63 UTF-8 bytes")
    if any(ord(character) < 32 for character in ssid + password):
        raise ValueError("control characters are not supported")


def c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(dir=path.parent, text=True)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as file:
            file.write(content)
        os.chmod(temporary_path, 0o600)
        temporary_path.replace(path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def prompt_credentials() -> tuple[str, str]:
    ssid = input("Wi-Fi SSID: ").strip()
    password = getpass("Wi-Fi password (empty for open network): ")
    validate(ssid, password)
    return ssid, password


def configure(_: argparse.Namespace) -> None:
    ssid, password = prompt_credentials()
    content = (
        "#pragma once\n\n"
        "// Generated locally by tools/wifi_credentials.py; excluded from Git.\n"
        f'#define SENTINEL_WIFI_SSID "{c_string(ssid)}"\n'
        f'#define SENTINEL_WIFI_PASSWORD "{c_string(password)}"\n'
    )
    atomic_write(HEADER_PATH, content)
    print(f"Updated {HEADER_PATH}")


def read_rows() -> list[dict[str, str]]:
    if not JOURNAL_PATH.exists():
        return []
    with JOURNAL_PATH.open(encoding="utf-8", newline="") as file:
        return list(csv.DictReader(file))


def write_rows(rows: list[dict[str, str]]) -> None:
    JOURNAL_PATH.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(dir=JOURNAL_PATH.parent, text=True)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=CSV_FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        os.chmod(temporary_path, 0o600)
        temporary_path.replace(JOURNAL_PATH)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def record(args: argparse.Namespace) -> None:
    ssid, password = prompt_credentials()
    rows = [row for row in read_rows() if row.get("ssid") != ssid]
    rows.append(
        {
            "ssid": ssid,
            "password": password,
            "source": args.source,
            "verified_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        }
    )
    write_rows(rows)
    print(f"Recorded confirmed credentials for {ssid!r} in {JOURNAL_PATH}")


def list_entries(_: argparse.Namespace) -> None:
    rows = read_rows()
    if not rows:
        print("No confirmed credentials recorded")
        return
    for row in rows:
        state = "open" if not row.get("password") else "password saved"
        print(
            f"{row.get('ssid', '')}: {state}; "
            f"source={row.get('source', '')}; verified={row.get('verified_at', '')}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    configure_parser = subcommands.add_parser(
        "configure", help="write the build-time local Wi-Fi header"
    )
    configure_parser.set_defaults(handler=configure)
    record_parser = subcommands.add_parser(
        "record", help="record credentials already confirmed by the owner"
    )
    record_parser.add_argument("--source", default="manual")
    record_parser.set_defaults(handler=record)
    list_parser = subcommands.add_parser("list", help="list records without passwords")
    list_parser.set_defaults(handler=list_entries)
    return parser


def main() -> int:
    try:
        args = build_parser().parse_args()
        args.handler(args)
        return 0
    except (EOFError, KeyboardInterrupt):
        print("\nCancelled")
        return 130
    except ValueError as error:
        print(f"Error: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
