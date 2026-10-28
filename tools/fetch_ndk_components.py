import io
import json
from pathlib import Path
import struct
import urllib.request
import zipfile
import hashlib


URL = 'https://dl.google.com/android/repository/android-ndk-r29-linux.zip'
DESTINATION = Path(__file__).resolve().parent.parent / 'toolchain' / 'ndk-r29-components'


class RemoteZip(io.RawIOBase):
    def __init__(self, url):
        self.url = url
        with urllib.request.urlopen(urllib.request.Request(url, method='HEAD'), timeout=30) as r:
            self.size = int(r.headers['Content-Length'])
        self.position = 0
        self.blocks = []
        self.downloaded = 0

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=0):
        self.position = offset + (self.position if whence == 1 else self.size if whence == 2 else 0)
        return self.position

    def fetch(self, start, end):
        request = urllib.request.Request(self.url, headers={'Range': f'bytes={start}-{end - 1}'})
        with urllib.request.urlopen(request, timeout=60) as response:
            if response.status != 206:
                raise ValueError(('server did not honor range', response.status))
            expected = f'bytes {start}-{end - 1}/{self.size}'
            if response.headers.get('Content-Range') != expected:
                raise ValueError(('unexpected range', response.headers.get('Content-Range')))
            data = response.read()
        if len(data) != end - start:
            raise ValueError('short HTTP range')
        self.blocks.append((start, data))
        self.downloaded += len(data)

    def read(self, count=-1):
        if count < 0:
            count = self.size - self.position
        count = min(count, self.size - self.position)
        start, end = self.position, self.position + count
        for base, block in reversed(self.blocks):
            if base <= start and end <= base + len(block):
                self.position = end
                return block[start - base:end - base]
        self.fetch(start, end)
        return self.read(count)


def main():
    remote = RemoteZip(URL)
    manifest = []
    with zipfile.ZipFile(remote) as archive:
        candidates = [f for f in archive.infolist()
                      if '/sysroot/usr/lib/aarch64-linux-android/' in f.filename
                      and f.filename.rsplit('/', 1)[-1] in
                      ('libc++_static.a', 'libc++abi.a', 'libunwind.a')]
        if not candidates:
            raise ValueError('no ARM64 static runtimes in NDK archive')
        for member in candidates:
            relative = Path(member.filename.split('/sysroot/', 1)[1])
            target = DESTINATION / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            data = archive.read(member)
            target.write_bytes(data)
            manifest.append({'member': member.filename, 'size': len(data),
                             'zip_crc32': f'{member.CRC:08x}',
                             'sha256': hashlib.sha256(data).hexdigest()})
            print(member.filename, len(data), flush=True)
    (DESTINATION / 'manifest.json').write_text(json.dumps(
        {'url': URL, 'archive_bytes': remote.size, 'downloaded_bytes': remote.downloaded,
         'verification': 'HTTPS and per-member ZIP CRC32; full archive checksum not checked',
         'files': manifest}, indent=2) + '\n')
    print('Total download', remote.downloaded, flush=True)


if __name__ == '__main__':
    main()
