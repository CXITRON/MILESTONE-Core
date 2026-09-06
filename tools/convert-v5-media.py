#!/usr/bin/env python3
"""Convert local media into experimental MVJ1 SD video (no audio/network)."""
import argparse
import os
import pathlib
import struct
import subprocess
import tempfile
import zlib

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--fps', type=int, choices=range(1, 31), default=15)
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    if not args.source.is_file() or args.seconds < 1 or args.seconds > 3600:
        parser.error('Source must exist; duration must be 1..3600 seconds')
    if args.output.exists():
        parser.error('Output exists; choose another path')
    with tempfile.TemporaryDirectory(prefix='milestone-video-') as directory:
        root = pathlib.Path(directory)
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', str(args.source.resolve()),
                        '-t', str(args.seconds), '-an', '-vf',
                        f'fps={args.fps},scale=128:128:force_original_aspect_ratio=decrease,pad=128:128:(ow-iw)/2:(oh-ih)/2',
                        '-q:v', '5', str(root / '%08d.jpg')], check=True)
        frames = sorted(root.glob('*.jpg'))
        if not frames:
            raise ValueError('No frames decoded')
        # Build beside output and publish only after all frames have been checked.
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix='.mvj-', delete=False) as stream:
            staging = pathlib.Path(stream.name)
            try:
                stream.write(struct.pack('<4sHHHHI', b'MVJ1', 128, 128, args.fps, 0, len(frames)))
                for frame in frames:
                    data = frame.read_bytes()
                    if not 4 <= len(data) <= 32768 or data[:2] != b'\xff\xd8' or data[-2:] != b'\xff\xd9':
                        raise ValueError('JPEG frame exceeds 32KiB or is invalid')
                    stream.write(struct.pack('<II', len(data), zlib.crc32(data)))
                    stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
                # Hard link fails rather than overwrite if output appears meanwhile.
            except BaseException:
                staging.unlink(missing_ok=True)
                raise
        try:
            args.output.hardlink_to(staging)
        finally:
            staging.unlink(missing_ok=True)
    print(f'{len(frames)} frames at {args.fps} fps -> {args.output}')

if __name__ == '__main__':
    main()
