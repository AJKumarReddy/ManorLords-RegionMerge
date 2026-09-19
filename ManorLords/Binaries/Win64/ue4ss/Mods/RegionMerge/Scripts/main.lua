-- RegionMerge: claimed land becomes part of the settlement it borders.
--
-- A merged region is kept as a native Outpost of its parent town (so the game
-- saves the link itself). RegionMergeNative.dll registers every building on
-- that land with the parent town: on load, on spawn, and when this script
-- sweeps. The parent therefore owns those buildings for jobs, stock,
-- construction and UI; the outpost stays an empty shell.
local PREFIX = "[RegionMerge] "
local MAGIC_MOVE = -777701     -- native op 1: move building into Context region
local MAGIC_RESIDENT = -777703 -- native op 3: move resident unit into Context region
local OUTPOST = 5              -- ESettlementType::Outpost
local CAMP = 30                -- settlement camp: stays in the outpost as its anchor
local DECORATION = 13          -- EBuildingFunction of map features: never moved

local function log(msg) print(PREFIX .. msg .. "\n") end

local function mod_dir()
    local src = debug.getinfo(1, "S").source:gsub("^@", "")
    local sep = "[/" .. string.char(92) .. "]"
    return src:match("^(.*)" .. sep .. "Scripts" .. sep)
end

local cfg = dofile(mod_dir() .. "/Scripts/config.lua")

-- ---------------------------------------------------------------- helpers
local function valid(o)
    if o == nil then return false end
    local ok, v = pcall(o.IsValid, o)
    return ok and v
