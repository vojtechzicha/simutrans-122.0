# Bus layover beside the lane: a road ring (10..40 x 20..26) with a loop road 30..35 x 14..20, one-side
# layover tiles at 30,16 and 32,14, a both-sides one at 35,17, stops at 15,20, 25,26, 20,26 and 33,14,
# depot at 11,18 (2026-10-10).
newworld 64 64
waydesc Road_050
track 10 20 40 20
track 40 20 40 26
track 40 26 10 26
track 10 26 10 20
track 30 20 30 14
track 30 14 35 14
track 35 14 35 20
track 11 18 11 20
depot roaddepot_2000 11 18
station CZR-Stops-ODIS_Bus-Empty-L 15 20
station CZR-Stops-ODIS_Bus-Empty-L 25 26
station CZR-Stops-ODIS_Bus-Empty-L 20 26
station CZR-Stops-ODIS_Bus-Empty-L 33 14
station VZ-Stations-BusLayover-OneSide 30 16
station VZ-Stations-BusLayover-OneSide 32 14
station VZ-Stations-BusLayover-BothSides 35 17
vehicles CZ-ODIS-SOR_BN95-Green
