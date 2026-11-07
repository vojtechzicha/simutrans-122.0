# Bus layover for buses longer than a tile (2026-11-07, line 59 in Praha with 24 m buses of 8+5+6 car
# units): a road ring (10..60 x 20..26) with a pair of one-side layover tiles at 40,20 and 41,20, a single
# one-side tile at 43,20 (plain road at 42, so the same waiting area but no row with the pair), a row of
# three one-side tiles at 30..32,26 on the south side, stops at 20,20, 55,26 and 55,20, depot at 11,18.
newworld 72 40
waydesc Road_050
track 10 20 60 20
track 60 20 60 26
track 60 26 10 26
track 10 26 10 20
track 11 18 11 20
depot roaddepot_2000 11 18
station CZR-Stops-ODIS_Bus-Empty-L 20 20
station CZR-Stops-ODIS_Bus-Empty-L 55 26
station CZR-Stops-ODIS_Bus-Empty-L 55 20
station VZ-Stations-BusLayover-OneSide 40 20
station VZ-Stations-BusLayover-OneSide 41 20
station VZ-Stations-BusLayover-OneSide 43 20
station VZ-Stations-BusLayover-OneSide 30 26
station VZ-Stations-BusLayover-OneSide 31 26
station VZ-Stations-BusLayover-OneSide 32 26
