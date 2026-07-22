#!/usr/bin/env python3
"""Download and extract macOS SDK from Apple's Command Line Tools package.

Apple publishes Command Line Tools (CLT) packages via their publicly
accessible software update catalog.  This script downloads the latest CLT,
extracts just the macOS SDK, and packages it as a .tar.gz tarball suitable
for osxcross.

No Apple ID or paid developer account required.

Usage:
    python3 sdk-fetcher.py [output_dir]

The script prints the absolute path of the resulting tarball to stdout.
All progress / status messages go to stderr.
If a cached SDK tarball already exists in output_dir, it is reused.

Dependencies: python3 (>= 3.6), cpio, tar, gzip
Optional:     bsdtar (libarchive-tools) or xar -- faster XAR extraction.
              Falls back to a pure-Python XAR parser when neither is available.
"""

import glob
import gzip
import lzma
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zlib

try:
    import defusedxml.ElementTree as ET
except ImportError as exc:
    raise ImportError(
        "defusedxml is required: pip install defusedxml"
    ) from exc

# -- Configuration -----------------------------------------------------------

CATALOG_URLS = [
    # Try newest catalog first; first successful fetch wins.
    "https://swscan.apple.com/content/catalogs/others/"
    "index-16-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-"
    "mountainlion-lion-snowleopard-leopard.merged-1.sucatalog.gz",

    "https://swscan.apple.com/content/catalogs/others/"
    "index-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-"
    "mountainlion-lion-snowleopard-leopard.merged-1.sucatalog.gz",

    "https://swscan.apple.com/content/catalogs/others/"
    "index-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-"
    "mountainlion-lion-snowleopard-leopard.merged-1.sucatalog.gz",
]

USER_AGENT = "Software%20Update"


# -- Helpers -----------------------------------------------------------------

def _validate_url(url):
    """Reject non-HTTPS URLs to prevent file:// and other scheme attacks."""
    if not url.startswith("https://"):
        raise ValueError(f"Refusing non-HTTPS URL: {url}")


def log(msg):
    print(msg, file=sys.stderr, flush=True)


# -- 1) Catalog & URL discovery ----------------------------------------------

def find_sdk_pkg_url():
    """Search Apple catalogs for the latest CLTools_macOSNMOS_SDK.pkg URL."""
    for cat_url in CATALOG_URLS:
        short = cat_url.split("/index-")[1][:25] + "..."
        log(f"    Trying catalog: {short}")
        try:
            _validate_url(cat_url)
            req = urllib.request.Request(cat_url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=60) as resp:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
                raw = gzip.decompress(resp.read())
            catalog = plistlib.loads(raw)
        except Exception as exc:
            log(f"    -> fetch failed: {exc}")
            continue

        products = catalog.get("Products", {})
        candidates = []
        for pid, product in products.items():
            post_date = str(product.get("PostDate", ""))
            for pkg in product.get("Packages", []):
                url = pkg.get("URL", "")
                size = pkg.get("Size", 0)
                if "CLTools_macOSNMOS_SDK" in url and url.endswith(".pkg"):
                    candidates.append((post_date, url, size, pid))

        if not candidates:
            log(f"    -> no CLTools SDK packages in this catalog, trying next...")
            continue

        candidates.sort(reverse=True)
        _date, url, size, pid = candidates[0]
        log(f"==> Found: CLTools_macOSNMOS_SDK  (product {pid}, {size // 1048576} MB)")
        return url

    log("ERROR: No CLTools SDK packages found in any Apple catalog.")
    sys.exit(1)


# -- 2) Download -------------------------------------------------------------

def download(url, dest):
    """Download *url* to *dest* with a basic progress indicator."""
    _validate_url(url)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=600) as resp:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
        total = int(resp.headers.get("Content-Length", 0))
        done = 0
        with open(dest, "wb") as f:
            while True:
                chunk = resp.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                done += len(chunk)
                if total:
                    pct = done * 100 // total
                    log(f"\r    {done // 1048576} / {total // 1048576} MB  ({pct}%)")
    log("")


