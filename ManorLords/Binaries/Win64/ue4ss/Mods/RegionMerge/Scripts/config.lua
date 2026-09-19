-- RegionMerge settings. Edit and restart the game to apply.
return {
    -- Newly claimed regions that border one of your settlements (or land
    -- already merged into it) are merged into that settlement automatically.
    -- Set to false to merge only regions you turn into outposts yourself.
    AutoMergeNewClaims = true,

    -- Settling a region hands out starter goods and 50 regional wealth. A merged
    -- region is part of an existing town, so those freebies are removed.
    StripStarterSupplies = true,

    -- Hide the border line between a town and the land merged into it.
    HideInnerBorders = true,

    -- Seconds between sweeps. Buildings are normally moved by the native helper
    -- the moment they appear; the sweep is the fallback and does auto-merging.
    SweepSeconds = 2,
}
