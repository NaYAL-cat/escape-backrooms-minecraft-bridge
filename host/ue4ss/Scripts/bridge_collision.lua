local U=require("UEHelpers")
local IO=require("bridge_io")
local M={}
local black={R=0,G=0,B=0,A=0}
local function trace(pawn,a,b)
    local hit={}
    local ok,result=pcall(function() return U.GetKismetSystemLibrary():LineTraceSingle(pawn,a,b,0,false,{pawn},0,hit,true,black,black,0) end)
    if not ok then error("collision trace unavailable: "..tostring(result)) end
    if not result or not hit.ImpactPoint or not hit.ImpactNormal then return nil end
    -- Dynamic pawns get combat proxies; never bake them into permanent wall surfaces.
    if hit.Actor then
        local actor=hit.Actor:Get()
        if IO.valid(actor) and actor:IsA("/Script/Engine.Pawn") then return nil end
    end
    return hit.ImpactPoint,hit.ImpactNormal
end
local function box(out,x0,y0,z0,x1,y1,z1)
    if x1<=x0 or y1<=y0 or z1<=z0 then return end
    for _,v in ipairs({x0,y0,z0,x1,y1,z1}) do out[#out+1]=string.format("%.4f",v) end
end
-- Bounded incremental sampling. Immediate foot tiles are refreshed every update;
-- the wider floor/ceiling and wall work is spread across updates instead of a 500-ray burst.
local spacing=50
local radius=10
local offsets={}
for x=-radius,radius do for y=-radius,radius do
    if x*x+y*y<=radius*radius and (math.abs(x)>1 or math.abs(y)>1) then
        offsets[#offsets+1]={x,y}
    end
end end
table.sort(offsets,function(a,b) return a[1]^2+a[2]^2<b[1]^2+b[2]^2 end)
local floors,ceilings,walls={},{},{}
local step,scanIndex,wallIndex=0,1,0
function M.reset()
    floors,ceilings,walls={},{},{}
    step,scanIndex,wallIndex=0,1,0
end
local function patch(cache,key,x,y,values)
    if values then cache[key]={x=x,y=y,at=step,values=values} else cache[key]=nil end
end
local function floorTile(pawn,gx,gy,feet,ceiling,reuse)
    local x,y=gx*spacing,gy*spacing
    local key=tostring(gx)..":"..tostring(gy)
    local previous=floors[key]
    local roof=ceilings[key]
    if reuse and previous and step-previous.at<4 and (not ceiling or not roof or step-roof.at<4)
        and math.abs(previous.values[6]-feet)<55 then return end
    local start=feet+55
    -- A falling guest must not drag the next trace origin below a floor already observed.
    if previous and previous.values[6]>start and previous.values[6]-feet<250 then
        start=previous.values[6]+55
    end
    local hit,n=trace(pawn,{X=x,Y=y,Z=start},{X=x,Y=y,Z=feet-300})
    patch(floors,key,gx,gy,hit and n.Z>.55 and {x-25,y-25,hit.Z-12,x+25,y+25,hit.Z} or nil)
    if ceiling then
        hit,n=trace(pawn,{X=x,Y=y,Z=feet+85},{X=x,Y=y,Z=feet+500})
        patch(ceilings,key,gx,gy,hit and n.Z<-.55 and {x-25,y-25,hit.Z,x+25,y+25,hit.Z+5} or nil)
    end
end
function M.sample(pawn)
    step=step+1
    local p=pawn:K2_GetActorLocation()
    local feet=p.Z-pawn.CapsuleComponent:GetScaledCapsuleHalfHeight()
    local gx,gy=math.floor(p.X/spacing+.5),math.floor(p.Y/spacing+.5)
    -- Nine priority floor/ceiling pairs cover the entire footprint, including either side of a tile seam.
    for x=-1,1 do for y=-1,1 do floorTile(pawn,gx+x,gy+y,feet,true,true) end end
    for _=1,2 do
        local o=offsets[scanIndex];scanIndex=scanIndex%#offsets+1
        floorTile(pawn,gx+o[1],gy+o[2],feet,true)
    end
    local function wall(angle,height,key)
        local hit,n=trace(pawn,{X=p.X,Y=p.Y,Z=feet+height},
            {X=p.X+math.cos(angle)*450,Y=p.Y+math.sin(angle)*450,Z=feet+height})
        local values=nil
        if hit and math.abs(n.Z)<.3 then
            if math.abs(n.X)>.92 then
                local dx=-n.X*4
                values={math.min(hit.X,hit.X+dx),hit.Y-16,hit.Z-36,math.max(hit.X,hit.X+dx),hit.Y+16,hit.Z+36}
            elseif math.abs(n.Y)>.92 then
                local dy=-n.Y*4
                values={hit.X-16,math.min(hit.Y,hit.Y+dy),hit.Z-36,hit.X+16,math.max(hit.Y,hit.Y+dy),hit.Z+36}
            else values={hit.X-5,hit.Y-5,hit.Z-36,hit.X+5,hit.Y+5,hit.Z+36} end
        end
        patch(walls,key,gx,gy,values)
    end
    -- Refresh a wall on each side immediately, including before the first background sweep completes.
    for direction=0,3 do wall(direction*math.pi/2,105,-1-direction) end
    for _=1,2 do
        local index=wallIndex;wallIndex=(wallIndex+1)%288
        wall((index%96)*math.pi*2/96,35+math.floor(index/96)*70,index)
    end
    local out={}
    for _,cache in ipairs({floors,ceilings,walls}) do
        for key,value in pairs(cache) do
            if step-value.at>192 or math.abs(value.x-gx)>radius+1 or math.abs(value.y-gy)>radius+1 then
                cache[key]=nil
            else box(out,table.unpack(value.values)) end
        end
    end
    return out
end
return M
