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

    def test_uniform_sources_past_first_page(self):
        for index in [0, 1, 31, 32, 63, 64, 65, 84, 91, 127]:
            for name, variant in [('MOV', 'i32'), ('FADD', 'f32')]:
                fields = {'src0': 128 + (index & 63), 'dest_register_index': 12,
                          'flow': 'none', 'fau_page_index': index // 64}
                if name == 'FADD':
                    fields.update(src1=192, dest_width_narrow='none')
                else:
                    fields.update(dest_width='none')
                old = self.old.encode(name, variant, **fields)
                result = ctypes.c_uint64()
                with self.subTest(index=index, instruction=name):
                    self.assertTrue(self.encode(old, ctypes.byref(result)))
                    fields['src0'] = 256 + index
                    fields['fau_page_index'] = 0
                    if name == 'FADD':
                        fields['src1'] = 448
                    self.assertEqual(result.value, self.new.encode(name, variant, **fields))

    def test_special_sources_keep_their_page(self):
        for page, source in itertools.product([0, 1, 3], [224, 225, 230, 231]):
            old = self.old.encode('MOV', 'i32', src0=source, dest_register_index=12,
                                  dest_width='none', flow='none', fau_page_index=page)
            result = ctypes.c_uint64()
            self.assertTrue(self.encode(old, ctypes.byref(result)))
            expected = self.new.encode('MOV', 'i32', src0=480 + (source & 31),
                                       dest_register_index=12, dest_width='none',
                                       flow='none', fau_page_index=page)
            self.assertEqual(result.value, expected)


if __name__ == '__main__':
    unittest.main()
