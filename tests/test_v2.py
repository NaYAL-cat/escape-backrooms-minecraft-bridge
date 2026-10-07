"""Offline regression tests. No game launch or physical input."""
import json, math, os, random, socket, struct, sys, tempfile, time, unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/bridge'))
from coordinates import Transform, mc_yaw, ue_yaw, vertical_fov, horizontal_fov, scaled_view
import protocol
from input_ring import InputRing, SIZE, CAPACITY, MAGIC
from transport import LocalWebSocket, MAX_FRAME_BYTES
from bridge import Coordinator
from lupa import LuaRuntime

class Coordinates(unittest.TestCase):
    def test_arbitrary_origin_round_trip(self):
        rng=random.Random(16)
        for _ in range(200):
            t=Transform(*[rng.uniform(-5000,5000) for _ in range(3)])
            p=[rng.uniform(-100000,100000) for _ in range(3)]
            for a,b in zip(p,t.to_ue(t.to_mc(p))): self.assertAlmostEqual(a,b,places=7)
    def test_forward_right_up_bases_are_not_mirrored(self):
        for yaw in range(-720,720,11):
            a=math.radians(yaw); b=math.radians(mc_yaw(yaw))
            ue_forward=[math.cos(a),0,math.sin(a)]
            ue_right=[-math.sin(a),0,math.cos(a)]
            mc_forward=[-math.sin(b),0,math.cos(b)]
            mc_right=[-math.cos(b),0,-math.sin(b)]
            for u,v in zip(ue_forward+ue_right,mc_forward+mc_right): self.assertAlmostEqual(u,v,places=10)
            self.assertAlmostEqual((ue_yaw(mc_yaw(yaw))-yaw)%360,0,places=8)
    def test_fov_preserves_projection(self):
        for w,h in [(1920,1080),(2560,1440),(3440,1440),(1024,768)]:
            for f in (50,70,90,120): self.assertAlmostEqual(horizontal_fov(vertical_fov(f,w,h),w,h),f,places=9)
    def test_fractional_wall_and_floor_not_rounded(self):
        box=Transform(0,64.035,0).box_to_mc([175.25,-60,-3.5,179.25,60,190])
        self.assertAlmostEqual(box[0],1.7525)
        self.assertAlmostEqual(box[3]-box[0],.04)
        self.assertAlmostEqual(box[1],64)
    def test_eye_and_feet_roundtrip_without_double_eye_offset(self):
        t=Transform(-1300,74.625,806)
        feet=[1200.25,88.5,-16];eye=[1200.25,88.5,146]
        for p in (feet,eye):
            for x,y in zip(t.to_ue(t.to_mc(p)),p): self.assertAlmostEqual(x,y,places=7)
        self.assertAlmostEqual(t.to_mc(eye)[1]-t.to_mc(feet)[1],1.62)
    def test_resolution_budget(self):
        self.assertEqual(scaled_view(1920,1080),(1920,1080))
        w,h=scaled_view(5120,1440)
        self.assertLessEqual(w*h,1920*1080)
        self.assertAlmostEqual(w/h,5120/1440,delta=.01)