# -- 3) XAR extraction -------------------------------------------------------

def extract_xar(pkg_path, dest_dir):
    """Extract a XAR (.pkg) archive -- external tool or pure-Python fallback."""
    for tool in ("bsdtar", "xar"):
        if shutil.which(tool):
            log(f"==> Extracting .pkg with {tool}...")
            r = subprocess.run([tool, "-xf", pkg_path, "-C", dest_dir],
                               capture_output=True)
            if r.returncode == 0:
                return
            log(f"    {tool} exited {r.returncode}, trying next method...")

    log("==> Extracting .pkg with built-in Python XAR parser...")
    _extract_xar_python(pkg_path, dest_dir)


def _extract_xar_python(pkg_path, dest_dir):
    """Pure-Python XAR extractor (no external dependencies)."""
    with open(pkg_path, "rb") as f:
        raw = f.read(28)
        if len(raw) < 28:
            raise ValueError("File too small to be a valid XAR archive")
        magic, hdr_size, _ver, toc_clen, _toc_ulen, _ck = struct.unpack(
            ">4sHHQQI", raw,
        )
        if magic != b"xar!":
            raise ValueError(f"Not a XAR file (magic: {magic!r})")

        f.seek(hdr_size)
        toc_xml = zlib.decompress(f.read(toc_clen))
        heap_off = hdr_size + toc_clen

        root = ET.fromstring(toc_xml)
        toc = root.find("toc")
        if toc is None:
            raise ValueError("Malformed XAR: no <toc> element")

        def _walk(elem, base):
            for fe in elem.findall("file"):
                name = fe.findtext("name", "")
                ftype = fe.findtext("type", "file")
                path = os.path.join(base, name)

                if ftype == "directory":
                    os.makedirs(path, exist_ok=True)
                    _walk(fe, path)
                    continue

                de = fe.find("data")
                if de is None:
                    continue
                offset = int(de.findtext("offset", "0"))
                size = int(de.findtext("size", "0"))
                enc_el = de.find("encoding")
                enc = enc_el.get("style", "") if enc_el is not None else ""

                os.makedirs(os.path.dirname(path), exist_ok=True)
                f.seek(heap_off + offset)

                if "gzip" in enc:
                    with open(path, "wb") as out:
                        out.write(zlib.decompress(f.read(size), 15 + 32))
                elif "bzip2" in enc:
                    import bz2
                    with open(path, "wb") as out:
                        out.write(bz2.decompress(f.read(size)))
                else:
                    with open(path, "wb") as out:
                        rem = size
                        while rem > 0:
                            blk = f.read(min(rem, 1 << 20))
                            if not blk:
                                break
                            out.write(blk)
                            rem -= len(blk)

        _walk(toc, dest_dir)


# -- 4) Payload extraction (pbzx / gzip cpio) --------------------------------

def _pbzx_stream(path):
    """Yield decompressed chunks from a pbzx-compressed file."""
    with open(path, "rb") as f:
        if f.read(4) != b"pbzx":
            raise ValueError("Not a pbzx file")
        f.read(8)
        while True:
            hdr = f.read(16)
            if len(hdr) < 16:
                break
            _usize, csize = struct.unpack(">QQ", hdr)
            data = f.read(csize)
            if len(data) < csize:
                break
            if csize == _usize:
                yield data
            else:
                yield lzma.decompress(data)


