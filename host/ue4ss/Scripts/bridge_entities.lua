local U=require("UEHelpers")
local IO=require("bridge_io")
local M={actors={},ids={},nextId=1,lastDamage=0,session=nil}
function M.scan(player)
    local actors=FindAllOf("Pawn") or {}; local out={}; local seen={}
    local origin=player:K2_GetActorLocation()
    for _,actor in ipairs(actors) do
        if IO.valid(actor) and actor~=player then
            local ok,values=pcall(function()
                if not actor.bCanBeDamaged then return nil end
                local p=actor:K2_GetActorLocation()
                if (p.X-origin.X)^2+(p.Y-origin.Y)^2+(p.Z-origin.Z)^2>6400^2 then return nil end
                local width,height=60,190
                local good,r,h=pcall(function() return actor.CapsuleComponent:GetScaledCapsuleRadius(),actor.CapsuleComponent:GetScaledCapsuleHalfHeight() end)
                if good then width=r*2; height=h*2 end
                local name=actor:GetFullName(); local id=M.ids[name]
                if not id then id=M.nextId; M.nextId=id+1; M.ids[name]=id end
                M.actors[id]=actor; seen[id]=true
                return {id,p.X,p.Y,p.Z-height/2,width,height}
            end)
            if ok and values then for _,v in ipairs(values) do out[#out+1]=v end end
        end
    end
    for id,_ in pairs(M.actors) do if not seen[id] then M.actors[id]=nil end end
    return out
end
function M.damage(controller,row,hostSession)
    if not row or row[4]~=hostSession or not tonumber(row[5]) or math.abs(os.time()-tonumber(row[5]))>2 then return end
    if M.session~=row[2] then M.session=row[2]; M.lastDamage=0 end
    for i=6,#row,3 do
        local seq,id,amount=tonumber(row[i]),tonumber(row[i+1]),tonumber(row[i+2])
        if seq and id and amount and seq>M.lastDamage then
            local actor=M.actors[id]
            if IO.valid(actor) then
                local applied=U.GetGameplayStatics():ApplyDamage(actor,math.min(2048,math.max(0,amount)),controller,controller.Pawn,StaticFindObject("/Script/Engine.DamageType"))
                print("[ETB-MC V2] damage="..tostring(applied).." actor="..actor:GetFullName())
            end
            M.lastDamage=seq
        end
    end
end
return M