class FileProtocol(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir=ROOT/'.test-tmp');self.root=Path(self.tmp.name);self.path=self.root/'state.csv'
    def tearDown(self): self.tmp.cleanup()
    def test_complete_record_round_trip(self):
        self.assertTrue(protocol.write_record(self.path,'s',3,4,5,6))
        self.assertEqual(protocol.read_record(self.path),['2','s','3','4','5','6'])
    def test_partial_wrong_version_and_mismatched_trailer(self):
        for text in ('2,s,3,4,3','2,s,3,4,2\n','1,s,3,4,3\n'):
            self.path.write_text(text,encoding='utf-8');self.assertIsNone(protocol.read_record(self.path))
    def test_stale_and_future_records_are_not_live(self):
        for age in (-5,3):
            protocol.write_record(self.path,'s',2,0)
            os.utime(self.path,(time.time()-age,time.time()-age))
            self.assertIsNone(protocol.read_record(self.path))
    def test_collision_and_nonfinite_values(self):
        protocol.write_record(self.path,'s',0,0,0,0,1,2,3)
        self.assertEqual(protocol.collision_boxes(self.path),('s',0,[[0.,0.,0.,1.,2.,3.]]))
        protocol.write_record(self.path,'s',1,0,0,0,1,'nan',3)
        self.assertIsNone(protocol.collision_boxes(self.path))
    def test_transient_replace_preserves_previous_complete_record(self):
        protocol.write_record(self.path,'s',1,10)
        with patch('protocol.os.replace',side_effect=PermissionError): self.assertFalse(protocol.write_record(self.path,'s',2,20))
        self.assertEqual(protocol.read_record(self.path)[2],'1')

class Events(unittest.TestCase):
    def setUp(self):
        self.mem=bytearray(SIZE);struct.pack_into('<IIIIqIII',self.mem,0,MAGIC,2,CAPACITY,32,0,11,1920,1080);struct.pack_into('<q',self.mem,40,123)
        self.ring=InputRing(self.mem); self.ring.poll()
    def emit(self,n,kind,a,b,mods=0,ready=True):
        struct.pack_into('<qiiiiq',self.mem,64+((n-1)%CAPACITY)*32,n*2+(not ready),kind,a,b,mods,0)
        struct.pack_into('<q',self.mem,16,n)
    def test_short_click_and_release_remain_ordered(self):
        self.emit(1,3,0,1);self.emit(2,3,0,0)
        self.assertEqual([e['b'] for e in self.ring.poll()],[1,0]);self.assertEqual(self.ring.poll(),[])
    def test_torn_event_is_not_consumed(self):
        self.emit(1,1,40,-10,ready=False);self.assertEqual(self.ring.poll(),[])
        self.emit(1,1,40,-10);self.assertEqual(self.ring.poll()[0]['a'],40)
    def test_overrun_and_restart_release_keys(self):
        self.emit(CAPACITY+1,2,87,1);self.assertEqual(self.ring.poll(),[{'t':'release'}])
        struct.pack_into('<q',self.mem,40,456);self.assertEqual(self.ring.poll(),[{'t':'release'}])
    def test_modifiers_and_f8_flags(self):
        self.emit(1,2,69,1,65);self.assertEqual(self.ring.poll()[0]['mods'],65)
        struct.pack_into('<I',self.mem,24,6);self.ring.poll();self.assertFalse(self.ring.active);self.assertTrue(self.ring.released)
    def test_invalid_magic_ignored(self):
        self.mem[:4]=bytes(4);self.assertEqual(self.ring.poll(),[{'t':'release'}]);self.assertFalse(self.ring.active);self.assertFalse(self.ring.ready)

class WebSocket(unittest.TestCase):
    def test_fragments_and_ping(self):
        a,b=socket.socketpair(); w=LocalWebSocket(a)
        try:
            b.sendall(b'\x01\x03abc\x80\x03def\x89\x01x')
            self.assertEqual(w.poll_text(),['abcdef'])
            result=b.recv(1024);self.assertEqual(result[0],0x8A);self.assertTrue(result[1]&128)
        finally: w.close();b.close()
    def test_fragment_limit_is_cumulative(self):
        a,b=socket.socketpair();w=LocalWebSocket(a)
        try:
            w.fragments=bytearray(MAX_FRAME_BYTES);w.fragment_opcode=1
            b.sendall(b'\x80\x01x')
            with self.assertRaises(ConnectionError):w.poll_text()
        finally:w.close();b.close()
    def test_close_always_closes_socket(self):
        a,b=socket.socketpair();w=LocalWebSocket(a)
        try:
            b.sendall(b'\x88\x00')
            with self.assertRaises(ConnectionError):w.poll_text()
            w.close();self.assertEqual(a.fileno(),-1)
        finally:b.close()

class LuaTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('package.path = '+json.dumps(str(ROOT/'host/ue4ss/Scripts/?.lua').replace('\\','/'))+" .. ';' .. package.path")
    def test_every_module_parses(self):
        for f in (ROOT/'host/ue4ss/Scripts').glob('*.lua'):
            self.lua.execute('assert(load(...))',f.read_text(encoding='utf-8'))
    def test_lua_writer_matches_python_protocol(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'.test-tmp') as tmp:
            root=str(Path(tmp)).replace('\\','/')+'/'
            self.lua.execute('local io=require("bridge_io"); io.root=...; io.write("host","host",9,{1,"level",100,200,0,100,200,162,0,0,90,1,7})',root)
            s=protocol.host_state(Path(tmp)/'host-v2.csv')
            self.assertEqual(s['feet'],[100,200,0]);self.assertEqual(s['eye'],[100,200,162]);self.assertEqual(s['applied'],7)
            self.lua.execute('local io=require("bridge_io"); local r=io.read("host"); assert(#r==16 and r[16]=="7")')
    def test_sampled_bounds_match_known_planes(self):
        self.lua.execute(r'''
            package.loaded.UEHelpers={GetKismetSystemLibrary=function() return {
              LineTraceSingle=function(self,pawn,a,b,channel,complex,ignore,debug,out)
                local t=2; local normal=nil
                local function plane(axis,value,n)
                  local d=b[axis]-a[axis]; if math.abs(d)<1e-8 then return end
                  local u=(value-a[axis])/d
                  if u>=0 and u<=1 and u<t then t=u; normal=n end
                end
                plane('Z',-3.5,{X=0,Y=0,Z=1}); plane('Z',280,{X=0,Y=0,Z=-1})
                plane('X',175.25,{X=-1,Y=0,Z=0})
                if t>1 then return false end
                out.ImpactPoint={X=a.X+(b.X-a.X)*t,Y=a.Y+(b.Y-a.Y)*t,Z=a.Z+(b.Z-a.Z)*t}
                out.ImpactNormal=normal; return true
              end
            } end}
            pawn={K2_GetActorLocation=function() return {X=0,Y=0,Z=84.5} end,
                  CapsuleComponent={GetScaledCapsuleHalfHeight=function() return 88 end}}
            for i=1,160 do surfaces=require('bridge_collision').sample(pawn) end
        ''')
        values=list(self.lua.globals().surfaces.values());boxes=[values[i:i+6] for i in range(0,len(values),6)]
        boxes=[[float(x) for x in b] for b in boxes]
        floors=[b for b in boxes if abs(b[5]+3.5)<1e-6];walls=[b for b in boxes if abs(b[0]-175.25)<1e-6]
        self.assertGreater(len(floors),10);self.assertGreater(len(walls),10)
        for b in floors:self.assertAlmostEqual(b[5],-3.5)
        for b in walls:self.assertLessEqual(b[3]-b[0],4.001)
    def flat_sampler(self):
        self.lua.execute(r'''
            calls=0; floorEnabled=true; floorHeight=0
            package.loaded.UEHelpers={GetKismetSystemLibrary=function() return {
              LineTraceSingle=function(self,pawn,a,b,channel,complex,ignore,debug,out)
                calls=calls+1
                if not floorEnabled or a.Z<b.Z or a.Z<floorHeight or b.Z>floorHeight then return false end
                out.ImpactPoint={X=a.X,Y=a.Y,Z=floorHeight};out.ImpactNormal={X=0,Y=0,Z=1};return true
              end
            } end}
            position={X=0,Y=0,Z=88}
            pawn={K2_GetActorLocation=function() return position end,
                  CapsuleComponent={GetScaledCapsuleHalfHeight=function() return 88 end}}
            collision=require('bridge_collision')
            function sample()
              calls=0; local values=collision.sample(pawn); assert(calls<=28,'unbounded ray burst')
              return values
            end
        ''')
    def test_moving_sampler_keeps_underfoot_tile_without_burst(self):
        self.flat_sampler()
        for tick in range(160):
            self.lua.globals().position.X=tick*12.5
            self.lua.globals().position.Y=math.sin(tick*.12)*70
            values=list(self.lua.globals().sample().values())
            boxes=[[float(v) for v in values[i:i+6]] for i in range(0,len(values),6)]
            px=self.lua.globals().position.X;py=self.lua.globals().position.Y
            for dx,dy in [(0,0),(-30,-30),(30,-30),(-30,30),(30,30)]:
                self.assertTrue(any(b[0]<=px+dx<=b[3] and b[1]<=py+dy<=b[4] and b[5]==0 for b in boxes))
            self.assertLess(len(boxes),1000)
    def test_stationary_ground_reuses_tiles_without_stopping_fresh_reports(self):
        self.flat_sampler()
        total=0
        for _ in range(40):
            self.lua.globals().sample();total+=self.lua.globals().calls
        self.assertLess(total,40*18) # Existing floor work is reused; safety snapshots still publish each update.
    def test_ground_trace_recovers_when_pawn_dropped_below_floor(self):
        self.flat_sampler();self.lua.globals().sample()
        self.lua.globals().position.Z=8 # Feet are 80 cm below the observed floor.
        values=list(self.lua.globals().sample().values())
        boxes=[[float(v) for v in values[i:i+6]] for i in range(0,len(values),6)]
        self.assertTrue(any(b[0]<=0<=b[3] and b[1]<=0<=b[4] and b[5]==0 for b in boxes))
    def test_missing_ground_does_not_invent_floor_and_map_reset_drops_cache(self):
        self.flat_sampler();self.lua.globals().sample()
        self.lua.globals().floorEnabled=False
        for _ in range(200):values=list(self.lua.globals().sample().values())
        self.assertEqual(values,[])
        self.lua.globals().floorEnabled=True;self.lua.globals().sample()
        self.lua.execute('collision.reset();floorEnabled=false')
        self.assertEqual(list(self.lua.globals().sample().values()),[])
    def test_camera_separate_and_restore(self):
        self.lua.execute(r'''
            local function object(id,t) t=t or {}; t.IsValid=function() return true end; t.GetAddress=function() return id end; return t end
            pawn=object(1,{bHidden=false,DisableInput=function() end,EnableInput=function() end,GetActorEnableCollision=function() return true end,
              CapsuleComponent={GetScaledCapsuleHalfHeight=function() return 88 end},
              CharacterMovement={MovementMode=1,StopMovementImmediately=function() end,SetMovementMode=function(self,m) self.MovementMode=m end},
              SetActorHiddenInGame=function(self,v) self.bHidden=v end,SetActorEnableCollision=function(self,v) self.collide=v end,
              K2_SetActorLocationAndRotation=function(self,p,r) self.pos=p end})
            view=object(2)
            cam=object(3,{CameraComponent={SetFieldOfView=function(self,v) self.fov=v end},
              K2_SetActorLocationAndRotation=function(self,p,r) self.pos=p;self.rotation=r end,K2_DestroyActor=function(self) self.destroyed=true end})
            pc=object(4,{Pawn=pawn,view=view,PlayerCameraManager={StopAllCameraShakes=function() end},
              GetViewTarget=function(self) return self.view end,SetViewTargetWithBlend=function(self,v) self.view=v end,
              SetIgnoreLookInput=function(self,v) self.ignoreLook=v end,SetIgnoreMoveInput=function(self,v) self.ignoreMove=v end})
            spawns=0
            package.loaded.UEHelpers={GetWorld=function() return {SpawnActor=function() spawns=spawns+1; return cam end} end}
            StaticFindObject=function() return {} end
            camera=require('bridge_camera')
            row={'2','bridge','1','host','0','1','11','100','200','300','100','200','462','90','-15','100','0'}
            camera.apply(pc,row); camera.apply(pc,row)
            assert(spawns==1 and pc.view==cam and cam.pos.Z==462 and pawn.pos.Z==388)
            assert(cam.rotation.Yaw==90 and pc.ignoreLook and not pawn.collide)
            camera.release()
            assert(pc.view==view and not pc.ignoreLook and pawn.collide and not pawn.bHidden)
            assert(pawn.CharacterMovement.MovementMode==1 and cam.destroyed)
        ''')

