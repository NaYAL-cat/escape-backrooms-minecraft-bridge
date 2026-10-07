-- Complete, versioned records. Reader rejects truncated/replayed messages.
local M = {}
-- Derive the mod directory from this installed script, including relative UE4SS paths.
local script = debug.getinfo(1, "S").source:gsub("^@", ""):gsub("\\", "/")
M.root = script:match("^(.*[/])Scripts/bridge_io%.lua$")
assert(M.root, "Cannot locate BackroomsMinecraftBridge from bridge_io.lua")
function M.read(name)
    local f=io.open(M.root..name.."-v2.csv","rb"); if not f then return nil end
    local text=f:read("*a"); f:close()
    if not text or #text>1048576 or text:sub(-1)~="\n" then return nil end
    text=text:gsub("[\r\n]+$","")
    local row={}; for item in (text..","):gmatch("(.-),") do row[#row+1]=item end
    if row[1]~="2" or #row<4 or row[#row]~=row[3] then return nil end
    table.remove(row); return row
end
function M.write(name,session,seq,values)
    local out={"2",session,tostring(seq)}
    for _,v in ipairs(values) do out[#out+1]=tostring(v) end
    out[#out+1]=tostring(seq)
    -- Readers validate the trailing sequence and newline, so no partial write is actionable.
    local f=io.open(M.root..name.."-v2.csv","wb"); if not f then return false end
    f:write(table.concat(out,",").."\n"); f:close(); return true
end
function M.number(x)
    local n=tonumber(x)
    if n==nil or n~=n or math.abs(n)==math.huge then return nil end
    return n
end
function M.valid(o) return o~=nil and o:IsValid() end
function M.same(a,b)
    return M.valid(a) and M.valid(b) and a:GetAddress()==b:GetAddress()
end
return M
