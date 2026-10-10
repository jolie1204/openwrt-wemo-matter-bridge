import importlib.util
import pathlib
import re
import stat
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('baseline', pathlib.Path(__file__).parents[1] / 'read_matter_endpoint_baseline.py')
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)


def fixture(trim=0, section_offset=0x100, load_address=0):
    """ELF32 PIE with text bytes and a writable zero tail, optionally sstripped."""
    data = bytearray(0x300)
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    shoff, shnum = (0, 0) if trim else (0x200, 3)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0, ident, 3, 8, 1, load_address + 0x100, 52, shoff, 0, 52, 32, 2, 40, shnum, 0 if trim else 2)
    struct.pack_into('<8I', data, 52, 1, 0, load_address, load_address, 0x180, 0x180, 5, 4096)
    struct.pack_into('<8I', data, 84, 1, 0x180, load_address + 0x1180, load_address + 0x1180, 0x10 - trim, 0x40, 6, 4096)
    data[0x100:0x110] = bytes(range(16))
    if not trim:
        struct.pack_into('<10I', data, 0x228, 1, 1, 6, load_address + 0x100, section_offset, 16, 0, 0, 4, 0)
        names = b'\0.text\0.shstrtab\0'
        data[0x1b0:0x1b0 + len(names)] = names
        struct.pack_into('<10I', data, 0x250, 7, 3, 0, 0, 0x1b0, len(names), 0, 0, 1, 0)
    return bytes(data[:0x190 - trim] if trim else data)


class ElfValidationTests(unittest.TestCase):
    def test_matching_and_sstrip_zero_tail(self):
        debug = fixture()
        self.assertEqual(baseline.verify_elf(debug, fixture(trim=1)), ['.text'])

    def test_changed_instructions_rejected(self):
        operating = bytearray(fixture(trim=1))
        operating[0x105] ^= 1
        with self.assertRaisesRegex(ValueError, 'LOAD bytes'):
            baseline.verify_elf(fixture(), operating)

    def test_program_address_change_rejected(self):
        with self.assertRaisesRegex(ValueError, 'header|layout'):
            baseline.verify_elf(fixture(), fixture(trim=1, load_address=4096))

    def test_nonzero_trim_rejected(self):
        debug = bytearray(fixture())
        debug[0x18f] = 1
        with self.assertRaisesRegex(ValueError, 'not zero'):
            baseline.verify_elf(debug, fixture(trim=1))

    def test_section_address_file_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError, 'file/address'):
            baseline.verify_elf(fixture(section_offset=0x101), fixture(trim=1))

    def test_truncated_and_wrong_arch_rejected(self):
        with self.assertRaises(ValueError):
            baseline.parse_elf(fixture()[:100])
        elf = bytearray(fixture()); struct.pack_into('<H', elf, 18, 62)
        with self.assertRaisesRegex(ValueError, 'MIPS'):
            baseline.parse_elf(elf)


class PrivacyAndMemoryTests(unittest.TestCase):
    def test_private_creation_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            parent = pathlib.Path(temp); parent.chmod(0o700)
            output = baseline.output_path(parent / 'result.json')
            baseline.write_private(output, {'test': True})
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
            with self.assertRaisesRegex(ValueError, 'exists'):
                baseline.output_path(output)
            with self.assertRaises(FileExistsError):
                baseline.write_private(output, {})

    def test_public_parent_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            pathlib.Path(temp).chmod(0o755)
            with self.assertRaisesRegex(ValueError, '0700'):
                baseline.output_path(pathlib.Path(temp) / 'result.json')

    def test_out_of_bounds_never_runs_remote(self):
        class Remote:
            def run(self, command):
                raise AssertionError('Must reject before SSH')
        maps = [{'start': 4096, 'end': 8192, 'permissions': 'rw-p'}]
        for ranges in ([(8190, 4)], [(4096, 8193)], [(4096, 0)]):
            with self.assertRaises(ValueError):
                baseline.read_ranges(Remote(), 10, maps, ranges)

    def test_short_read_and_binary_batch(self):
        class Remote:
            def run(self, command):
                self.command = command
                return b'abcde'
        remote = Remote()
        maps = [{'start': 4096, 'end': 8192, 'permissions': 'rw-p'}]
        self.assertEqual(baseline.read_ranges(remote, 10, maps, [(4096, 2), (4098, 3)]), [b'ab', b'cde'])
        self.assertIn('/proc/10/mem', remote.command)
        self.assertNotIn('ptrace', remote.command)
        with self.assertRaisesRegex(ValueError, 'Short'):
            baseline.read_ranges(remote, 10, maps, [(4096, 6)])

    def test_host_argument_injection_rejected(self):
        for host in ('-oProxyCommand=bad', 'name;bad', 'host\ncommand'):
            with self.assertRaisesRegex(ValueError, 'SSH host'):
                baseline.Remote(host)


