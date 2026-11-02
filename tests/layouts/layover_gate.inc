# Bus layover with one way in: a road ring (10..60 x 20..26) with two one-side layover tiles at 50,20
# and 51,20 on its north side, stops at 20,20 and 40,26, a depot far from the layover at 11,18 and one
# close to it at 47,18, whose buses join the ring three tiles before the layover (2026-11-02).
newworld 72 40
waydesc Road_050
track 10 20 60 20
track 60 20 60 26
track 60 26 10 26
track 10 26 10 20
track 11 18 11 20
track 47 18 47 20
depot roaddepot_2000 11 18
depot roaddepot_2000 47 18
station CZR-Stops-ODIS_Bus-Empty-L 20 20
station CZR-Stops-ODIS_Bus-Empty-L 40 26
station VZ-Stations-BusLayover-OneSide 50 20
station VZ-Stations-BusLayover-OneSide 51 20
vehicles CZ-ODIS-SOR_BN95-Green
