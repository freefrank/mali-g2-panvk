import ctypes
import itertools
from pathlib import Path
import subprocess
import tempfile
import unittest

from va_v15_encode import Generator
from va_v15_isa import ISA


class EncoderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        root = Path(cls.directory.name)
        code, _ = Generator().generate()
        (root / 'encoder.c').write_text(code)
        subprocess.run(['cc', '-std=c99', '-O0', '-shared', '-fPIC',
                        str(root / 'encoder.c'), '-o', str(root / 'encoder.so')], check=True)
        cls.library = ctypes.CDLL(str(root / 'encoder.so'))
        cls.encode = cls.library.va_v15_encode_from_valhall
        cls.encode.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64)]
        cls.encode.restype = ctypes.c_bool
        cls.old = ISA(14)
        cls.new = ISA(15)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_saturated_add_with_negative_zero(self):
        old = self.old.encode('FADD', 'f32', src0=101, src1=192,
                              dest_register_index=37, dest_width_narrow='none',
                              clamp='clamp_0_1', neg1='neg', flow='none', fau_page_index=0)
        result = ctypes.c_uint64()
        self.assertTrue(self.encode(old, ctypes.byref(result)))
        expected = self.new.encode('FADD', 'f32', src0=165, src1=448,
                                   dest_register_index=37, dest_width_narrow='none',
                                   clamp='clamp_0_1', neg1='neg', flow='none', fau_page_index=0)
        self.assertEqual(result.value, expected)

    def test_add_modifiers_and_destination_widths(self):
        for variant in ['f32', 'v2f16']:
            width = 'dest_width_narrow' if variant == 'f32' else 'dest_width'
            widths = self.old.enums['dest_width_narrow_m' if variant == 'f32' else 'dest_width_m']
            for clamp, rounding, destination, source, negate in itertools.product(
                    ['none', 'clamp_0_inf', 'clamp_m1_1', 'clamp_0_1'],
                    ['none', 'round_up', 'round_down', 'round_zero'],
                    widths, [0, 37, 101, 128, 192], [False, True]):
                fields = {'src0': source, 'src1': 192, 'dest_register_index': 37,
                          width: destination, 'clamp': clamp, 'round': rounding,
                          'neg1': 'neg' if negate else 'none',
                          'flow': 'none', 'fau_page_index': 0}
                old = self.old.encode('FADD', variant, **fields)
                result = ctypes.c_uint64()
                with self.subTest(variant=variant, fields=fields):
                    self.assertTrue(self.encode(old, ctypes.byref(result)))
                    fields['src0'] = (0, 128, 256, 448)[source >> 6] + (source & 63)
                    fields['src1'] = 448
                    self.assertEqual(result.value, self.new.encode('FADD', variant, **fields))


if __name__ == '__main__':
    unittest.main()
