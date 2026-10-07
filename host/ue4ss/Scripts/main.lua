-- ETB × Minecraft V2. All UE mutations run on the game thread.
local U=require("UEHelpers")
local IO=require("bridge_io")
local Camera=require("bridge_camera")
local Collision=require("bridge_collision")
local Entities=require("bridge_entities")
local session=tostring(os.time()).."-"..tostring(math.random(100000,999999))
local seq=0; local collisionSeq=0; local entitySeq=0
local lastDrive=0; local bridgeSession=nil; local driveSeq=0
local levelKey=nil; local ticks=0; local lastError=nil
local function update()
    ticks=ticks+1
    local controller=U.GetPlayerController()
    if not IO.valid(controller) or not IO.valid(controller.Pawn) then Camera.release(); return end
    local pawn=controller.Pawn
    -- A menu/spectator pawn has no valid movement or capsule. It must never be taken over.
    local ok,ready=pcall(function() return IO.valid(pawn.CharacterMovement) and IO.valid(pawn.CapsuleComponent) end)
    if not ok or not ready then Camera.release(); return end
    local world=U.GetWorld(); local name=world:GetFullName():gsub(",","_")
    if name~=levelKey then
        Camera.release(); Collision.reset(); levelKey=name; session=tostring(os.time()).."-"..tostring(math.random(100000,999999)); driveSeq=0; bridgeSession=nil
    end
    local manager=controller.PlayerCameraManager
    if not IO.valid(manager) then Camera.release(); return end
    -- Measure the engine's view after its last camera update, before applying the next MC pose.
    local eye=manager:GetCameraLocation(); local rotation=manager:GetCameraRotation()
    local p=pawn:K2_GetActorLocation(); local half=pawn.CapsuleComponent:GetScaledCapsuleHalfHeight()
    seq=seq+1
    IO.write("host",session,seq,{1,name,p.X,p.Y,p.Z-half,eye.X,eye.Y,eye.Z,rotation.Yaw,rotation.Pitch,manager:GetFOVAngle(),Camera.active and 1 or 0,Camera.applied})
    local row=IO.read("drive")
    if row and #row==17 and row[4]==session and tonumber(row[5]) and math.abs(os.time()-tonumber(row[5]))<=2 then
        local good=true; for i=7,17 do if not IO.number(row[i]) then good=false end end
        if good and tonumber(row[16])>1 and tonumber(row[16])<179 then
            if bridgeSession~=row[2] then bridgeSession=row[2];driveSeq=0 end
            if tonumber(row[3])>driveSeq then
                Camera.apply(controller,row); driveSeq=tonumber(row[3]); lastDrive=os.time()
            end
        end
    end
    if Camera.active and os.time()-lastDrive>1 then Camera.release() end
    -- Apply the newest guest position before sampling. Each call has a strict 28-ray budget (cached foot tiles usually need fewer).
    local surfaces=Collision.sample(pawn)
    collisionSeq=collisionSeq+1
    IO.write("collision",session,collisionSeq,surfaces)
    if ticks%12==1 then
        entitySeq=entitySeq+1
        IO.write("entities",session,entitySeq,Entities.scan(pawn))
    end
    if Camera.active then Entities.damage(controller,IO.read("damage"),session) end
end
LoopInGameThreadWithDelay(16,function()
    local ok,err=pcall(update)
    if not ok then
        Camera.release()
        if tostring(err)~=lastError then lastError=tostring(err); print("[ETB-MC V2] paused: "..lastError) end
    end
end)
print("[ETB-MC V2] Waiting for a valid single-player character and fresh V2 handshake.")
