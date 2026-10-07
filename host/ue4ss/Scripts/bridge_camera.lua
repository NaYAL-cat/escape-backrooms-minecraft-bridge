local U=require("UEHelpers")
local IO=require("bridge_io")
local M={active=false,applied=0}
local function valid(o) return IO.valid(o) end
function M.release()
    if M.controller and valid(M.controller) then
        pcall(function() M.controller:SetViewTargetWithBlend(valid(M.oldView) and M.oldView or M.pawn,0,0,0,false) end)
        pcall(function() M.controller:SetIgnoreLookInput(false); M.controller:SetIgnoreMoveInput(false) end)
    end
    if valid(M.pawn) then
        if valid(M.controller) then pcall(function() M.pawn:EnableInput(M.controller) end) end
        pcall(function() M.pawn:SetActorHiddenInGame(M.hidden or false); M.pawn:SetActorEnableCollision(M.collision~=false)
            if M.canBeDamaged~=nil then M.pawn.bCanBeDamaged=M.canBeDamaged end end)
        pcall(function() M.pawn.CharacterMovement:SetMovementMode(M.movement or 1,0) end)
    end
    if valid(M.camera) then pcall(function() M.camera:K2_DestroyActor() end) end
    M.active=false; M.camera=nil; M.controller=nil; M.pawn=nil; M.applied=0
end
function M.apply(controller,row)
    if row[6]~="1" then M.release(); return end
    local pawn=controller.Pawn
    if not IO.same(M.pawn,pawn) and M.active then M.release() end
    if not M.active then
        M.controller=controller; M.pawn=pawn
        M.oldView=controller:GetViewTarget()
        M.hidden=pawn.bHidden
        M.canBeDamaged=pawn.bCanBeDamaged
        M.collision=pawn:GetActorEnableCollision()
        M.movement=pawn.CharacterMovement.MovementMode
        local class=StaticFindObject("/Script/Engine.CameraActor")
        M.camera=U.GetWorld():SpawnActor(class,{}, {})
        if not valid(M.camera) then error("CameraActor spawn failed") end
        M.camera.CameraComponent.bConstrainAspectRatio=false
        controller:SetIgnoreLookInput(true); controller:SetIgnoreMoveInput(true)
        pawn:DisableInput(controller)
        pawn.CharacterMovement:StopMovementImmediately()
        pawn.CharacterMovement:SetMovementMode(0,0)
        pawn:SetActorHiddenInGame(true); pawn:SetActorEnableCollision(false)
        pawn.bCanBeDamaged=false
        controller:SetViewTargetWithBlend(M.camera,0,0,0,false)
        M.active=true
    end
    -- A native dimension is shown as a complete MC frame; keep the host pawn at its portal.
    if row[17]~="1" then
        local half=pawn.CapsuleComponent:GetScaledCapsuleHalfHeight()
        local feet={X=tonumber(row[8]),Y=tonumber(row[9]),Z=tonumber(row[10])+half}
        local eye={X=tonumber(row[11]),Y=tonumber(row[12]),Z=tonumber(row[13])}
        local rotation={Pitch=tonumber(row[15]),Yaw=tonumber(row[14]),Roll=0}
        pawn:K2_SetActorLocationAndRotation(feet,{Pitch=0,Yaw=rotation.Yaw,Roll=0},false,{},true)
        M.camera:K2_SetActorLocationAndRotation(eye,rotation,false,{},true)
        M.camera.CameraComponent:SetFieldOfView(tonumber(row[16]))
        -- Camera shake belongs to the original game. Clear it while the separate view target owns the view.
        pcall(function() controller.PlayerCameraManager:StopAllCameraShakes(true) end)
        if not IO.same(controller:GetViewTarget(),M.camera) then controller:SetViewTargetWithBlend(M.camera,0,0,0,false) end
    end
    M.applied=tonumber(row[7])
end
return M
