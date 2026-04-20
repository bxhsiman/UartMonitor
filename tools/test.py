#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import json
import re
import sys
from dataclasses import dataclass
from typing import Optional, Tuple

import requests


TENCENT_ENDPOINT = "https://apis.map.qq.com/ws/location/v1/network"


@dataclass
class ServingCell:
    earfcn: int
    pci: int
    rsrp: int  # dBm
    tac: int
    cellid: int
    mcc: int
    mnc: int


def _parse_ceng_serving_cell(text: str) -> Optional[ServingCell]:
    """
    Parse SIM7080G AT+CENG? output, extract +CENG: 0,"..."
    Example:
      +CENG: 0,"3688,374,-81,-68,-16,5,24341,126526347,460,00,255"
    We only need: earfcn, pci, rsrp, tac, cellid, mcc, mnc
    """
    m = re.search(r'\+CENG:\s*0,"([^"]+)"', text)
    if not m:
        return None

    parts = [p.strip() for p in m.group(1).split(",")]
    # Defensive: ensure enough fields
    # Expected indexes based on your sample:
    # 0 earfcn, 1 pci, 2 rsrp, ... , 6 tac, 7 cellid, 8 mcc, 9 mnc, ...
    if len(parts) < 10:
        return None

    try:
        earfcn = int(parts[0])
        pci = int(parts[1])
        rsrp = int(parts[2])  # already negative
        tac = int(parts[6])
        cellid = int(parts[7])
        mcc = int(parts[8])
        # mnc might be "00" -> int("00") works => 0
        mnc = int(parts[9])
        return ServingCell(
            earfcn=earfcn, pci=pci, rsrp=rsrp,
            tac=tac, cellid=cellid, mcc=mcc, mnc=mnc
        )
    except ValueError:
        return None


def build_tencent_payload(key: str, device_id: str, cell: ServingCell, get_poi: int = 0) -> dict:
    """
    Tencent doc:
      - POST JSON
      - cellinfo is array; LTE: lac=tac; cellid=cellid; rss in dBm
    """
    return {
        "key": key,
        "device_id": device_id,
        "get_poi": int(get_poi),
        "cellinfo": [
            {
                "mcc": cell.mcc,
                "mnc": cell.mnc,
                "lac": cell.tac,        # LTE: tac
                "cellid": cell.cellid,  # LTE: cellid/ECI
                "rss": cell.rsrp        # dBm
            }
        ]
    }


def post_location(payload: dict, timeout: float = 10.0) -> Tuple[int, str]:
    headers = {"Content-Type": "application/json"}
    r = requests.post(TENCENT_ENDPOINT, headers=headers, json=payload, timeout=timeout)
    return r.status_code, r.text


def main():
    ap = argparse.ArgumentParser(description="Tencent LBS (cell-only) test using SIM7080G AT+CENG? output")
    ap.add_argument("--key", required=True, help="Tencent LBS Key")
    ap.add_argument("--device-id", required=True, help="device unique id (string)")
    ap.add_argument("--ceng-file", default="-", help="File containing AT+CENG? output, or '-' for stdin")
    ap.add_argument("--get-poi", type=int, default=0, choices=[0, 1], help="0: no POI, 1: include POI")
    ap.add_argument("--timeout", type=float, default=10.0, help="HTTP timeout seconds")
    ap.add_argument("--dry-run", action="store_true", help="Only print payload, do not send request")
    args = ap.parse_args()

    if args.ceng_file == "-" or args.ceng_file.lower() == "stdin":
        text = sys.stdin.read()
    else:
        with open(args.ceng_file, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()

    cell = _parse_ceng_serving_cell(text)
    if not cell:
        print("ERROR: Cannot find/parse '+CENG: 0,\"...\"' line from input.", file=sys.stderr)
        sys.exit(2)

    payload = build_tencent_payload(args.key, args.device_id, cell, get_poi=args.get_poi)

    print("=== Parsed serving cell ===")
    print(json.dumps({
        "mcc": cell.mcc, "mnc": cell.mnc,
        "tac": cell.tac, "cellid": cell.cellid,
        "rsrp": cell.rsrp, "earfcn": cell.earfcn, "pci": cell.pci
    }, ensure_ascii=False, indent=2))

    print("\n=== Request payload ===")
    print(json.dumps(payload, ensure_ascii=False, indent=2))

    if args.dry_run:
        return

    print("\n=== POST ===")
    status, body = post_location(payload, timeout=args.timeout)
    print(f"HTTP {status}")
    try:
        print(json.dumps(json.loads(body), ensure_ascii=False, indent=2))
    except Exception:
        print(body)


if __name__ == "__main__":
    main()
