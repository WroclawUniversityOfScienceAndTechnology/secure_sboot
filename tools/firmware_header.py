"""
Create the FW header structure and provide methods to convert it to bytes and pretty-print it.
Use explicit little-endian fields so host byte order cannot change the format.
"""
from ctypes import c_uint8, c_uint16, c_uint32, memmove, LittleEndianStructure
from enum import IntEnum


class ImageType(IntEnum):
    Firmware = 0
    Bootloader = 1
    NvItems = 2


class FwHeaderConstants:
    HEADER_SIZE = 512
    IMAGE_MAGIC = 0xbeef
    GIT_SHA_SIZE = 8
    MD5_HASH_SIZE = 16


class FirmwareHeader(LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("data_length", c_uint32),
        ("data_offset", c_uint32),
        ("_md5_hash", c_uint8 * FwHeaderConstants.MD5_HASH_SIZE),
        ("image_magic", c_uint16),
        ("image_type", c_uint8),
        ("version_major", c_uint8),
        ("version_minor", c_uint8),
        ("version_patch", c_uint8),
        ("vector_address", c_uint32),
        ("reserved", c_uint32),
        ("_git_sha", c_uint8 * FwHeaderConstants.GIT_SHA_SIZE)
    ]

    def __init__(self):
        self.data_offset = FwHeaderConstants.HEADER_SIZE

    @property
    def md5_hash(self):
        return self._md5_hash

    @md5_hash.setter
    def md5_hash(self, hash: bytes):
        if len(hash) != FwHeaderConstants.MD5_HASH_SIZE:
            raise ValueError("MD5 must contain 16 bytes")
        self._md5_hash = (self._md5_hash._type_ * len(hash))()
        memmove(self._md5_hash, hash, len(hash))

    @property
    def version(self):
        return f"{self.version_major}.{self.version_minor}.{self.version_patch}"

    @version.setter
    def version(self, version_string: str):
        version = [int(v) for v in version_string.split(".")]
        if len(version) != 3 or any(not 0 <= part <= 255 for part in version):
            raise ValueError("version must have three components in 0..255")
        self.version_major, self.version_minor, self.version_patch = version

    @property
    def git_sha(self):
        return self._git_sha

    @git_sha.setter
    def git_sha(self, hash: bytes):
        if len(hash) < FwHeaderConstants.GIT_SHA_SIZE:
            raise ValueError("Git SHA must contain at least 8 bytes")
        self._git_sha = (self._git_sha._type_ * len(self._git_sha))()
        memmove(self._git_sha, hash, len(self._git_sha))

    def to_bytes(self):
        return bytes(self).ljust(self.data_offset, b"\x00")

    def pretty_str(self):
        return f"""
    Data length: {self.data_length} bytes
    Data offset: {self.data_offset} bytes
    md5 hash: {bytes(self._md5_hash).hex()}
    Image magic: 0x{self.image_magic:x}
    image type: {ImageType(self.image_type).name}
    Version: {self.version_major}.{self.version_minor}.{self.version_patch}
    Vector address: 0x{self.vector_address:04x}
    Reserved: {self.reserved}
    Git sha: {bytes(self._git_sha).hex()}"""
