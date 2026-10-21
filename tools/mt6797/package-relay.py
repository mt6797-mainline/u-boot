#!/usr/bin/env python3
"""Package U-Boot proper for the measured X620 stock LK boot path."""
from pathlib import Path
import gzip
import hashlib
import json
import os
import struct
import subprocess

ROOT = Path(os.environ.get('MT6797_WORKSPACE') or os.environ.get('X620_WORKSPACE', Path(__file__).resolve().parents[3]))
DIST = ROOT / "dist"
OUT = ROOT / "out/u-boot"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    capacity = json.loads((ROOT / "artifacts/live-x620/partition-sizes.json").read_text())
    limit = int(capacity["boot_bytes"])
    assert limit == int(capacity["boot_sectors"]) * 512
    binary = (DIST / "u-boot.bin").read_bytes()
    assert binary[56:60] == b"ARM\x64", "LK needs the ARM64 Image header"
    offset, runtime, flags = struct.unpack_from("<QQQ", binary, 8)
    assert offset == 0x80000
    assert 0 < runtime < 0x2000000
    assert len(binary) < 0x2000000
    # Give stock LK its original hardware tree, distinct from U-Boot's control DT.
    stock_dtb = (ROOT / "artifacts/stock-x620/boot/board.dtb").read_bytes()
    assert struct.unpack_from(">I", stock_dtb)[0] == 0xd00dfeed
    dtb_size = struct.unpack_from(">I", stock_dtb, 4)[0]
    handoff = bytearray(stock_dtb[:dtb_size] + bytes(65536))
    struct.pack_into(">I", handoff, 4, len(handoff))
    assert 0x44000000 + len(handoff) < 0x44400000
    (DIST / "lk-handoff.dtb").write_bytes(handoff)
    compressed = gzip.compress(binary, compresslevel=9, mtime=0)
    (DIST / "u-boot.bin.gz").write_bytes(compressed)
    payload = compressed + handoff
    (DIST / "kernel-dtb.gz").write_bytes(payload)
    # LK only carries this ramdisk; the independent Linux slot has its own.
    # A valid empty newc archive keeps the normal LK load path within 1 MiB.
    name = b'TRAILER!!!\0'
    fields = (1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0)
    empty = b'070701' + ''.join(f'{v:08x}' for v in fields).encode() + name
    empty += bytes(-len(empty) % 512)
    ramdisk = OUT / 'relay-ramdisk.cpio.gz'
    ramdisk.write_bytes(gzip.compress(empty, compresslevel=9, mtime=0))
    temporary = DIST / ".boot.img.build"
    subprocess.run([
        "mkbootimg", "--header_version", "0", "--pagesize", "2048",
        "--board", "MT6797-RELAY",
        "--base", "0x40000000", "--kernel_offset", "0x80000",
        "--ramdisk_offset", "0x5000000", "--tags_offset", "0x4000000",
        "--kernel", str(DIST / "kernel-dtb.gz"), "--ramdisk", str(ramdisk),
        "--cmdline", "bootopt=64S3,32N2,64N2 androidboot.selinux=permissive",
        "--output", str(temporary),
    ], check=True)
    image = temporary.read_bytes()
    assert image[:8] == b"ANDROID!"
    ks, ka, rs, ra, second_size, second_addr, tags, page = struct.unpack_from("<8I", image, 8)
    assert (ka, ra, tags, page) == (0x40080000, 0x45000000, 0x44000000, 2048)
    assert second_size == 0 and ks == len(payload)
    assert image[page:page + ks] == payload
    roffset = page + (ks + page - 1) // page * page
    assert image[roffset:roffset + rs] == ramdisk.read_bytes()
    assert len(image) <= 0x100000, "Exceeds reserved 1 MiB relay prefix"
    temporary.replace(DIST / "boot.img")
    names = ["u-boot.bin", "u-boot.bin.gz", "u-boot.dtb", "u-boot.config",
             "lk-handoff.dtb", "kernel-dtb.gz", "boot.img"]
    files = {}
    for name in names:
        data = (DIST / name).read_bytes()
        files[name] = {"bytes": len(data), "sha256": digest(data)}
    repository = ROOT / "u-boot"
    source_base = subprocess.check_output(["git", "rev-parse", "HEAD"],
        cwd=repository, text=True).strip()
    source_diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=repository)
    provenance = json.loads((ROOT / "sources.json").read_text())
    upstream = next(r["commit"] for r in provenance["repositories"] if r["directory"] == "u-boot")
    metadata = {
        "device": "LeEco X620 / MT6797", "kind": "U-Boot diagnostic relay",
        "status": "built; hardware validation pending",
        "upstream_commit": upstream,
        "source_base_commit": source_base,
        "source_worktree_diff_sha256": digest(source_diff),
        "kernel_slot_entry": "0x40080000", "image_text_offset": hex(offset),
        "image_runtime_bytes": runtime, "image_flags": hex(flags),
        "lk_dtb_addr": "0x44000000", "ramdisk_addr": "0x45000000",
        "allocator_ram": ["0x48000000", "0x50000000"],
        "fastboot_buffer": ["0x48000000", "0x4c000000"],
        "boot_partition_bytes": limit, "boot_image_bytes": len(image),
        "features": {"entry_and_early_C_persistent_markers": True,
            "early_AP_RGU_watchdog_disable": True, "retained_DSI_console": "hardware-untested",
            "USB2_MTU3_fastboot": "hardware-untested", "fastboot_flash": True,
            "automatic_linux_boot": True, "eMMC_driver": True,
            "logical_boot_offset": "0x100000", "logical_boot_bytes": limit - 0x100000,
            "startup_wait_seconds": 5,
            "startup_interrupt": "VOL+ or VOL-, 30 ms debounce",
            "LK_binary_modified": False},
        "files": files,
    }
    (DIST / "build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (DIST / "SHA256SUMS").write_text("".join(f"{v['sha256']}  {k}\n" for k, v in files.items()))
    print(f"U-Boot boot.img: {len(image)} / {limit} bytes; LK entry 0x40080000")


if __name__ == "__main__":
    main()
