# /bin/bash
RESULT_PATH="../humantech_results/fio"
bench_load_name=("fio_sr_load_16" "fio_rr_load_16" "fio_rwsr_load_16" "fio_rwrr_load_16")
bench_run_name=("fio_sr_run_16" "fio_rr_run_16" "fio_rwsr_run_16" "fio_rwrr_run_16")

algo_type_name=("dftl")

echo "### DFTL START! ###"

TRACE="/home/yumin/real_trace/fio_16"

#####FIO
len=${#bench_load_name[@]}
for ((i=0; i<$len; i++));
do
    load=${bench_load_name[$i]}
    run=${bench_run_name[$i]}
    for algo in "${algo_type_name[@]}";
    do
        mkdir -p $RESULT_PATH/$algo

        ## 10,0
        echo "$algo start!"
        make clean SH_FTLTYPE=$algo
        echo "making driver"
        make -j SH_LCYCLE=1 SH_RCYCLE=0 SH_FTLTYPE=$algo > make_file 2>make_file_warn
        echo "making done"
        echo "./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$load.out"
        ./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$load.out


        ##10,1 
        make clean SH_FTLTYPE=$algo
        echo "making driver"
        make -j SH_LCYCLE=10 SH_RCYCLE=1 SH_FTLTYPE=$algo >make_file 2>make_file_warn
        echo "making done"
        echo "./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$run.out"
        ./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$run.out

        make clean SH_FTLTYPE=$algo
    done
done



RESULT_PATH="../humantech_results/fio"
bench_load_name=("fio_mix_load_16" "fio_rwmix_load_16")
bench_run_name=("fio_mix_run_16" "fio_rwmix_run_16")

echo "FIO MIX START"

len=${#bench_load_name[@]}
for ((i=0; i<$len; i++));
do
    load=${bench_load_name[$i]}
    run=${bench_run_name[$i]}
    for algo in "${algo_type_name[@]}";
    do
        mkdir -p $RESULT_PATH/$algo

        ## 1,0
        echo "$algo start!"
        make clean SH_FTLTYPE=$algo
        echo "making driver"
        make -j SH_LCYCLE=1 SH_RCYCLE=0 SH_FTLTYPE=$algo > make_file 2>make_file_warn
        echo "making done"
        echo "./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$load.out"
        ./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$load.out


        ##1,10 
        make clean SH_FTLTYPE=$algo
        echo "making driver"
        make -j SH_LCYCLE=1 SH_RCYCLE=10 SH_FTLTYPE=$algo >make_file 2>make_file_warn
        echo "making done"
        echo "./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$run.out"
        ./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$run.out

        make clean SH_FTLTYPE=$algo
    done
done


RESULT_PATH="../humantech_results/filebench"

TRACE="/home/yumin/real_trace/filebench_16"
bench_load_name=("fserver_load_16" "varmai_load_16" "webserver_load_16" "webproxy_load_16")
bench_run_name=("fserver_run_16" "varmail_run_16" "webserver_run_16" "webproxy_run_16")

echo "Filebench start!"

len=${#bench_load_name[@]}
for ((i=0; i<$len; i++));
do
    load=${bench_load_name[$i]}
    run=${bench_run_name[$i]}
    for algo in "${algo_type_name[@]}";
    do
        mkdir -p $RESULT_PATH/$algo

        ## 1,0
        echo "$algo start!"
        make clean SH_FTLTYPE=$algo
        echo "making driver"
        make -j SH_LCYCLE=1 SH_RCYCLE=0 SH_FTLTYPE=$algo > make_file 2>make_file_warn
        echo "making done"
        echo "./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$load.out"
        ./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$load.out


        ##1,10 
        make clean SH_FTLTYPE=$algo
        echo "making driver"
        make -j SH_LCYCLE=1 SH_RCYCLE=5 SH_FTLTYPE=$algo >make_file 2>make_file_warn
        echo "making done"
        echo "./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$run.out"
        ./driver $TRACE/$load.out $TRACE/$run.out > $RESULT_PATH/$algo/$algo-$run.out

        make clean SH_FTLTYPE=$algo
    done
done


echo "Program end!"
