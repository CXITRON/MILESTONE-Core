import hashlib
import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sd_prepare', ROOT / 'tools/prepare-v5-sd-restore.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SignedSdPreparation(unittest.TestCase):
    def test_roundtrip_and_refusals(self):
        with tempfile.TemporaryDirectory(prefix='v5-signing-test-') as directory:
            root = pathlib.Path(directory)
            private = root / 'ephemeral-test-key.pem'
            public = root / 'ephemeral-test-public.pem'
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', str(private)], check=True, capture_output=True)
            subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)], check=True, capture_output=True)
            data = bytes(range(256)) * 4
            image = root / 'fixture.bin'
            image.write_bytes(data)
            output = root / 'restore'
            size, sha = module.prepare(image, output, 'ZERO', '5.0.0', private, public, 1, 1)
            self.assertEqual(size, len(data))
            self.assertEqual(sha, hashlib.sha256(data).hexdigest())
            self.assertEqual((output / 'firmware.bin').read_bytes(), data)
            self.assertEqual((output / 'manifest.txt').read_text(), f'MILESTONE-V5 ZERO 5.0.0 1024 {sha} 1 1\n')
            zero = root / 'zero.bin'
            zero.write_bytes(b'ZERO test fixture, not flashable')
            bundle = root / 'bundle'
            main_sha, zero_sha = module.prepare_bundle(image, zero, bundle, '5.0.0', private, public, 1, 1)
            self.assertEqual(main_sha, sha)
            self.assertEqual(zero_sha, hashlib.sha256(zero.read_bytes()).hexdigest())
            self.assertEqual((bundle / 'bundle.txt').read_text(), f'MILESTONE-V5 BUNDLE 5.0.0 {main_sha} {zero_sha}\n')
            self.assertEqual((bundle / 'zero/firmware.bin').read_bytes(), zero.read_bytes())
            subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public), '-signature',
                            str(bundle / 'bundle.sig'), str(bundle / 'bundle.txt')], check=True, capture_output=True)
            with self.assertRaises(ValueError):
                module.prepare_bundle(image, zero, bundle, '5.0.0', private, public, 1, 1)
            main_only = root / 'main-only'
            self.assertEqual(module.prepare_bundle(image, None, main_only, '5.0.0', private, public, 1, 1), (sha, 'NONE'))
            self.assertFalse((main_only / 'zero').exists())
            self.assertTrue((main_only / 'bundle.txt').read_text().endswith(' NONE\n'))
            # Missing companion must not publish a deceptively MAIN-only bundle.
            with self.assertRaises(ValueError):
                module.prepare_bundle(image, root / 'missing.bin', root / 'failed-bundle', '5.0.0', private, public, 1, 1)
            self.assertFalse((root / 'failed-bundle').exists())
            unrelated_private = root / 'unrelated-test-key.pem'
            unrelated_public = root / 'unrelated-test-public.pem'
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', str(unrelated_private)], check=True, capture_output=True)
            subprocess.run(['openssl', 'pkey', '-in', str(unrelated_private), '-pubout', '-out', str(unrelated_public)], check=True, capture_output=True)
            with self.assertRaises(subprocess.CalledProcessError):
                module.prepare_bundle(image, zero, root / 'wrong-key-bundle', '5.0.0', private, unrelated_public, 1, 1)
            self.assertFalse((root / 'wrong-key-bundle').exists())
            (bundle / 'bundle.txt').write_text(f'MILESTONE-V5 BUNDLE 5.0.0 {main_sha} NONE\n')
            result = subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public), '-signature',
                                     str(bundle / 'bundle.sig'), str(bundle / 'bundle.txt')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            with self.assertRaises(ValueError):
                module.prepare(image, output, 'ZERO', '5.0.0', private, public, 1, 1)
            with self.assertRaises(ValueError):
                module.prepare(image, root / 'invalid', 'ZERO', '05.0.0', private, public, 1, 1)
            (output / 'manifest.txt').write_text('tampered\n')
            result = subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public), '-signature', str(output / 'manifest.sig'), str(output / 'manifest.txt')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
