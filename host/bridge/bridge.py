"""ETB x Minecraft V2 coordinator. Local-only; Minecraft owns the simulation."""
from __future__ import annotations
import argparse
import ctypes
import json
import logging
import math
import time
import uuid
from dataclasses import asdict
from logging.handlers import RotatingFileHandler
from pathlib import Path
from coordinates import Transform, mc_yaw, ue_yaw, vertical_fov, horizontal_fov, scaled_view
from protocol import host_state, collision_boxes, read_record, write_record, atomic_write, finite
from input_ring import InputRing
from transport import LocalWebSocket

ROOT = Path(__file__).resolve().parents[2]

class Singleton:
    def __init__(self):
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
        self.kernel.CreateMutexW.restype = ctypes.c_void_p
        self.kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        self.handle = self.kernel.CreateMutexW(None, False, 'Local\\ETBMinecraftBridgeV2')
        if not self.handle or ctypes.get_last_error() == 183:
            raise RuntimeError('Another bridge is already running. Close it first.')
    def close(self):
        self.kernel.CloseHandle(self.handle)

class Coordinator:
    def __init__(self, game, port=25599):
        self.directory = game / 'EscapeTheBackrooms/Binaries/Win64/ue4ss/Mods/BackroomsMinecraftBridge'
        self.directory.mkdir(parents=True, exist_ok=True)
        self.runtime = ROOT / 'runtime'
        self.runtime.mkdir(exist_ok=True)
        self.transforms_path = self.runtime / 'maps-v2.json'
        self.maps = json.loads(self.transforms_path.read_text(encoding='utf-8')) if self.transforms_path.exists() else {}
        self.port = port
        self.input = InputRing()
        self.ws = None
        self.hello = False
        self.world = False
        self.aligned = False
        self.session = uuid.uuid4().hex[:16]
        self.host_key = None
        self.transform = Transform()
        self.mc_frame = None
        self.mc_at = 0.0
        self.connected_at = 0.0
        self.host = None
        self.host_at = 0.0
        self.host_seq = None
        self.collision_seq = None
        self.geometry_at = 0.0
        self.geometry_usable = False
        self.entity_seq = None
        self.seq = 0
        self.view = None
        self.last_heartbeat = self.last_diagnostic = self.last_start = 0.0
        self.damage_seq = 0
        self.damage = []
        self.state = ''
        self.last_active = False
        self.config = {'max_pixels': 1920*1080, 'mouse_scale': 1.0}
        config = ROOT/'bridge-v2.json'
        if config.exists(): self.config.update(json.loads(config.read_text(encoding='utf-8')))

    def send(self, message):
        if self.ws:
            message.setdefault('v', 2)
            message.setdefault('s', self.session)
            self.ws.send_text(json.dumps(message, separators=(',',':'), allow_nan=False))

    def disconnect(self):
        if self.ws:
            try: self.send({'t':'release'})
            except OSError: pass
            self.ws.close()
        self.ws = None
        self.hello = self.world = self.aligned = False
        self.host_key = None
        self.mc_frame = None
        self.last_active = False
        self.seq += 1
        write_record(self.directory/'render-v2.csv', self.session, self.seq, 0,0,0,0,0,0,70,0,0,0,0)

    def transform_for(self, host):
        name = host['map']
        if name not in self.maps:
            # New host maps live far apart in the persistent void overworld.
            # Anchor the first host floor to y=64 once; never recompute while walking.
            self.maps[name] = asdict(Transform(4096*len(self.maps)-host['feet'][0]/100,
                64-host['feet'][2]/100, -host['feet'][1]/100))
            atomic_write(self.transforms_path, json.dumps(self.maps, indent=2))
        return Transform(**self.maps[name])

    def receive(self, now):
        for text in self.ws.poll_text():
            m=json.loads(text)
            if m.get('t')=='hello':
                if m.get('v')!=2: raise ConnectionError('Minecraft mod protocol mismatch; install V2 jar.')
                self.hello=True
                self.send({'t':'hello'})
                continue
            if not self.hello or m.get('s')!=self.session: continue
            t=m.get('t')
            if t=='status': self.world=bool(m.get('ready'))
            elif t=='aligned': self.aligned=True
            elif t=='frame' and self.aligned:
                values=finite(m['feet']+m['eye']+[m['yaw'],m['pitch'],m['fov']])
                if len(values)!=9 or not 1<values[-1]<179: continue
                self.mc_frame=m
                self.mc_at=now
            elif t=='recovered': logging.warning('Player returned to last supported position: %s',m.get('reason'))
            elif t=='damage':
                self.damage_seq+=1
                # Cumulative short queue prevents lost hits if UE skips a file update.
                self.damage.append([self.damage_seq,int(m['id']),min(2048,max(0,float(m['amount'])))])
                self.damage=self.damage[-64:]
            elif t=='error': logging.error('Minecraft: %s',m.get('message'))

    def tick(self):
        now=time.monotonic()
        events=self.input.poll()
        self.receive(now)
        h=host_state(self.directory/'host-v2.csv')
        if h and (h['session'],h['seq'])!=self.host_seq:
            self.host,self.host_at,self.host_seq=h,now,(h['session'],h['seq'])
        h=self.host
        ready=bool(self.hello and self.world and h and h['ready'] and now-self.host_at<1.0)
        if ready:
            key=(h['session'],h['map'])
            if self.host_key!=key:
                self.host_key=key
                self.session=uuid.uuid4().hex[:16]
                self.send({'t':'hello'})
                self.transform=self.transform_for(h)
                self.aligned=False
                self.mc_frame=None
                self.collision_seq=self.entity_seq=None
                self.geometry_at=0.0
                self.geometry_usable=False
                self.damage=[]
                self.last_start=0
                logging.info('Host level %s; stable transform %s',h['map'],self.transform)
            collision=collision_boxes(self.directory/'collision-v2.csv')
            if collision and collision[0]==h['session'] and collision[:2]!=self.collision_seq:
                self.send({'t':'collision','boxes':[self.transform.box_to_mc(b) for b in collision[2]]})
                self.collision_seq=collision[:2]
                self.geometry_at=now
                self.geometry_usable=bool(collision[2])
            if not self.aligned and self.geometry_usable and now-self.last_start>1:
                self.send({'t':'start','map':h['map'],'p':self.transform.to_mc(h['feet']),
                           'yaw':mc_yaw(h['yaw']),'pitch':-h['pitch']})
                self.last_start=now
            entities=read_record(self.directory/'entities-v2.csv')
            if entities and entities[1]==h['session'] and len(entities[3:])%6==0:
                if (entities[1],entities[2])!=self.entity_seq:
                    flat=finite(entities[3:]); out=[]
                    for i in range(0,len(flat),6):
                        e=flat[i:i+6]
                        out.append([int(e[0]),*self.transform.to_mc(e[1:4]),e[4]/100,e[5]/100])
                    self.send({'t':'entities','entities':out})
                    self.entity_seq=(entities[1],entities[2])
        if self.hello and now-self.last_heartbeat>=0.1:
            geometry_ready=bool(self.mc_frame and self.mc_frame.get('full')) or (self.geometry_usable and now-self.geometry_at<.75)
            active=ready and self.aligned and geometry_ready and self.input.ready and not self.input.released and self.input.focused
            self.send({'t':'heartbeat','active':active,'render':ready and self.aligned,
                       'mouseScale':float(self.config['mouse_scale'])})
            self.last_heartbeat=now
            size=scaled_view(self.input.width,self.input.height,int(self.config['max_pixels']))
            if size!=self.view:
                self.send({'t':'view','w':size[0],'h':size[1]}); self.view=size
            if self.last_active and not active: self.send({'t':'release'})
            self.last_active=active
        if self.aligned:
            for e in events:
                if e['t']=='release' or self.input.active: self.send(e)
        self.seq+=1
        valid=ready and self.aligned and self.mc_frame and now-self.mc_at<1.0
        if valid:
            m=self.mc_frame
            full=bool(m.get('full'))
            feet=self.transform.to_ue(m['feet']); eye=self.transform.to_ue(m['eye'])
            write_record(self.directory/'drive-v2.csv',self.session,self.seq,h['session'],time.time(),
                         int(not self.input.released and self.input.ready),int(m['f']),*feet,*eye,ue_yaw(m['yaw']),-m['pitch'],
                         horizontal_fov(m['fov'],self.input.width,self.input.height),int(full))
            # This is the measured host view. Never relabel the MC frame with it.
            render=bool((h['camera'] or full) and not self.input.released)
            p=self.transform.to_mc(h['eye'])
            write_record(self.directory/'render-v2.csv',self.session,self.seq,*p,mc_yaw(h['yaw']),-h['pitch'],0,
                         vertical_fov(h['hfov'],self.input.width,self.input.height),0,int(render),int(render),int(full))
            if self.damage:
                write_record(self.directory/'damage-v2.csv',self.session,self.seq,h['session'],time.time(),
                             *[v for hit in self.damage for v in hit])
            state='RUNNING' if render else 'WAITING_FOR_HOST_CAMERA'
        else:
            write_record(self.directory/'render-v2.csv',self.session,self.seq,0,0,0,0,0,0,70,0,0,0,0)
            state='WAITING_FOR_HOST' if not ready else 'ALIGNING'
        if ready and self.aligned and not (self.mc_frame and self.mc_frame.get('full')) and (not self.geometry_usable or now-self.geometry_at>=.75): state='WAITING_FOR_COLLISION'
        if ready and not self.input.ready: state='INPUT_UNAVAILABLE'
        if self.input.released: state='RELEASED_F8'
        if state!=self.state:
            logging.info('State: %s',state); self.state=state
        if now-self.last_diagnostic>1:
            atomic_write(self.runtime/'status.json',json.dumps({'version':2,'state':state,'session':self.session,
                'map':h['map'] if h else None,'input':{'active':self.input.active,'focus':self.input.focused,'raw_ready':self.input.ready},
                'capture_lag_ms':self.mc_frame.get('captureLagMs') if self.mc_frame else None,
                'collision_usable':self.geometry_usable,
                'collision_age_ms':round((now-self.geometry_at)*1000) if self.geometry_at else None,
                'feet':self.mc_frame.get('feet') if self.mc_frame else None,
                'frame':self.mc_frame.get('f') if self.mc_frame else None,
                'frame_age_ms':round((now-self.mc_at)*1000) if self.mc_at else None,
                'host_applied_frame':h['applied'] if h else None,'updated':time.time()},indent=2))
            self.last_diagnostic=now

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir',type=Path,required=True)
    parser.add_argument('--port',type=int,default=25599)
    args=parser.parse_args()
    (ROOT/'logs').mkdir(exist_ok=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s',handlers=[
        logging.StreamHandler(),RotatingFileHandler(ROOT/'logs/bridge-v2.log',maxBytes=2_000_000,backupCount=3,encoding='utf-8')])
    singleton=Singleton()
    c=Coordinator(args.game_dir,args.port)
    try:
        logging.info('V2 ready. Start MC in PCL2, enter a Backrooms single-player level. F8 releases input.')
        while True:
            try:
                if c.ws is None:
                    c.ws=LocalWebSocket.connect('127.0.0.1',args.port)
                    c.connected_at=time.monotonic()
                    logging.info('Connected to Minecraft; waiting for version handshake.')
                c.tick()
                if not c.hello and time.monotonic()-c.connected_at>5: raise ConnectionError('No V2 handshake')
                time.sleep(0.008)
            except (OSError,ConnectionError,ValueError,KeyError) as e:
                logging.warning('%s; reconnecting',e)
                c.disconnect()
                time.sleep(1)
    except KeyboardInterrupt:
        logging.info('Stopped; releasing input.')
    finally:
        c.disconnect(); c.input.close(); singleton.close()

if __name__=='__main__': main()
