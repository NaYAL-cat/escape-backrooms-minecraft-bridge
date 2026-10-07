"""Read raw host-window events; never derive mouse input from camera rotations."""
import mmap
import struct

CAPACITY = 2048
SIZE = 64 + CAPACITY * 32
MAGIC = 0x32494245

class InputRing:
    def __init__(self, buffer=None):
        self.buffer = buffer
        self.last = 0
        self.boot = None
        self.active = False
        self.focused = False
        self.released = False
        self.ready = False
        self.width, self.height = 1920, 1080

    def poll(self):
        if self.buffer is None:
            self.buffer = mmap.mmap(-1, SIZE, tagname=r'Local\ETBBridgeInputV2', access=mmap.ACCESS_READ)
        b = self.buffer
        magic, version, count, stride = struct.unpack_from('<IIII', b)
        if (magic,version,count,stride) != (MAGIC,2,CAPACITY,32):
            was_active=self.active
            self.active=self.focused=self.ready=False
            return [{'t':'release'}] if was_active else []
        published, flags, w, h = struct.unpack_from('<qIII', b, 16)
        boot, = struct.unpack_from('<q', b, 40)
        self.active, self.focused, self.released = bool(flags&1), bool(flags&2), bool(flags&4)
        self.ready=bool(flags&8)
        if w >= 320 and h >= 200:
            self.width, self.height = w, h
        if self.boot != boot:
            self.boot, self.last = boot, published
            return [{'t':'release'}]
        if published < self.last or published-self.last > CAPACITY:
            self.last = published
            return [{'t':'release'}]
        out=[]
        for serial in range(self.last+1, published+1):
            offset=64+((serial-1)%CAPACITY)*32
            stamp,kind,a,bb,mods,_=struct.unpack_from('<qiiiiq',b,offset)
            if stamp != serial*2 or struct.unpack_from('<q',b,offset)[0] != stamp:
                break
            out.append({'t':'raw','kind':kind,'a':a,'b':bb,'mods':mods})
            self.last=serial
        return out

    def close(self):
        if hasattr(self.buffer,'close'):
            self.buffer.close()
        self.buffer=None
