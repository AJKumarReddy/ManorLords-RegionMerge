-- RegionMerge: claimed land becomes part of the settlement it borders.
--
-- A merged region is kept as a native Outpost of its parent town (so the game
-- saves the link itself). RegionMergeNative.dll makes the game treat that land
-- as the town: position lookups answer with the town (placement, building
-- costs, construction, workers, HUD), and buildings there are registered with
-- the town on placement, spawn and load. This script decides what to merge
-- and moves what an existing settlement already owns.
local PREFIX = "[RegionMerge] "
-- Commands to RegionMergeNative, sent as SetFamilyHome(familyID = magic, ...)
local MAGIC_RESIDENT = -777701 -- op 1: move a resident unit into the Context region
local MAGIC_MERGE = -777702    -- op 2: move a region's buildings into Context and share its trees
local MAGIC_ROADS = -777703    -- op 3: have a region's roads work out their regions again
local MAGIC_STOCK = -777705    -- op 5: keep a region's goods list a copy of its town's
local OUTPOST = 5              -- ESettlementType::Outpost

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
    -- Reuse the engine while it lives; FindFirstOf walks the object table.
    local e = W.engine
    if not valid(e) then
        e = FindFirstOf("RTSMultiEngineCPP")
        if not valid(e) then W = { ready_at = 0 }; return false end
    end
    local key = e:GetAddress()
    if key ~= W.engine_addr then
        W = { engine_addr = key, engine = e, ready_at = os.clock() + 8, merged = {}, fresh = {},
              hidden = {}, retagged = {}, roads_done = {} }
        return false
    end
    if os.clock() < W.ready_at then return false end
    if not W.ready then
        -- The title screen's background world has a live engine too; only
        -- look for the menu until this world is confirmed to be a game.
        local menu = FindFirstOf("mainMenu_widget_C")
        if valid(menu) then
            local ok, shown = pcall(menu.IsInViewport, menu)
            if ok and shown then return false end
        end
        W.ready = true
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

-- Natively: every building on `child` except its camp and map decorations
-- joins `parent` (from the region's own list; the engine-wide list can hold
-- destroyed buildings, which must never be touched from Lua), and `parent`
-- gets `child`'s tree lists so its woodcutters and foresters see those trees.
local function move_all(child, parent)
    parent:SetFamilyHome(MAGIC_MERGE, child, false)
end

-- Roads worked out which regions they cross while the land still stood alone,
-- so they never named the town, and the town's planning data is what plot
-- snapping and the road collision test read. Have them work it out again --
-- once per piece of land, since the game keeps no check against a road being
-- filed twice.
local function refresh_roads(child, parent)
    parent:SetFamilyHome(MAGIC_ROADS, child, false)
end

-- Resource clumps (berries, stone, game, ...) carry a Region tag of their own
-- and stay with the land they sit on; retagging them changes nothing a
-- gatherer reads. What a gathering hut reads is the resource nodes the clumps
-- are grouped into, and the helper hands the land's nodes to the town on every
-- merge sweep (hand_over_nodes in RegionMergeNative).

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

-- Settling hands out starter goods and wealth; merged land gets neither. The
-- goods are removed through the region's stock, so no building is touched
-- (the game deletes settler tents once they are empty).
local function strip_starter(child, info)
    info.tries = info.tries + 1
    child.regionalWealth = info.wealth
    local any = false
    for t = 0, 260 do
        local ok, n = pcall(child.getStockOfGood, child, t, false, false)
        if ok and type(n) == "number" and n > 0 then
            child:consumeGood(t, n, CreateInvalidObject(), false, false, false)
            any = true
        end
    end
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
    local before = len(parent:GetBuildings())
    move_all(child, parent)
    local moved = len(parent:GetBuildings()) - before
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

-- Land merged into a town takes the town's name, so hovering it names the town
-- rather than the settlement it used to be. The game renames regions itself --
-- renameRegion is its own call, and it tells the rest of the game through
-- OnRegionRenamed -- and a save finds a region again by the Center it was
-- written at, not by what it is called (SavedRegion keeps a Center and an
-- outpostToRegionLocation, with a CustomName beside them), so two regions
-- sharing a name cannot confuse a load. The name is set again on every sweep,
-- so renaming the town carries to everything merged into it.
local function rename_to_town(child, parent)
    local want = get(parent, "regionName")
    if want == nil or want == "" or get(child, "regionName") == want then return end
    pcall(function() child:renameRegion(want) end)
end

