# Platforms (documentation/fork-platforms-plan.md): a station S with three tracks and the stops around it.
# Rail: main track y20 from the terminus A (x 5..10, depot at 4,20) to B (x 54..59); S at x 30..37 with a siding
# y19 north (switches at 26 and 44) and a siding y21 south (27 and 43); a branch leaves the main at x 48 south
# to the terminus C (x 54..59, y 35).
# Road, south of it: a ring 10..40 x 40..46 with a through stop "Street" at 20,40, a stop "West" at 10,43, a bay
# "Bay" at the end of a stub 30,40..30,37, a stop "South" at 25,46 and a depot at 11,38.
newworld 80 52
waydesc Rail_120_Tracks
track 4 20 70 20
track 26 19 44 19
track 26 19 26 20
track 44 19 44 20
track 27 21 43 21
track 27 21 27 20
track 43 21 43 20
track 48 20 48 35
track 48 35 60 35
stations ConcretePlatformTrainStop 5 10 20
stations ConcretePlatformTrainStop 30 37 20
stations ConcretePlatformTrainStop 30 37 19
stations ConcretePlatformTrainStop 30 37 21
stations ConcretePlatformTrainStop 54 59 20
stations ConcretePlatformTrainStop 54 59 35
depot TrainDepot 4 20
haltname 7 20 A
haltname 33 20 S
haltname 56 20 B
haltname 56 35 C
waydesc Road_050
track 10 40 40 40
track 40 40 40 46
track 40 46 10 46
track 10 46 10 40
track 30 40 30 37
track 11 38 11 40
depot roaddepot_2000 11 38
station CZR-Stops-ODIS_Bus-Empty-L 20 40
station CZR-Stops-ODIS_Bus-Empty-L 10 43
station CZR-Stops-ODIS_Bus-Empty-L 30 37
station CZR-Stops-ODIS_Bus-Empty-L 25 46
haltname 20 40 Street
haltname 10 43 West
haltname 30 37 Bay
haltname 25 46 South
