#!/bin/bash

#fichier sans argument : 01 02 11 12



#fichier avec un seul arguments : 21 22 23 51 61

iteration_nb=22
iterations_to_exclude=3 # for each side
real_iteration_nb=$((iteration_nb - iteration_to_exclude * 2))
nb_thread_max=50
nb_thread_mutex_max=20
nb_fibo_max=20
nb_yield_max=200
array_size=100000


function execute2()
{
    for filep in build/2*_pthread ; do
	file=$(echo "${filep%%_*}")
	for i in `seq 1 $nb_thread_max` ; do
	    elapsed=0
	    t=()
	    sum=0
	    echo "Executing $file with $i threads..."
	    for j in `seq 1 $iteration_nb` ; do
		t+=($(./$file $i | tail -n 1))
            done
	    sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	    sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	    elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	    echo "$i  $elapsed" >> thread.dat
	done
	for i in `seq 1 $nb_thread_max` ; do
	    elapsed=0
	    t=()
	    sum=0
	    echo "Executing $filep with $i threads..."
	    for j in `seq 1 $iteration_nb` ; do
		t+=($(./$filep $i | tail -n 1))
	    done
	    sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	    sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	    elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	    echo "$i  $elapsed" >> pthread.dat
	done
	echo "set title \"Représentation du temps d'exécution du programme en fonction du nombre de threads ($file)\"
  set xlabel \"Nombre de threads\"
  set ylabel \"Temps en millisecondes\"
  set style line 10 linetype 1 \
                    linecolor rgb \"#ff0000\" \
                    linewidth 1
  set style line 11 linetype 1 \
                    linecolor rgb \"#0000ff\" \
                    linewidth 1
  plot [1:$nb_thread_max] 'thread.dat' with lines linestyle 11, 'pthread.dat' with lines linestyle 10" > script.gnu
	gnuplot -persist script.gnu
	rm -f thread.dat
	rm -f pthread.dat
    done
}


function execute3()
{
    for filep in build/3*_pthread ; do
	file=$(echo "${filep%%_*}")
	for i in `seq 1 $nb_thread_max` ; do
	    elapsed=0
	    t=()
	    sum=0
	    echo "Executing $file with $i threads..."
            for j in `seq 1 $iteration_nb` ; do
		t+=($(./$file $i $nb_yield_max | tail -n 1))
	    done
            sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	    sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	    elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	    echo "$i  $elapsed" >> thread.dat
	done
	for i in `seq 1 $nb_thread_max` ; do
	    elapsed=0
	    t=()
	    sum=0
	    echo "Executing $filep with $i threads..."
            for j in `seq 1 $iteration_nb` ; do
		t+=($(./$filep $i $nb_yield_max | tail -n 1))
	    done
	    sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	    sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	    elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)	
	    echo "$i  $elapsed" >> pthread.dat
	done
	echo "set title \"Représentation du temps d'exécution du programme en fonction du nombre de threads ($file avec $nb_yield_max yields)\"
  set xlabel \"Nombre de threads\"
  set ylabel \"Temps en millisecondes\"
  set style line 10 linetype 1 \
                    linecolor rgb \"#ff0000\" \
                    linewidth 1
  set style line 11 linetype 1 \
                    linecolor rgb \"#0000ff\" \
                    linewidth 1
  plot [1:$nb_thread_max] 'thread.dat' with lines linestyle 11, 'pthread.dat' with lines linestyle 10" > script.gnu
	gnuplot -persist script.gnu
	rm -f thread.dat
	rm -f pthread.dat
    done
}