def _gzip_stream(path):
    """Yield decompressed chunks from a gzip file."""
    with gzip.open(path, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            yield chunk


def _raw_stream(path):
    """Yield raw 1 MiB chunks (last resort)."""
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            yield chunk


def extract_payload(payload_path, out_dir):
    """Decompress a CLT Payload (pbzx or gzip cpio) into *out_dir*."""
    with open(payload_path, "rb") as pf:
        magic = pf.read(4)

    if magic == b"pbzx":
        log("    Payload format: pbzx (LZMA chunks)")
        stream = _pbzx_stream(payload_path)
    elif magic[:2] == b"\x1f\x8b":
        log("    Payload format: gzip")
        stream = _gzip_stream(payload_path)
    else:
        log(f"    Payload format: unknown (magic: {magic.hex()}), trying raw cpio...")
        stream = _raw_stream(payload_path)

    proc = subprocess.Popen(
        ["cpio", "-id", "--quiet"],
        stdin=subprocess.PIPE,
        cwd=out_dir,
        stderr=subprocess.PIPE,
    )
    for chunk in stream:
        try:
            proc.stdin.write(chunk)
        except BrokenPipeError:
            break
    proc.stdin.close()
    proc.wait()


# -- Main --------------------------------------------------------------------

def main():
    output_dir = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.getcwd()
    os.makedirs(output_dir, exist_ok=True)

    # Re-use a previously fetched SDK if present.
    cached = glob.glob(os.path.join(output_dir, "MacOSX*.sdk.tar.*"))
    if cached:
        cached.sort()
        result = os.path.realpath(cached[-1])
        log(f"==> Using cached SDK: {os.path.basename(result)}")
        print(result)
        return

    work = tempfile.mkdtemp(prefix="fetch-macos-sdk-")

    try:
        # 1 -- Locate SDK package URL from Apple's catalog
        log("==> Searching Apple software-update catalogs...")
        sdk_url = find_sdk_pkg_url()

        # 2 -- Download (just the SDK component, ~55 MB)
        pkg = os.path.join(work, "sdk.pkg")
        log("==> Downloading CLTools SDK package...")
        download(sdk_url, pkg)

        # 3 -- Extract the flat .pkg (XAR format) to get the Payload
        pkg_dir = os.path.join(work, "pkg")
        os.makedirs(pkg_dir)
        extract_xar(pkg, pkg_dir)
        os.unlink(pkg)

        # 4 -- Locate the Payload file
        log("==> Locating SDK payload...")
        sdk_payload = None
        for dirpath, _dirs, files in os.walk(pkg_dir):
            if "Payload" in files:
                sdk_payload = os.path.join(dirpath, "Payload")
                log(f"    Found: {os.path.relpath(sdk_payload, pkg_dir)}")
                break

        if sdk_payload is None:
            log("ERROR: No Payload found in extracted package")
            sys.exit(1)

        # 5 -- Decompress Payload -> cpio -> filesystem
        sdk_root = os.path.join(work, "sdk")
        os.makedirs(sdk_root)
        log("==> Extracting SDK from payload (this may take a minute)...")
        extract_payload(sdk_payload, sdk_root)
        shutil.rmtree(pkg_dir)

        # 6 -- Find MacOSX*.sdk directory
        sdk_found = None
        for dirpath, dirs, _files in os.walk(sdk_root):
            for d in dirs:
                if re.match(r"MacOSX\d+(\.\d+)?\.sdk$", d):
                    sdk_found = os.path.join(dirpath, d)
                    break
            if sdk_found:
                break

        if not sdk_found:
            log("ERROR: MacOSX*.sdk directory not found.  Extracted contents:")
            for dp, ds, fs in os.walk(sdk_root):
                depth = dp.replace(sdk_root, "").count(os.sep)
                if depth < 4:
                    log(f"    {'  ' * depth}{os.path.basename(dp)}/")
            sys.exit(1)

        sdk_name = os.path.basename(sdk_found)
        log(f"==> Found: {sdk_name}")

        # 7 -- Package as .tar.gz
        tarball = os.path.join(output_dir, f"{sdk_name}.tar.gz")
        log(f"==> Packaging: {sdk_name}.tar.gz ...")
        subprocess.run(
            ["tar", "-czf", tarball, "-C", os.path.dirname(sdk_found), sdk_name],
            check=True,
        )

        log(f"==> macOS SDK ready: {tarball}")
        print(tarball)

    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
