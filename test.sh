#!/bin/bash

./driver 0.234 1 >./result/23_1x.res &
./driver 0.25 1 >./result/25_1x.res &
./driver 0.265 1 >./result/26_1x.res &
./driver 0.281 1 >./result/28_1x.res 

./driver 0.234 2 >./result/23_2x.res &
./driver 0.25 2 >./result/25_2x.res &
./driver 0.265 2 >./result/26_2x.res &
./driver 0.281 2 >./result/28_2x.res

./driver 0.234 4 >./result/23_4x.res &
./driver 0.25 4 >./result/25_4x.res &
./driver 0.265 4 >./result/26_4x.res &
./driver 0.281 4 >./result/28_4x.res

./driver 0.234 8 >./result/23_8x.res &
./driver 0.25 8 >./result/25_8x.res &
./driver 0.265 8 >./result/26_8x.res &
./driver 0.281 8 >./result/28_8x.res
