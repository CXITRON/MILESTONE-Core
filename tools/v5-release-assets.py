#!/usr/bin/env python3
"""Internal v5 signing/verification backend for milestone-release."""
import argparse
import binascii
import hashlib
import json
import pathlib
import re
import struct
import subprocess

ASSETS = ('v5-bundle.txt', 'v5-bundle.sig', 'v5-main-manifest.txt',
          'v5-main-manifest.sig', 'v5-main.bin', 'v5-zero-manifest.txt',
          'v5-zero-manifest.sig', 'v5-zero.bin', 'v5-safe.bin',
          'v5-main-initial.bin', 'v5-zero-initial.bin')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def openssl(*args):
    return subprocess.run(['openssl', *map(str, args)], check=True,
                          stdin=subprocess.DEVNULL, capture_output=True).stdout


def validate_bins(root, version):
    for role, maximum in (('main', 0x600000), ('zero', 1966080), ('safe', 0x200000)):
        data = (root / f'v5-{role}.bin').read_bytes()
        if not data or data[0] != 0xe9 or len(data) > maximum:
            raise ValueError(f'{role}: invalid ESP application size/header')
        if f'MILESTONE_V5_{role.upper()}_RUNTIME'.encode() not in data or version.encode() + b'\0' not in data:
            raise ValueError(f'{role}: missing role/version marker')
        if b'/api/stream/start' in data or b'/stream' in data:
            raise ValueError(f'{role}: streaming implementation remains')
        if role != 'zero' and b'MILESTONE_BLE_AMS_RUNTIME_V8' in data:
            raise ValueError(f'{role}: unexpected BLE runtime')
        if role == 'zero' and b'MILESTONE_BLE_AMS_RUNTIME_V8' not in data:
            raise ValueError('ZERO is missing the AMS runtime')
    # USB image offsets are deliberate; ordinary Arduino upload would put MAIN
    # at the factory slot and overwrite SAFE. Verify the merged bytes explicitly.
    initial = (root / 'v5-main-initial.bin').read_bytes()
    ota = initial[0xe000:0x10000]
    if len(ota) != 0x2000:
        raise ValueError('Initial MAIN image is missing OTA selection data')
    sequence, stored_crc = struct.unpack_from('<I24xI', ota)
    expected_crc = binascii.crc32(ota[:4], 0xffffffff) & 0xffffffff
    if sequence != 1 or stored_crc != expected_crc:
        raise ValueError('Initial MAIN image does not select OTA slot A')
    for name, offset in (('safe', 0x10000), ('main', 0x210000)):
        data = (root / f'v5-{name}.bin').read_bytes()
        if initial[offset:offset + len(data)] != data:
            raise ValueError(f'Wrong initial MAIN {name} offset')
    zero = (root / 'v5-zero.bin').read_bytes()
    if (root / 'v5-zero-initial.bin').read_bytes()[0x10000:0x10000 + len(zero)] != zero:
        raise ValueError('Wrong initial ZERO application offset')


def verify(root, public, version):
    openssl('dgst', '-sha256', '-verify', public, '-signature', root / 'v5-catalog.sig', root / 'v5-catalog.json')
    catalog = json.loads((root / 'v5-catalog.json').read_text())
    if catalog['version'] != version or catalog['protocol'] != 1 or set(catalog['assets']) != set(ASSETS):
        raise ValueError('Catalog version/protocol/assets mismatch')
    for name in ASSETS:
        metadata = catalog['assets'][name]
        if (root / name).stat().st_size != metadata['size'] or digest(root / name) != metadata['sha256']:
            raise ValueError(f'Catalog mismatch: {name}')
    for role in ('main', 'zero'):
        manifest = root / f'v5-{role}-manifest.txt'
        openssl('dgst', '-sha256', '-verify', public, '-signature', root / f'v5-{role}-manifest.sig', manifest)
        expected = f'MILESTONE-V5 {role.upper()} {version} {(root / f"v5-{role}.bin").stat().st_size} {digest(root / f"v5-{role}.bin")} 1 1\n'
        if manifest.read_text() != expected:
            raise ValueError(f'{role} manifest mismatch')
    openssl('dgst', '-sha256', '-verify', public, '-signature', root / 'v5-bundle.sig', root / 'v5-bundle.txt')
    expected = f'MILESTONE-V5 BUNDLE {version} {digest(root / "v5-main.bin")} {digest(root / "v5-zero.bin")}\n'
    if (root / 'v5-bundle.txt').read_text() != expected:
        raise ValueError('Bundle does not bind this exact pair')
    validate_bins(root, version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('header', 'catalog', 'verify'))
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--public-key', required=True, type=pathlib.Path)
    parser.add_argument('--private-key', type=pathlib.Path)
    parser.add_argument('--version', default='5.0.0')
    args = parser.parse_args()
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', args.version):
        parser.error('Invalid version')
    if args.command == 'header':
        public = openssl('pkey', '-pubin', '-in', args.public_key, '-pubout').decode('ascii')
        if len(public) > 8192 or not re.fullmatch(r'-----BEGIN PUBLIC KEY-----\n[A-Za-z0-9+/=\n]+-----END PUBLIC KEY-----\n', public):
            raise ValueError('Invalid public key')
        with args.output.open('x') as stream:
            stream.write('#pragma once\n#define MILESTONE_V5_RELEASE_PUBLIC_KEY R"V5KEY(' + public + ')V5KEY"\n')
        return
    if args.command == 'catalog':
        if not args.private_key:
            parser.error('Catalog signing needs --private-key')
        validate_bins(args.output, args.version)
        catalog = {'version': args.version, 'protocol': 1, 'assets': {
            name: {'size': (args.output / name).stat().st_size, 'sha256': digest(args.output / name)} for name in ASSETS}}
        with (args.output / 'v5-catalog.json').open('x') as stream:
            json.dump(catalog, stream, sort_keys=True, indent=2)
            stream.write('\n')
        openssl('dgst', '-sha256', '-sign', args.private_key, '-out', args.output / 'v5-catalog.sig', args.output / 'v5-catalog.json')
    verify(args.output, args.public_key, args.version)
    print('v5 signed release catalog and all image contracts passed')


if __name__ == '__main__':
    main()
