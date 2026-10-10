#!/usr/bin/env python3
"""Private endpoint evidence via strict SSH and read-only /proc/PID/mem.

No extra Python dependencies. `manifest` uses a matching cross GDB's DWARF.
`capture` never attaches, stops a process, reads fabric storage, or prints IDs.
Use separate output files for preflash and postflash evidence. Output must be
outside Git in a private directory; files are created exclusively with 0600.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import stat
import struct
import subprocess
import sys

EXE = '/usr/sbin/wemo-bridge-app'
MAX_ELF = 128 * 1024 * 1024
MAX_READ = 8192


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse_elf(data):
    require(len(data) >= 52 and data[:7] == b'\x7fELF\x01\x01\x01', 'ELF32 little-endian required')
    hdr = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    require(hdr[1:3] == (3, 8), 'MIPS PIE ELF required')
    phoff, shoff, phsize, phnum, shsize, shnum, shnames = hdr[5], hdr[6], hdr[9], hdr[10], hdr[11], hdr[12], hdr[13]
    require(phsize == 32 and 0 < phnum <= 64 and phoff >= 52, 'Invalid program headers')
    require(phoff + phsize * phnum <= len(data), 'Truncated program headers')
    programs = [struct.unpack_from('<8I', data, phoff + i * phsize) for i in range(phnum)]
    for p in programs:
        require(p[1] + p[4] <= len(data), 'Program segment outside ELF')
        if p[0] == 1:
            require(p[4] <= p[5] and p[2] + p[5] <= 2**32, 'Invalid LOAD bounds')
    sections = []
    if shnum:
        require(shsize == 40 and shnum <= 4096 and shnames < shnum, 'Invalid section headers')
        require(shoff + shsize * shnum <= len(data), 'Truncated section headers')
        entries = [struct.unpack_from('<10I', data, shoff + i * shsize) for i in range(shnum)]
        names = entries[shnames]
        require(names[4] + names[5] <= len(data), 'Invalid section strings')
        names = data[names[4]:names[4] + names[5]]
        for e in entries:
            require(e[0] < len(names) and b'\0' in names[e[0]:], 'Invalid section name')
            if e[1] != 8:
                require(e[4] + e[5] <= len(data), 'Section outside ELF')
            sections.append((names[e[0]:].split(b'\0', 1)[0].decode('ascii'), e))
    return {'header': hdr, 'programs': programs, 'sections': sections, 'header_end': phoff + phsize * phnum}


def verify_elf(debug, operating):
    """Validate layout and every loadable byte, including allowed sstrip changes."""
    d, o = parse_elf(debug), parse_elf(operating)
    # sstrip removes section-table fields, and can trim trailing zero bytes of LOAD.
    for i in (1, 2, 3, 4, 5, 7, 8, 9, 10):
        require(d['header'][i] == o['header'][i], 'ELF header mismatch')
    require(len(d['programs']) == len(o['programs']), 'Program header count mismatch')
    loads = []
    for dp, op in zip(d['programs'], o['programs']):
        require(all(dp[i] == op[i] for i in (0, 1, 2, 3, 5, 6, 7)), 'Program header layout mismatch')
        if dp[0] != 1:
            require(dp[4] == op[4], 'Non-LOAD program size mismatch')
            continue
        require(0 <= dp[4] - op[4] <= 4096, 'Unexpected LOAD trimming')
        require(not any(debug[dp[1] + op[4]:dp[1] + dp[4]]), 'Trimmed LOAD bytes are not zero')
        start = max(dp[1], d['header_end'])
        require(debug[start:dp[1] + op[4]] == operating[start:op[1] + op[4]], 'LOAD bytes mismatch')
        loads.append(dp)
    allocated = []
    require(d['sections'], 'Debug ELF needs section headers')
    for name, s in d['sections']:
        if not s[2] & 2:
            continue
        require(any(p[2] <= s[3] and s[3] + s[5] <= p[2] + p[5] for p in loads), 'Allocated section outside LOAD')
        if s[1] != 8:
            require(any(p[1] <= s[4] and s[4] + s[5] <= p[1] + p[4] and s[3] - p[2] == s[4] - p[1] for p in loads), 'Section file/address mismatch')
            actual = operating[s[4]:s[4] + s[5]]
            require(debug[s[4]:s[4] + s[5]] == actual + bytes(s[5] - len(actual)), 'Allocated section bytes mismatch')
        allocated.append(name)
    return allocated


def output_path(path):
    path = pathlib.Path(path).absolute()
    require(not path.exists() and not path.is_symlink(), 'Output already exists; use a fresh path')
    parent = path.parent
    if not parent.exists():
        parent.mkdir(mode=0o700, parents=True)
    require(parent.is_dir() and not parent.is_symlink(), 'Output parent must be a directory')
    require(stat.S_IMODE(parent.stat().st_mode) == 0o700 and parent.stat().st_uid == os.getuid(), 'Output parent must be owned by this user with mode 0700')
    # Protect ignored private evidence from accidental Git staging.
    repo = subprocess.run(['git', '-C', str(parent), 'rev-parse', '--show-toplevel'], capture_output=True)
    if repo.returncode == 0:
        ignored = subprocess.run(['git', '-C', str(parent), 'check-ignore', '-q', str(path)], capture_output=True)
        require(ignored.returncode == 0, 'Output inside Git must be ignored')
    return path


def write_private(path, record):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as f:
        json.dump(record, f, indent=2)
        f.write('\n')


def generate_manifest(args):
    path = output_path(args.output)
    data = pathlib.Path(args.debug_elf).read_bytes()
    parse_elf(data)
    expressions = {
        'pointer_size': 'sizeof(void*)',
        'devices_address': "(unsigned long)&'(anonymous namespace)::gDevices'",
        'devices_size': "sizeof('(anonymous namespace)::gDevices')",
        'endpoints_address': '(unsigned long)&emAfEndpoints',
        'endpoints_size': 'sizeof(emAfEndpoints)',
        'endpoint_size': 'sizeof(EmberAfDefinedEndpoint)',
        'object_size': 'sizeof(Device)',
        'object_unique': '(unsigned long)&((Device*)0)->mUniqueId',
        'object_unique_size': 'sizeof(((Device*)0)->mUniqueId)',
        'object_endpoint': '(unsigned long)&((Device*)0)->mEndpointId',
        'object_parent': '(unsigned long)&((Device*)0)->mParentEndpointId',
        'endpoint_id': '(unsigned long)&((EmberAfDefinedEndpoint*)0)->endpoint',
        'endpoint_parent': '(unsigned long)&((EmberAfDefinedEndpoint*)0)->parentEndpointId',
        'types_pointer': '(unsigned long)&((EmberAfDefinedEndpoint*)0)->deviceTypeList.mDataBuf',
        'types_count': '(unsigned long)&((EmberAfDefinedEndpoint*)0)->deviceTypeList.mDataLen',
        'type_size': 'sizeof(EmberAfDeviceType)',
        'type_id': '(unsigned long)&((EmberAfDeviceType*)0)->deviceTypeId',
        'type_revision': '(unsigned long)&((EmberAfDeviceType*)0)->deviceTypeRevision',
    }
    if args.include_bindings:
        slot = "'(anonymous namespace)::BridgedWemoLight'"
        vector = "'(anonymous namespace)::gBridgedWemoLights'"
        expressions.update({
            'bindings_address': f'(unsigned long)&{vector}',
            'bindings_size': f'sizeof({vector})',
            'bindings_start': f'(unsigned long)&({vector}._M_impl._M_start) - (unsigned long)&{vector}',
            'bindings_finish': f'(unsigned long)&({vector}._M_impl._M_finish) - (unsigned long)&{vector}',
            'binding_size': f'sizeof({slot})',
            'binding_device': f'(unsigned long)&(({slot}*)0)->device',
            'binding_device_size': f'sizeof((({slot}*)0)->device)',
            'binding_udn_pointer': f'(unsigned long)&(({slot}*)0)->udn._M_dataplus._M_p',
            'binding_udn_size': f'(unsigned long)&(({slot}*)0)->udn._M_string_length',
            'object_name': '(unsigned long)&((Device*)0)->mName',
            'object_name_size': 'sizeof(((Device*)0)->mName)',
        })
    command = [args.gdb, '-nx', '-batch', '-iex', 'set auto-load off', args.debug_elf]
    for expression in expressions.values():
        command.extend(['-ex', 'p/u ' + expression])
    proc = subprocess.run(command, capture_output=True, timeout=60)
    require(proc.returncode == 0, 'GDB DWARF manifest generation failed')
    values = re.findall(rb'^\$\d+ = (\d+)\s*$', proc.stdout, re.M)
    require(len(values) == len(expressions), 'GDB did not resolve every DWARF expression')
    manifest = {'schema': 1, 'debug_sha256': sha(data), 'operating_sha256': args.operating_sha256, 'layout': dict(zip(expressions, map(int, values)))}
    validate_manifest(manifest, data)
    write_private(path, manifest)
    print(json.dumps({'manifest_created': True, 'layout_fields': len(values)}))


def validate_manifest(manifest, debug):
    require(manifest['schema'] == 1, 'Unsupported manifest schema')
    for key in ('debug_sha256', 'operating_sha256'):
        require(re.fullmatch('[0-9a-f]{64}', manifest[key]) is not None, 'Invalid manifest SHA256')
    require(manifest['debug_sha256'] == sha(debug), 'Debug ELF hash mismatch')
    m = manifest['layout']
    require(all(type(v) is int and 0 <= v < 2**32 for v in m.values()), 'Invalid layout values')
    require(m['pointer_size'] == 4 and 0 < m['devices_size'] <= 1024 and m['devices_size'] % 4 == 0, 'Invalid pointer table layout')
    require(0 < m['endpoint_size'] <= 256 and 0 < m['endpoints_size'] <= MAX_READ and m['endpoints_size'] % m['endpoint_size'] == 0, 'Invalid endpoint table layout')
    require(0 < m['object_unique_size'] <= 64 and 0 < m['object_size'] <= 1024 and 0 < m['type_size'] <= 64, 'Invalid object/type layout')
    for field, width in (('object_unique', m['object_unique_size']), ('object_endpoint', 2), ('object_parent', 2)):
        require(m[field] + width <= m['object_size'], 'Object field outside layout')
    for field, width in (('endpoint_id', 2), ('endpoint_parent', 2), ('types_pointer', 4), ('types_count', 4)):
        require(m[field] + width <= m['endpoint_size'], 'Endpoint field outside layout')
    require(m['type_id'] + 4 <= m['type_size'] and m['type_revision'] + 1 <= m['type_size'], 'Type field outside layout')
    elf = parse_elf(debug)
    for address, size in ((m['devices_address'], m['devices_size']), (m['endpoints_address'], m['endpoints_size'])):
        require(any(p[0] == 1 and p[6] & 2 and p[2] <= address and address + size <= p[2] + p[5] for p in elf['programs']), 'Table outside writable ELF LOAD')
    if 'bindings_address' in m:
        require(m['binding_device_size'] == 4 and 0 < m['bindings_size'] <= 64 and 0 < m['binding_size'] <= 1024, 'Unsupported binding pointer/vector layout')
        require(0 < m['object_name_size'] <= 64 and m['object_name'] + m['object_name_size'] <= m['object_size'], 'Invalid bounded name layout')
        for field in ('bindings_start', 'bindings_finish'):
            require(m[field] + 4 <= m['bindings_size'], 'Vector field outside layout')
        for field in ('binding_device', 'binding_udn_pointer', 'binding_udn_size'):
            require(m[field] + 4 <= m['binding_size'], 'Binding field outside layout')
        require(any(p[0] == 1 and p[6] & 2 and p[2] <= m['bindings_address'] and m['bindings_address'] + m['bindings_size'] <= p[2] + p[5] for p in elf['programs']), 'Binding vector outside writable ELF LOAD')
    return m


class Remote:
    def __init__(self, host):
        require(re.fullmatch(r'[A-Za-z0-9_.@:-]+', host) is not None and not host.startswith('-'), 'Invalid SSH host')
        self.command = ['ssh', '-o', 'StrictHostKeyChecking=yes', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', host]

    def run(self, command):
        proc = subprocess.run(self.command + [command], capture_output=True, timeout=60)
        require(proc.returncode == 0, 'Read-only SSH command failed; no attach or fallback attempted')
        return proc.stdout

    def pid(self):
        result = self.run('pidof wemo-bridge-app').strip()
        require(re.fullmatch(rb'[0-9]+', result) is not None, 'Expected one Matter app PID')
        return int(result)


def parse_maps(text):
    maps = []
    for line in text.splitlines():
        match = re.fullmatch(r'([0-9a-f]+)-([0-9a-f]+) ([rwxps-]{4}) ([0-9a-f]+) \S+ \d+(?:\s+(.*))?', line)
        require(match is not None, 'Invalid process map')
        maps.append({'start': int(match[1], 16), 'end': int(match[2], 16), 'permissions': match[3], 'offset': int(match[4], 16), 'path': match[5] or ''})
    return maps


def read_ranges(remote, pid, maps, ranges):
    require(sum(size for _, size in ranges) <= MAX_READ, 'Memory read exceeds minimal range limit')
    for address, size in ranges:
        require(0 < size <= MAX_READ and address + size < 2**32, 'Invalid memory read range')
        require(any(x['permissions'].startswith('r') and x['start'] <= address and address + size <= x['end'] for x in maps), 'Memory read outside readable map')
    if not ranges:
        return []
    # All addresses/counts are validated integers; stdout is binary, never a terminal.
    command = 'set -e\n' + '\n'.join(f'dd if=/proc/{pid}/mem bs=1 skip={addr} count={size} 2>/dev/null' for addr, size in ranges)
    data = remote.run(command)
    require(len(data) == sum(size for _, size in ranges), 'Short process-memory read')
    blocks, offset = [], 0
    for _, size in ranges:
        blocks.append(data[offset:offset + size]); offset += size
    return blocks


def capture_bindings(remote, pid, base, maps, m, active):
    header_ranges = [(base + m['bindings_address'] + m[field], 4) for field in ('bindings_start', 'bindings_finish')]
    header = read_ranges(remote, pid, maps, header_ranges)
    start, finish = [int.from_bytes(x, 'little') for x in header]
    require(finish >= start and (finish - start) % m['binding_size'] == 0, 'Invalid binding vector bounds')
    count = (finish - start) // m['binding_size']
    require(count <= m['devices_size'] // 4, 'Binding vector exceeds endpoint capacity')
    ranges = []
    for i in range(count):
        ranges.extend([(start + i * m['binding_size'] + m[field], 4) for field in ('binding_device', 'binding_udn_pointer', 'binding_udn_size')])
    blocks = read_ranges(remote, pid, maps, ranges)
    entries, udn_ranges = [], []
    for i in range(count):
        device, pointer, size = [int.from_bytes(x, 'little') for x in blocks[i * 3:i * 3 + 3]]
        if not device:
            continue
        require(device in active.values(), 'Binding device does not match registered pointer')
        require(6 <= size <= 256, 'Invalid bounded UDN string length')
        entries.append(device); udn_ranges.append((pointer, size))
    require(len(entries) == len(active) and len(set(entries)) == len(entries), 'Binding-to-device pointer join incomplete')
    bindings = {}
    for device, data in zip(entries, read_ranges(remote, pid, maps, udn_ranges)):
        require(all(33 <= c <= 126 for c in data) and data[:5].lower() == b'uuid:', 'Invalid canonical UDN')
        bindings[device] = 'uuid:' + data[5:].decode('ascii')
    require(len(set(bindings.values())) == len(bindings), 'Duplicate canonical UDN binding')
    require(header == read_ranges(remote, pid, maps, header_ranges), 'Binding vector changed during capture')
    return bindings


def capture_sample(remote, pid, base, maps, m, include_bindings=False):
    pointers = read_ranges(remote, pid, maps, [(base + m['devices_address'], m['devices_size'])])[0]
    active = [(i // 4, struct.unpack_from('<I', pointers, i)[0]) for i in range(0, len(pointers), 4) if struct.unpack_from('<I', pointers, i)[0]]
    ranges = [(base + m['endpoints_address'], m['endpoints_size'])]
    for _, address in active:
        ranges.extend([(address + m['object_unique'], m['object_unique_size']), (address + m['object_endpoint'], 2), (address + m['object_parent'], 2)])
    blocks = read_ranges(remote, pid, maps, ranges)
    table = blocks[0]
    devices = []
    for i, (slot, _) in enumerate(active):
        unique, endpoint, parent = blocks[1 + i * 3:4 + i * 3]
        require(b'\0' in unique, 'UniqueID not terminated')
        devices.append({'slot': slot, 'unique_id_hex': unique.split(b'\0', 1)[0].hex(), 'endpoint': int.from_bytes(endpoint, 'little'), 'parent': int.from_bytes(parent, 'little')})
    if include_bindings:
        require('bindings_address' in m, 'Manifest lacks requested binding DWARF layout')
        bindings = capture_bindings(remote, pid, base, maps, m, dict(active))
        names = read_ranges(remote, pid, maps, [(address + m['object_name'], m['object_name_size']) for _, address in active])
        for device, (_, address), name in zip(devices, active, names):
            require(b'\0' in name, 'Name not terminated')
            device['canonical_udn'] = bindings[address]
            device['name_hex'] = name.split(b'\0', 1)[0].hex()
    endpoints, type_ranges = [], []
    for offset in range(0, len(table), m['endpoint_size']):
        entry = table[offset:offset + m['endpoint_size']]
        endpoint = struct.unpack_from('<H', entry, m['endpoint_id'])[0]
        if endpoint == 65535:
            continue
        pointer = struct.unpack_from('<I', entry, m['types_pointer'])[0]
        count = struct.unpack_from('<I', entry, m['types_count'])[0]
        require(0 < count <= 8, 'Unexpected device type count')
        type_ranges.append((pointer, count * m['type_size']))
        endpoints.append({'endpoint': endpoint, 'parent': struct.unpack_from('<H', entry, m['endpoint_parent'])[0]})
    for endpoint, types in zip(endpoints, read_ranges(remote, pid, maps, type_ranges)):
        endpoint['device_types'] = [{'id': struct.unpack_from('<I', types, offset + m['type_id'])[0], 'revision': types[offset + m['type_revision']]} for offset in range(0, len(types), m['type_size'])]
    require(pointers == read_ranges(remote, pid, maps, [(base + m['devices_address'], m['devices_size'])])[0], 'Device pointers changed during capture')
    require(len({d['endpoint'] for d in devices}) == len(devices), 'Duplicate dynamic endpoint')
    require(all(any(d['endpoint'] == e['endpoint'] and d['parent'] == e['parent'] for e in endpoints) for d in devices), 'Object and registration tables differ')
    return {'devices': devices, 'endpoints': endpoints}


def capture(args):
    path = output_path(args.output)
    debug = pathlib.Path(args.debug_elf).read_bytes()
    manifest = json.loads(pathlib.Path(args.manifest).read_text())
    m = validate_manifest(manifest, debug)
    remote = Remote(args.host)
    require(remote.run('id -u').strip() == b'0', 'Read-only capture requires existing root SSH access')
    pid = remote.pid()
    # Bind ELF verification to this process, even if the on-disk file changed.
    operating = remote.run(f'cat /proc/{pid}/exe')
    require(len(operating) <= MAX_ELF and sha(operating) == manifest['operating_sha256'], 'Operating ELF hash mismatch')
    sections = verify_elf(debug, operating)
    maps = parse_maps(remote.run(f'cat /proc/{pid}/maps').decode('ascii'))
    bases = [x['start'] for x in maps if x['path'] == EXE and x['offset'] == 0]
    require(len(bases) == 1, 'Could not resolve executable load base')
    base = bases[0]
    # Verify file-backed LOAD placement against process mappings, not just ASLR base.
    for p in parse_elf(operating)['programs']:
        if p[0] == 1:
            start = (base + p[2]) & ~4095
            offset = p[1] & ~4095
            require(any(x['path'] == EXE and x['start'] <= start < x['end'] and x['offset'] + start - x['start'] == offset for x in maps), 'LOAD mapping mismatch')
    first = capture_sample(remote, pid, base, maps, m, args.include_bindings)
    second = capture_sample(remote, pid, base, maps, m, args.include_bindings)
    require(first == second, 'Endpoint snapshots changed; repeat when stable')
    require(remote.pid() == pid, 'PID changed during capture')
    record = {'schema': 1, 'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'operating_sha256': manifest['operating_sha256'], 'debug_sha256': manifest['debug_sha256'], 'manifest': manifest, 'pid': pid, 'verified_allocated_sections': sections, 'stable_samples': 2, 'capture': first, 'limitations': 'Server memory baseline, not commissioned Descriptor PartsList read or Google Home cache acknowledgement; process was not halted.'}
    write_private(path, record)
    print(json.dumps({'stable_samples': 2, 'device_count': len(first['devices']), 'endpoint_count': len(first['endpoints']), 'allocated_sections_verified': len(sections)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    make = sub.add_parser('manifest', help='Resolve symbols and offsets from matching ELF DWARF')
    make.add_argument('--debug-elf', required=True)
    make.add_argument('--gdb', required=True)
    make.add_argument('--operating-sha256', required=True)
    make.add_argument('--output', required=True)
    make.add_argument('--include-bindings', action='store_true', help='Resolve exact UDN/device-pointer join and bounded private name layout')
    read = sub.add_parser('capture', help='Strict SSH read-only capture to a fresh private file')
    read.add_argument('--host', required=True)
    read.add_argument('--debug-elf', required=True)
    read.add_argument('--manifest', required=True)
    read.add_argument('--output', required=True)
    read.add_argument('--include-bindings', action='store_true', help='Record exact UDN/device-pointer bindings and bounded private names for migration')
    args = parser.parse_args()
    os.umask(0o077)
    try:
        (generate_manifest if args.mode == 'manifest' else capture)(args)
    except (ValueError, KeyError, OSError, subprocess.TimeoutExpired) as error:
        # Do not include captured values, remote stderr, or raw binary in errors.
        print('Baseline capture failed: ' + (str(error) if isinstance(error, ValueError) else type(error).__name__), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
