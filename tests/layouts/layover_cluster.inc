# Bus layover: a road ring (10..40 x 20..26) with stops at 15,20, 30,20 and 25,26, and a cluster of five
# dead-end layover tiles (23,15 23,17 24,15 24,17 25,16) on a side road at x 21..25, depot at 11,18
# (2026-10-10).
newworld 64 64
waydesc Road_050
track 10 20 40 20
track 40 20 40 26
track 40 26 10 26
track 10 26 10 20
track 21 20 21 16
track 21 16 25 16
track 23 15 23 17
track 24 15 24 17
track 11 18 11 20
depot roaddepot_2000 11 18
station CZR-Stops-ODIS_Bus-Empty-L 15 20
station CZR-Stops-ODIS_Bus-Empty-L 30 20
station CZR-Stops-ODIS_Bus-Empty-L 25 26
station VZ-Stations-BusLayover-DeadEnd 23 15
station VZ-Stations-BusLayover-DeadEnd 23 17
station VZ-Stations-BusLayover-DeadEnd 24 15
station VZ-Stations-BusLayover-DeadEnd 24 17
station VZ-Stations-BusLayover-DeadEnd 25 16
vehicles CZ-ODIS-SOR_BN95-Green