class LayoutAndRaceTests(unittest.TestCase):
    def fixture(self, race=False, mismatch=False):
        # Deliberately different addresses, field offsets, record sizes and count.
        layout = {'devices_address': 0x100, 'devices_size': 8, 'endpoints_address': 0x200,
                  'endpoints_size': 40, 'endpoint_size': 20, 'object_unique': 32,
                  'object_unique_size': 8, 'object_endpoint': 10, 'object_parent': 12,
                  'endpoint_id': 2, 'endpoint_parent': 4, 'types_pointer': 8,
                  'types_count': 12, 'type_size': 12, 'type_id': 4, 'type_revision': 1}
        memory = bytearray(0x8000)
        struct.pack_into('<I', memory, 0x1100, 0x5000)
        memory[0x5020:0x5028] = b'\x01\x02\0xxxxx'
        struct.pack_into('<HH', memory, 0x500a, 77, 1)
        struct.pack_into('<HH', memory, 0x1202, 78 if mismatch else 77, 1)
        struct.pack_into('<II', memory, 0x1208, 0x6000, 1)
        struct.pack_into('<H', memory, 0x1216, 65535)
        memory[0x6001] = 4
        struct.pack_into('<I', memory, 0x6004, 0x107)
        class Remote:
            calls = 0
            def run(self, command):
                self.calls += 1
                if race and self.calls == 4:
                    struct.pack_into('<I', memory, 0x1100, 0)
                chunks = []
                for address, count in re.findall(r'skip=(\d+) count=(\d+)', command):
                    address, count = int(address), int(count)
                    chunks.append(memory[address:address + count])
                return b''.join(chunks)
        remote = Remote()
        remote.memory = memory
        return remote, layout, [{'start': 0, 'end': len(memory), 'permissions': 'rw-p'}]

    def test_relocated_layout_captured_without_name_or_heap_dump(self):
        remote, layout, maps = self.fixture()
        record = baseline.capture_sample(remote, 10, 0x1000, maps, layout)
        self.assertEqual(record['devices'], [{'slot': 0, 'unique_id_hex': '0102', 'endpoint': 77, 'parent': 1}])
        self.assertEqual(record['endpoints'][0]['device_types'], [{'id': 0x107, 'revision': 4}])
        self.assertEqual(remote.calls, 4)

    def test_pointer_change_during_capture_rejected(self):
        remote, layout, maps = self.fixture(race=True)
        with self.assertRaisesRegex(ValueError, 'pointers changed'):
            baseline.capture_sample(remote, 10, 0x1000, maps, layout)

    def test_object_and_registered_endpoint_mismatch_rejected(self):
        remote, layout, maps = self.fixture(mismatch=True)
        with self.assertRaisesRegex(ValueError, 'registration tables'):
            baseline.capture_sample(remote, 10, 0x1000, maps, layout)

    def bindings_fixture(self):
        remote, layout, maps = self.fixture()
        layout.update({'bindings_address': 0x300, 'bindings_start': 0, 'bindings_finish': 4,
                       'binding_size': 32, 'binding_device': 4, 'binding_udn_pointer': 12,
                       'binding_udn_size': 16, 'object_name': 16, 'object_name_size': 16})
        memory = remote.memory
        struct.pack_into('<II', memory, 0x1300, 0x4000, 0x4020)
        struct.pack_into('<I', memory, 0x4004, 0x5000)
        struct.pack_into('<II', memory, 0x400c, 0x7000, 14)
        memory[0x7000:0x700e] = b'UUID:fixture-a'
        memory[0x5010:0x5020] = b'fixture-name\0xxx'
        return remote, layout, maps

    def test_private_binding_joins_exact_pointer_and_canonicalizes_scheme(self):
        remote, layout, maps = self.bindings_fixture()
        capture = baseline.capture_sample(remote, 10, 0x1000, maps, layout, True)
        self.assertEqual(capture['devices'][0]['canonical_udn'], 'uuid:fixture-a')
        self.assertEqual(capture['devices'][0]['name_hex'], b'fixture-name'.hex())

    def test_binding_pointer_mismatch_is_not_resolved_by_name(self):
        remote, layout, maps = self.bindings_fixture()
        struct.pack_into('<I', remote.memory, 0x4004, 0x5100)
        with self.assertRaisesRegex(ValueError, 'registered pointer'):
            baseline.capture_sample(remote, 10, 0x1000, maps, layout, True)

    def test_oversized_binding_udn_rejected_before_string_read(self):
        remote, layout, maps = self.bindings_fixture()
        struct.pack_into('<I', remote.memory, 0x4010, 257)
        with self.assertRaisesRegex(ValueError, 'bounded UDN'):
            baseline.capture_sample(remote, 10, 0x1000, maps, layout, True)


if __name__ == '__main__':
    unittest.main()