function execute5()
{
    for i in `seq 1 $nb_fibo_max` ; do
	elapsed=0
	t=()
	sum=0
	echo "Executing "build/51-fibonacci" with $i threads..."
	for j in `seq 1 $iteration_nb` ; do
	    t+=($(./build/51-fibonacci $i | tail -n 1))
	done
	sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	echo "$i  $elapsed" >> thread.dat
    done
    for i in `seq 1 $nb_fibo_max` ; do
	elapsed=0
	t=()
	sum=0
	echo "Executing "build/51-fibonacci_pthread" with $i threads..."
	for j in `seq 1 $iteration_nb` ; do
	    t+=($(./build/51-fibonacci_pthread $i | tail -n 1))
	done
	sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	echo "$i  $elapsed" >> pthread.dat
    done
    echo "set title \"Représentation du temps d'exécution du programme en fonction du nombre de Fibonacci (build/51-fibonacci)\"
set xlabel \"Nombre de Fibonacci\"
set ylabel \"Temps en millisecondes\"
set style line 10 linetype 1 \
                  linecolor rgb \"#ff0000\" \
                  linewidth 1
set style line 11 linetype 1 \
                  linecolor rgb \"#0000ff\" \
                  linewidth 1
plot [1:$nb_fibo_max] 'thread.dat' with lines linestyle 11, 'pthread.dat' with lines linestyle 10" > script.gnu
    gnuplot -persist script.gnu
    rm -f thread.dat
    rm -f pthread.dat
}


function execute6()
{
    for i in `seq 1 $nb_thread_mutex_max` ; do
	elapsed=0
	t=()
	sum=0
	echo "Executing "build/61-mutex" with $i threads..."
	for j in `seq 1 $iteration_nb` ; do
	    t+=($(./build/61-mutex $i | tail -n 1))
	done
	sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	echo "$i  $elapsed" >> thread.dat
    done
    for i in `seq 1 $nb_thread_mutex_max` ; do
	elapsed=0
	t=()
	sum=0
	echo "Executing "build/61-mutex_pthread" with $i threads..."
	for j in `seq 1 $iteration_nb` ; do
	    t+=($(./build/61-mutex_pthread $i | tail -n 1))
	done
	sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	echo "$i  $elapsed" >> pthread.dat
    done
    echo "set title \"Représentation du temps d'exécution du programme en fonction du nombre de threads (build/61-mutex)\"
set xlabel \"Nombre de threads\"
set ylabel \"Temps en millisecondes\"
set style line 10 linetype 1 \
                  linecolor rgb \"#ff0000\" \
                  linewidth 1
set style line 11 linetype 1 \
                  linecolor rgb \"#0000ff\" \
                  linewidth 1
plot [1:$nb_thread_mutex_max] 'thread.dat' with lines linestyle 11, 'pthread.dat' with lines linestyle 10" > script.gnu
    gnuplot -persist script.gnu
    rm -f thread.dat
    rm -f pthread.dat
}


function execute7()
{
    for filep in build/7*_pthread ; do
	file=$(echo "${filep%%_*}")
	for i in `seq 1 $nb_thread_max` ; do
	    elapsed=0
	    t=()
	    sum=0
	    echo "Executing $file with $i threads..."
	    for j in `seq 1 $iteration_nb` ; do
		t+=($(./$file $i $array_size| tail -n 1))
            done
	    sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	    sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	    elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	    echo "$i  $elapsed" >> thread.dat
	done
	for i in `seq 1 $nb_thread_max` ; do
	    elapsed=0
	    t=()
	    sum=0
	    echo "Executing $filep with $i threads..."
	    for j in `seq 1 $iteration_nb` ; do
		t+=($(./$filep $i $array_size | tail -n 1))
	    done
	    sorted=$(echo "${t[@]}" | tr ' ' '\n' | sort -n)
	    sum=$(echo $sorted | cut -d' ' "-f$((1 + $iterations_to_exclude))-$((iteration_nb - $iterations_to_exclude))" | tr ' ' '\n' | paste -s -d+ - | bc)
	    elapsed=$(echo "scale=3 ; $sum / ($real_iteration_nb * 1000)" | bc -l)
	    echo "$i  $elapsed" >> pthread.dat
	done
	echo "set title \"Représentation du temps d'exécution du programme en fonction du nombre de threads ($file)\"
  set xlabel \"Nombre de threads\"
  set ylabel \"Temps en millisecondes\"
  set style line 10 linetype 1 \
                    linecolor rgb \"#ff0000\" \
                    linewidth 1
  set style line 11 linetype 1 \
                    linecolor rgb \"#0000ff\" \
                    linewidth 1
  plot [1:$nb_thread_max] 'thread.dat' with lines linestyle 11, 'pthread.dat' with lines linestyle 10" > script.gnu
	gnuplot -persist script.gnu
	rm -f thread.dat
	rm -f pthread.dat
    done
}



rm -f thread.dat pthread.dat
execute2
execute3
execute5
execute6
execute7
