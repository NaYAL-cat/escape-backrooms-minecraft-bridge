"""Versioned, complete CSV records. No half-written camera or collision frames."""
import csv
import math
import os
import time
from pathlib import Path

VERSION = 2

def read_record(path: Path, max_age=1.0):
    try:
        age = time.time() - path.stat().st_mtime
        if age < -2 or age > max_age:
            return None
        text = path.read_text(encoding='utf-8')
        if len(text) > 1_048_576 or not text.endswith('\n'):
            return None
        row = next(csv.reader([text.rstrip('\r\n')]))
        if len(row) < 4 or row[0] != '2' or row[-1] != row[2]:
            return None
        return row[:-1]
    except (OSError, ValueError, StopIteration, csv.Error):
        return None

def write_record(path: Path, session, sequence, *values):
    # Lua readers use io.open and close immediately. Atomic replacement may
    # encounter an open Windows handle: skip this tick rather than disconnect.
    data = ','.join(map(str, (VERSION, session, sequence, *values, sequence))) + '\n'
    return atomic_write(path, data)

def atomic_write(path: Path, data: str):
    tmp = path.with_suffix(path.suffix + '.tmp')
    try:
        tmp.write_text(data, encoding='utf-8')
        os.replace(tmp, path)
        return True
    except PermissionError:
        return False

def finite(values):
    result = [float(x) for x in values]
    if not all(math.isfinite(x) for x in result):
        raise ValueError('non-finite coordinate')
    return result

def host_state(path):
    row = read_record(path)
    if row is None or len(row) != 16:
        return None
    try:
        vals = finite(row[5:14])
        return {'session': row[1], 'seq': int(row[2]), 'ready': row[3] == '1',
                'map': row[4], 'feet': vals[:3], 'eye': vals[3:6],
                'yaw': vals[6], 'pitch': vals[7], 'hfov': vals[8],
                'camera': row[14] == '1', 'applied': int(row[15])}
    except ValueError:
        return None

def collision_boxes(path):
    row = read_record(path)
    if row is None or (len(row)-3) % 6 or len(row) > 40000:
        return None
    try:
        vals = finite(row[3:])
        boxes = [vals[i:i+6] for i in range(0,len(vals),6)]
        if any(any(b[i] >= b[i+3] for i in range(3)) for b in boxes):
            return None
        return row[1], int(row[2]), boxes
    except ValueError:
        return None
