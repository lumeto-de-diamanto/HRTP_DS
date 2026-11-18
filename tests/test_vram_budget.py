"""VRAM capacity/packing audit; runs inside the existing graphics checks.

This independent allocation model follows libnds'1024 offset units, minimum
one unit per request, and block-size alignment. The C++ viewport harness also
proves the kuji budget beside the actual renderer owners, not just this model.
"""
import struct
import unittest
from pathlib import Path


class AllocationModel:
    def __init__(self, boundary):
        self.boundary = boundary
        self.blocks = []

    def allocate(self, pixels):
        size = max(self.boundary, pixels)
        for offset in range(0, 1024*self.boundary-size+1, size):
            if all(offset+size<=left or offset>=left+length for left,length in self.blocks):
                self.blocks.append((offset,size))
                return offset
        return None

    @property
    def used(self):
        return sum(size for _,size in self.blocks)


def permanent_owners(model):
    # Exact startup order, independently checked by the C++ initializer test.
    for count,size in ((28,128),(6,256),(28,512),  # Reimu's three sheets
                       (4,32),                       # shots
                       (4,128),(1,64),                # orb + indicator
                       (2,128),(1,4096),              # items + shared boss
                       (2,128),(3,1024)):             # HUD icons + HP strips
        for _ in range(count):
            assert model.allocate(size) is not None


class VramBudgetTests(unittest.TestCase):
    def test_all_simultaneous_bg_regions_are_disjoint(self):
        # Half-open byte ranges in main BG space. BG0 includes its full modal
        # pool; results' atlas/map are intentionally borrowed at another time.
        main = ((0,32+27*128), (0x4000,0x5000), (0x8000,0xf000),
                (0xf000,0xf800), (0xf800,0x10000), (0x10000,0x30000),
                (0x40000,0x60000))
        sub = ((0,0x10000),(0x10000,0x14000))
        for regions,capacity in ((main,384*1024),(sub,128*1024)):
            for i,(start,end) in enumerate(regions):
                self.assertTrue(0<=start<end<=capacity)
                for other_start,other_end in regions[i+1:]:
                    self.assertTrue(end<=other_start or start>=other_end)
        self.assertEqual(128*1024-sub[-1][1],48*1024)

    def test_current_mapping_fits_kuji_only_after_right_sizing(self):
        for lease,expected in ((4096,False),(2048,True)):
            model=AllocationModel(32);permanent_owners(model)
            self.assertEqual(len(model.blocks),79)
            self.assertEqual(model.used,27840)
            self.assertIsNotNone(model.allocate(lease))
            kuji=[model.allocate(128) for _ in range(18)]
            self.assertEqual(all(p is not None for p in kuji),expected)
            if expected:
                self.assertEqual(model.used,32192)
                self.assertEqual(32768-model.used,576)

    def test_optional_1d64_uses_existing_bank_e_not_another_bank(self):
        model=AllocationModel(64);permanent_owners(model)
        self.assertEqual(model.used,27968)  # four32-byte shots round to64
        self.assertIsNotNone(model.allocate(2048))
        for _ in range(18):
            self.assertIsNotNone(model.allocate(128))
        self.assertEqual(model.used,32320)
        self.assertEqual(65536-model.used,33216)
        # This is a proposal, not permission to map128KiB onto a64KiB bank.
        self.assertTrue(all(start%64==0 and start+size<=65536 for start,size in model.blocks))

    def test_shipped_slash_banks_and_oam_ranges(self):
        data=Path('nitrofs/konngara_slash.bin').read_bytes()
        bank0=bank1=objects=0
        for frame in range(28):
            count,_,a,b,_,offset,_,_=struct.unpack_from('<HHHHIIII',data,32+frame*24)
            bank0=max(bank0,a);bank1=max(bank1,b);objects=max(objects,count)
            for i in range(count):
                x,y,w,h,bank,start,_=struct.unpack_from('<HHHHHHI',data,offset+i*16)
                self.assertEqual(start%64,0)  # also compatible with1D-64
                self.assertLessEqual(start+w*h,(a,b)[bank])
                self.assertLessEqual(x+w,320);self.assertLessEqual(y+h,192)
        self.assertEqual(bank0,4096)
        self.assertLessEqual(bank1,2048)
        self.assertLessEqual(objects,32)
        slash=set(range(75,107));kuji=set(range(107,125))
        self.assertFalse(slash & kuji)
        self.assertEqual(len(kuji),18)
        self.assertLess(max(kuji),128)