class CoordinatorTests(unittest.TestCase):
    def test_requires_geometry_stable_anchor_and_no_host_look(self):
        class WS:
            def __init__(self): self.sent=[];self.incoming=[]
            def poll_text(self): r=self.incoming;self.incoming=[];return r
            def send_text(self,s):self.sent.append(json.loads(s))
        class Input:
            focused=True;released=False;active=True;ready=True;width=1920;height=1080
            def poll(self):return []
        with tempfile.TemporaryDirectory(dir=ROOT/'.test-tmp') as tmp:
            with patch('bridge.ROOT',Path(tmp)):
                c=Coordinator(Path(tmp)/'game');c.ws=WS();c.input=Input();c.hello=c.world=True;d=c.directory
                protocol.write_record(d/'host-v2.csv','host',1,1,'level',0,0,0,0,0,162,0,0,90,0,0)
                c.tick();self.assertNotIn('start',[m['t'] for m in c.ws.sent])
                protocol.write_record(d/'collision-v2.csv','host',1,-50,-50,-10,50,50,0)
                c.tick();self.assertIn('start',[m['t'] for m in c.ws.sent])
                self.assertNotIn('look',[m['t'] for m in c.ws.sent]);self.assertNotIn('cam',[m['t'] for m in c.ws.sent])
                origin=dict(c.maps)
                protocol.write_record(d/'host-v2.csv','host',2,1,'level',200,0,20,200,0,182,20,0,90,0,0)
                c.tick();self.assertEqual(c.maps,origin)
                c.aligned=True;c.mc_frame={'t':'frame','f':7,'feet':[0,64,0],'eye':[0,65.62,0],'yaw':270,'pitch':0,'fov':70}
                c.mc_at=time.monotonic();c.tick();row=protocol.read_record(d/'drive-v2.csv')
                self.assertAlmostEqual(float(row[9]),0);self.assertAlmostEqual(float(row[12]),162)
                protocol.write_record(d/'collision-v2.csv','host',2)
                c.last_heartbeat=0;c.tick()
                self.assertFalse(c.geometry_usable)
                self.assertEqual(c.state,'WAITING_FOR_COLLISION')
                self.assertEqual([m for m in c.ws.sent if m['t']=='collision'][-1]['boxes'],[])
                self.assertFalse([m for m in c.ws.sent if m['t']=='heartbeat'][-1]['active'])
                protocol.write_record(d/'collision-v2.csv','host',3,-50,-50,-10,50,50,0)
                c.last_heartbeat=0;c.tick()
                self.assertTrue(c.geometry_usable)
                self.assertTrue([m for m in c.ws.sent if m['t']=='heartbeat'][-1]['active'])
                c.mc_frame['full']=True
                protocol.write_record(d/'collision-v2.csv','host',4)
                c.last_heartbeat=0;c.tick()
                self.assertTrue([m for m in c.ws.sent if m['t']=='heartbeat'][-1]['active'])
                c.mc_frame['full']=False
                c.input.released=True;c.tick();row=protocol.read_record(d/'drive-v2.csv');self.assertEqual(row[5],'0')
                self.assertEqual(c.state,'RELEASED_F8')

if __name__=='__main__':
    (ROOT/'.test-tmp').mkdir(exist_ok=True)
    unittest.main(verbosity=2)