end
local function get(o, f) local ok, v = pcall(function() return o[f] end); if ok then return v end end
local function len(a) local ok, n = pcall(function() return #a end); return ok and n or 0 end
local function addr(o) return valid(o) and o:GetAddress() or nil end
local function same(a, b) local x = addr(a); return x ~= nil and x == addr(b) end
local function str(v)
    if type(v) == "userdata" then local ok, s = pcall(v.ToString, v); if ok then return s end end
    return tostring(v)
end
local function each(arr, fn) -- fn(element) for valid elements; tolerates the array changing
    for i = 1, len(arr) do
        local ok, x = pcall(function() return arr[i] end)
        if ok and valid(x) then fn(x) end
    end
end

-- ---------------------------------------------------------------- native helper
local native_ok
do
    local loaded, err = package.loadlib(mod_dir() .. "/RegionMergeNative.dll", "*")
    local f = io.open(mod_dir() .. "/RegionMergeNative.status")
    local status = f and f:read("a") or "<no status file>"
    if f then f:close() end
    native_ok = loaded and status:sub(1, 2) == "OK"
    log(native_ok and ("native helper active (" .. status .. ")")
        or ("native helper NOT active: " .. tostring(err or status) .. " -- mod disabled, game unchanged"))
end

-- ---------------------------------------------------------------- world
-- W is rebuilt when the world changes; nothing is touched until the world has
-- been up for a few seconds and the main menu (a live background simulation)
-- is gone. Every cached object belongs to W and is dropped with it.
local W = { engine_addr = nil, ready_at = 0 }

local function refresh_world()
    local e = FindFirstOf("RTSMultiEngineCPP")
    if not valid(e) then W = { ready_at = 0 }; return false end
    local key = e:GetAddress()
    if key ~= W.engine_addr then
        W = { engine_addr = key, engine = e, ready_at = os.clock() + 8, merged = {}, fresh = {}, hidden = {} }
        return false
    end
    if os.clock() < W.ready_at then return false end
    local menu = FindFirstOf("mainMenu_widget_C")
    if valid(menu) then
        local ok, shown = pcall(menu.IsInViewport, menu)
        if ok and shown then return false end
    end
    W.pawn = get(e, "playerRef")
    return valid(W.pawn)
end

local function is_town(r)
    return get(r, "isSettled") and same(get(r, "ownerPawn"), W.pawn) and get(r, "settlementType") ~= OUTPOST
end

-- Follow outpost links up to the town that really owns the land.
local function root_of(r)
    for _ = 1, 8 do
        if not valid(r) or get(r, "settlementType") ~= OUTPOST then break end
        r = get(r, "outpostToRegion")
    end
    if valid(r) and is_town(r) then return r end
end

local function neighbours(r)
    local out = {}
    each(get(W.engine, "borders"), function(b)
        local a, c = get(b, "regionA"), get(b, "regionB")
        if same(a, r) and valid(c) then out[#out + 1] = c end
        if same(c, r) and valid(a) then out[#out + 1] = a end
    end)
    return out
end

-- Parent town for land next to r: a bordering town, or the town behind a
-- bordering merged outpost. With `best`, the most populous candidate.
local function bordering_town(r, best)
    local pick, pop
    for _, n in ipairs(neighbours(r)) do
        local root = is_town(n) and n or (same(get(n, "ownerPawn"), W.pawn) and root_of(n))
        if root and not same(root, r) then
            if not best then return root end
            local p = root:getNumTotalPopulation()
            if not pick or p > pop then pick, pop = root, p end
        end
    end
    return pick
end

-- region address -> movable buildings in it (one pass over all buildings).
-- Same rule as the native load/spawn hook: everything on merged land except
-- the camp and map decorations.
local function movable_by_region(wanted)
    local out = {}
    each(get(W.engine, "buildingArr"), function(b)
        local key = addr(get(b, "Region"))
        if key and wanted[key] and get(get(b, "Data"), "bType") ~= CAMP
            and get(b, "buildingFunction") ~= DECORATION then
            local list = out[key] or {}
            list[#list + 1] = b
            out[key] = list
        end
    end)
    return out
end

local function move_all(list, parent)
    for _, b in ipairs(list or {}) do parent:SetFamilyHome(MAGIC_MOVE, b, false) end
    return #(list or {})
end

-- ---------------------------------------------------------------- merging
local function merge_new_claim(r)
    local parent = bordering_town(r, false)
    if not parent then return end
    local loc = r:K2_GetActorLocation()
    if not same(W.engine:getRegionByPos(loc, false), r) then return end
    local xf = { Rotation = { X = 0, Y = 0, Z = 0, W = 1 }, Translation = loc, Scale3D = { X = 1, Y = 1, Z = 1 } }
    local wealth = get(r, "regionalWealth") or 0
    W.pawn:settleRegion(r, xf, 0, OUTPOST, parent)
    if cfg.StripStarterSupplies then W.fresh[addr(r)] = { wealth = wealth, tries = 0 } end
    log("merged new claim " .. str(get(r, "regionName")) .. " into " .. str(get(parent, "regionName")))
end

-- Settling hands out starter goods and wealth; merged land gets neither. Goods
-- are totalled first and then consumed from the region as a whole: emptied
-- settler tents are deleted by the game, so no building is touched afterwards.
local function strip_starter(child, info)
    info.tries = info.tries + 1
    child.regionalWealth = info.wealth
    local totals = {}
    each(get(W.engine, "buildingArr"), function(b)
        if same(get(b, "Region"), child) then
            local inv = get(b, "Inventory")
            for j = 1, len(inv) do
                local g = inv[j]
                local t, n = get(g, "Type"), get(g, "amt")
                if t and n and n > 0 then totals[t] = (totals[t] or 0) + n end
            end
        end
    end)
    local any = next(totals) ~= nil
    for t, n in pairs(totals) do child:consumeGood(t, n, CreateInvalidObject(), false, false, false) end
    return any or info.tries > 5 -- done (settlement buildings can take a moment to spawn)
end

-- Fold an existing settlement into a neighbouring town: buildings, families,
-- residents and treasury move to the parent, and the old settlement becomes an
-- outpost of it so the link survives saving.
local function merge_town(child, parent)
    -- 1. snapshot families, then detach them from home and workplace in the
    --    old town so no building keeps the old town's family IDs
    local plan = {}
    local fams = get(child, "workerFamilies")
    for i = 1, len(fams) do
        local f, members = fams[i], {}
        each(get(f, "familyMembers"), function(u) members[#members + 1] = u end)
        plan[i] = { members = members, home = get(f, "familyHome"), work = get(f, "assignedTo") }
    end
    for i, p in ipairs(plan) do
        if valid(p.work) then child:unassignFamily(i - 1) end
        if valid(p.home) then child:SetFamilyHome(i - 1, CreateInvalidObject(), true) end
    end
    -- 2. buildings (their stock moves with them)
    local moved = move_all(movable_by_region({ [addr(child)] = true })[addr(child)], parent)
    -- 3. families: recreate in the parent, then drop from the old town
    for i = #plan, 1, -1 do
        local p = plan[i]
        local id = parent:AddNewFamily(p.members)
        child:removeWorkerFamilyAt(i - 1)
        if valid(p.home) then parent:SetFamilyHome(id, p.home, true) end
        if valid(p.work) then parent:assignFamily(id, p.work) end
    end
    -- 4. residents list and each unit's native region (the save files units
    --    under it; a stale pointer crashes the next load)
    local units = {}
    each(get(child, "residents"), function(u) units[#units + 1] = u end)
    for _, u in ipairs(units) do parent:SetFamilyHome(MAGIC_RESIDENT, u, false) end
    -- 5. treasury, and keep the land linked as an outpost of the parent
    local w = get(child, "regionalWealth") or 0
    child.regionalWealth = 0
    parent:addRegionalWealth(w)
    child.settlementType = OUTPOST
    child.outpostToRegion = parent
    return string.format("merged %s into %s: %d buildings, %d families, %d residents, %d wealth",
        str(get(child, "regionName")), str(get(parent, "regionName")), moved, #plan, #units, w)
end

local function merge_selected(out)
    local msg
    if not refresh_world() then
        msg = "game world not ready yet"
    else
        local r
        for _, f in ipairs({ "selectedRegion", "regionUnderCursor", "currentRegion" }) do
            r = get(W.pawn, f)
            if valid(r) and is_town(r) then break end
            r = nil
        end
        local parent = r and bordering_town(r, true)
        msg = parent and merge_town(r, parent)
            or "select one of your settlements that borders another of your settlements, then try again"
    end
    log(msg)
    if out then out:Log("RegionMerge: " .. msg) end
end

-- Hide the border line between a town and land merged into it (once each).
-- Regions are never renamed: saves match region data by name, and a duplicate
-- name mixes up towns on load.
local function hide_inner_borders(merged)
    if not cfg.HideInnerBorders then return end
    each(get(W.engine, "borders"), function(b)
        local key = addr(b)
        if W.hidden[key] then return end
        local a, c = get(b, "regionA"), get(b, "regionB")
        local pa, pc = merged[addr(a)], merged[addr(c)]
        if (pa and (same(pa, c) or same(pa, pc))) or (pc and same(pc, a)) then
            b:SetActorHiddenInGame(true)
            W.hidden[key] = true
        end
    end)
end

local function sweep()
    if not refresh_world() then return end
    local merged, children, claims = {}, {}, {}
    each(get(W.engine, "regions"), function(r)
        if not same(get(r, "ownerPawn"), W.pawn) then return end
        if get(r, "isSettled") and get(r, "settlementType") == OUTPOST then
            local parent = root_of(r)
            if parent then merged[addr(r)] = parent; children[#children + 1] = r end
        elseif not get(r, "isSettled") then
            claims[#claims + 1] = r
        end
    end)
    W.merged = merged
    if cfg.AutoMergeNewClaims then for _, r in ipairs(claims) do merge_new_claim(r) end end

    local wanted = {}
    for _, r in ipairs(children) do
        local key = addr(r)
        local info = W.fresh[key]
        if info then
            if strip_starter(r, info) then W.fresh[key] = nil end -- move on the next sweep
        else
            wanted[key] = true
        end
    end
    if next(wanted) then
        local by_region = movable_by_region(wanted)
        for key, list in pairs(by_region) do move_all(list, merged[key]) end
    end
    hide_inner_borders(merged)
end

-- ---------------------------------------------------------------- UI redirect
-- The HUD and region panel follow the region under the camera or cursor. On
-- merged land that is the empty outpost, so point them at the parent town.
local UI_FIELDS = { "currentRegion", "selectedRegion", "RegionPanelTarget", "hoveringRegion" }
local function redirect_ui()
    local pawn, merged = W.pawn, W.merged
    if not merged or next(merged) == nil or not valid(pawn) or os.clock() < W.ready_at then return end
    local changed = false
    for _, f in ipairs(UI_FIELDS) do
        local parent = merged[addr(get(pawn, f)) or 0]
        if parent and valid(parent) then pawn[f] = parent; changed = true end
    end
    if changed then pcall(pawn.updateCurrentRegionUI, pawn) end
end

-- ---------------------------------------------------------------- scheduling
if native_ok then
    local function guarded(fn, what)
        return function(...)
            local ok, err = pcall(fn, ...)
            if not ok then log(what .. " error: " .. tostring(err)) end
        end
    end
    local sweeping = false
    LoopAsync(math.floor((cfg.SweepSeconds or 2) * 1000), function()
        if not sweeping then
            sweeping = true
            ExecuteInGameThread(function() guarded(sweep, "sweep")(); sweeping = false end)
        end
        return false
    end)
    LoopAsync(200, function()
        ExecuteInGameThread(guarded(redirect_ui, "ui"))
        return false
    end)
    RegisterKeyBind(Key.M, { ModifierKey.CONTROL }, function()
        ExecuteInGameThread(guarded(merge_selected, "merge"))
    end)
    RegisterConsoleCommandHandler("regionmerge", function(_, _, out)
        guarded(merge_selected, "merge")(out)
        return true
    end)
    log("loaded (Ctrl+M or console 'regionmerge' merges the selected settlement into its neighbour)")
end
