#!/usr/bin/env python3
"""Prepare a signed v5 SD restore directory; does not build or publish a release."""
import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import tempfile


def sign_manifest(manifest, signature, private_key, public_key):
    subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(private_key.resolve()),
                    '-out', str(signature), str(manifest)], check=True, stdin=subprocess.DEVNULL, capture_output=True)
    if not 1 <= signature.stat().st_size <= 512:
        raise ValueError('Signature exceeds firmware limit')
    subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public_key.resolve()),
                    '-signature', str(signature), str(manifest)], check=True, stdin=subprocess.DEVNULL, capture_output=True)


def prepare(source, output, target, version, private_key, public_key, minimum, maximum):
    if target not in ('MAIN', 'ZERO') or not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', version):
        raise ValueError('Invalid target or semantic version')
    if any(int(part) > 65535 for part in version.split('.')) or not 1 <= minimum <= maximum <= 255:
        raise ValueError('Version/protocol range exceeds wire format')
    if output.exists() or output.is_symlink():
        raise ValueError('Output exists; choose a new directory')
    if not source.is_file() or not 1 <= source.stat().st_size <= 0xffffffff:
        raise ValueError('Invalid firmware file size')
    with tempfile.TemporaryDirectory(prefix='.v5-restore-', dir=output.parent) as temporary:
        stage = pathlib.Path(temporary)
        image = stage / 'firmware.bin'
        digest = hashlib.sha256()
        size = 0
        with source.open('rb') as incoming, image.open('xb') as outgoing:
            while chunk := incoming.read(65536):
                if size + len(chunk) > 0xffffffff:
                    raise ValueError('Firmware grew beyond wire size')
                outgoing.write(chunk)
                digest.update(chunk)
                size += len(chunk)
            outgoing.flush()
            os.fsync(outgoing.fileno())
        if not size:
            raise ValueError('Empty firmware')
        manifest = stage / 'manifest.txt'
        manifest.write_bytes(f'MILESTONE-V5 {target} {version} {size} {digest.hexdigest()} {minimum} {maximum}\n'.encode('ascii'))
        signature = stage / 'manifest.sig'
        sign_manifest(manifest, signature, private_key, public_key)
        # Claim output atomically without overwriting any existing directory.
        # An interrupted publication is incomplete and the device rejects it.
        output.mkdir()
        for name in ('firmware.bin', 'manifest.txt', 'manifest.sig'):
            os.link(stage / name, output / name)
    return size, digest.hexdigest()


def prepare_bundle(main_source, zero_source, output, version, private_key, public_key, minimum, maximum):
    """Bind one exact MAIN/ZERO pair (or explicitly MAIN-only) before exposing output."""
    if output.exists() or output.is_symlink():
        raise ValueError('Output exists; choose a new directory')
    with tempfile.TemporaryDirectory(prefix='.v5-bundle-', dir=output.parent) as temporary:
        stage = pathlib.Path(temporary)
        _, main_sha = prepare(main_source, stage / 'main', 'MAIN', version,
                              private_key, public_key, minimum, maximum)
        zero_sha = 'NONE'
        if zero_source is not None:
            _, zero_sha = prepare(zero_source, stage / 'zero', 'ZERO', version,
                                  private_key, public_key, minimum, maximum)
        manifest = stage / 'bundle.txt'
        manifest.write_bytes(f'MILESTONE-V5 BUNDLE {version} {main_sha} {zero_sha}\n'.encode('ascii'))
        sign_manifest(manifest, stage / 'bundle.sig', private_key, public_key)
        output.mkdir()
        for target in ('main', 'zero'):
            if not (stage / target).exists():
                continue
            (output / target).mkdir()
            for name in ('firmware.bin', 'manifest.txt', 'manifest.sig'):
                os.link(stage / target / name, output / target / name)
        # Publish the signed bundle descriptor last. Partial copies fail closed.
        os.link(stage / 'bundle.sig', output / 'bundle.sig')
        os.link(manifest, output / 'bundle.txt')
    return main_sha, zero_sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--target', choices=['MAIN', 'ZERO'])
    mode.add_argument('--bundle', action='store_true', help='Source is MAIN; prepare a companion bundle')
    parser.add_argument('--zero-source', type=pathlib.Path, help='Optional ZERO BIN for --bundle; omitted means signed MAIN-only')
    parser.add_argument('--version', required=True)
    parser.add_argument('--private-key', type=pathlib.Path, required=True)
    parser.add_argument('--public-key', type=pathlib.Path, required=True)
    parser.add_argument('--min-peer', type=int, default=1)
    parser.add_argument('--max-peer', type=int, default=1)
    args = parser.parse_args()
    if args.zero_source is not None and not args.bundle:
        parser.error('--zero-source requires --bundle')
    if not shutil.which('openssl'):
        parser.error('OpenSSL is required')
    try:
        if args.bundle:
            main_sha, zero_sha = prepare_bundle(args.source, args.zero_source, args.output,
                                                args.version, args.private_key, args.public_key,
                                                args.min_peer, args.max_peer)
            print(f'BUNDLE {args.version}: MAIN {main_sha}, ZERO {zero_sha}')
        else:
            size, sha = prepare(args.source, args.output, args.target, args.version,
                                args.private_key, args.public_key, args.min_peer, args.max_peer)
            print(f'{args.target} {args.version}: {size} bytes, SHA-256 {sha}')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'SD restore preparation failed ({type(error).__name__}); no release was published.\n')
    print(f'Prepared {args.output}; this is not a published firmware release.')


if __name__ == '__main__':
    main()
