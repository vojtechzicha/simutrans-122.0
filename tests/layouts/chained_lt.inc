# Two station boundaries in a row, a small rebuild of Praha-Vršovice -> ONJ skupina A (2026-10-30):
# V: terminus, 2 bay tracks (y 19, 20; x 10..17), P at x 18 for the way out east
# LT W at 24 (into V), single track, LT E at 30 (into the junction area), dwarf LT E at 38 (into H)
# H: yard, 3 dead-end tracks (y 19..21; x 44..49), dwarf P W at 43, depot on a spur in its throat
# Line L: H (All off, timetable every 30 at :15) -> V (Regular, every 30 at :00). Every train's
# planned track at H is y 20, the schedule's stop tile.
newworld 80 40
waydesc Rail_120_Tracks
track 10 20 49 20
track 10 19 19 19
track 19 19 19 20
stations ConcretePlatformTrainStop 10 17 20
stations ConcretePlatformTrainStop 10 17 19
sign VZ-Signals-D1-New-P 18 20 E
sign VZ-Signals-D1-New-P 18 19 E
sign VZ-Signals-D1-New-LT 24 20 W
sign VZ-Signals-D1-New-LT 30 20 E
sign VZ-Signals-D1-Dwarf-LT 38 20 E
track 40 20 40 19
track 40 19 49 19
track 41 20 41 21
track 41 21 49 21
stations ConcretePlatformTrainStop 44 49 20
stations ConcretePlatformTrainStop 44 49 19
stations ConcretePlatformTrainStop 44 49 21
sign VZ-Signals-D1-Dwarf-P 43 19 W
sign VZ-Signals-D1-Dwarf-P 43 20 W
sign VZ-Signals-D1-Dwarf-P 43 21 W
track 42 20 42 23
depot TrainDepot 42 23
line L
stop 44 20 0 0 A
timetable 30 15
stop 12 20 0 0 R
timetable 30 0
endline
vehicles VZ-CeskeDrahy-843-najbrt2-843 VZ-CeskeDrahy-843-najbrt2-043 VZ-CeskeDrahy-843-najbrt2-843