-- Hide the border line between a town and land merged into it.
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

    local signature = {}
    for _, r in ipairs(children) do
        local key = addr(r)
        local info = W.fresh[key]
        if info then
            if strip_starter(r, info) then W.fresh[key] = nil end -- move on the next sweep
        else
            move_all(r, merged[key])
            rename_to_town(r, merged[key])
            -- a building's cost is checked against the goods the region itself
            -- holds, so merged land is kept holding what its town holds
            merged[key]:SetFamilyHome(MAGIC_STOCK, r, false)
            if not W.roads_done[key] then refresh_roads(r, merged[key]); W.roads_done[key] = true end
        end
        signature[#signature + 1] = tostring(key)
    end
    table.sort(signature)
    signature = table.concat(signature, ",")
    if signature ~= W.border_signature then
        hide_inner_borders(merged)
        W.border_signature = signature
    end
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
    local merge_key = cfg.MergeKey or "PAGE_UP"
    local merge_mods = cfg.MergeModifiers or { "CONTROL", "ALT" }
    local key = Key[merge_key]
    local mods = {}
    for _, m in ipairs(merge_mods) do mods[#mods + 1] = ModifierKey[m] end
    if key then
        RegisterKeyBind(key, mods, function() ExecuteInGameThread(guarded(merge_selected, "merge")) end)
    else
        log("unknown MergeKey '" .. tostring(cfg.MergeKey) .. "' in config.lua; use the console command")
    end
    -- Diagnostic: while a building is held ready to place over merged land,
    -- report why the game is refusing it and what that land says it holds.
    local function debug_placement(out)
        local function say(m)
            log(m)
            if out then pcall(out.Log, out, "RegionMerge: " .. m) end
        end
        if not refresh_world() then say("world not ready"); return end
        local p = W.pawn
        local r = get(p, "regionUnderCursor") or get(p, "selectedRegion") or get(p, "currentRegion")
        local root = valid(r) and root_of(r)
        say(string.format("region=%s type=%s root=%s", str(get(r, "regionName")),
            str(get(r, "settlementType")), root and str(get(root, "regionName")) or "-"))
        say(string.format("hoverProblem=%s insideBorders=%s isSnapped=%s fieldCollides=%s",
            str(get(p, "hoverProblem")), str(get(p, "isInsideBorders")),
            str(get(p, "placeBuilding_isSnapped")), str(get(p, "fieldCollides"))))
        if valid(r) then
            local parts = {}
            for _, g in ipairs({ 6, 15, 16, 172, 216 }) do
                local ok, n = pcall(r.getStockOfGood, r, g, false, false)
                parts[#parts + 1] = g .. "=" .. (ok and str(n) or "err")
            end
            say("stock seen by that region: " .. table.concat(parts, " "))
            if root then
                local tp = {}
                for _, g in ipairs({ 6, 15, 16, 172, 216 }) do
                    local ok, n = pcall(root.getStockOfGood, root, g, false, false)
                    tp[#tp + 1] = g .. "=" .. (ok and str(n) or "err")
                end
                say("stock seen by its town:   " .. table.concat(tp, " "))
            end
            local res = {}
            for _, x in ipairs(FindAllOf("Resource") or {}) do
                if valid(x) and same(get(x, "Region"), r) then
                    local t = str(get(x, "resourceType") or get(x, "Type") or "?")
                    res[t] = (res[t] or 0) + 1
                end
            end
            local rp = {}
            for t, n in pairs(res) do rp[#rp + 1] = t .. "x" .. n end
            say("resource deposits tagged to that region: " .. (#rp > 0 and table.concat(rp, " ") or "none"))
        end
    end
    RegisterConsoleCommandHandler("regionmergedebug", function(_, _, out)
        guarded(debug_placement, "debug")(out)
        return true
    end)
    RegisterConsoleCommandHandler("regionmerge", function(_, _, out)
        guarded(merge_selected, "merge")(out)
        return true
    end)
    local pretty = { CONTROL = "Ctrl", ALT = "Alt", SHIFT = "Shift", PAGE_UP = "PageUp",
                     PAGE_DOWN = "PageDown", HOME = "Home" }
    local parts = {}
    for _, m in ipairs(merge_mods) do parts[#parts + 1] = pretty[m] or m end
    parts[#parts + 1] = pretty[merge_key] or merge_key
    local keyname = table.concat(parts, "+")
    log("loaded (" .. keyname .. " or console 'regionmerge' merges the selected settlement into its neighbour)")
end
