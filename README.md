# mpibounce

Simple pass-the-ball MPI test program.  A memory buffer of an arbitrary size is created by each rank and then sent to other ranks by various methods:

  - sendrecv:  The root rank initiates an MPI\_Send() of the "ball" to the next-highest rank then waits to MPI\_Recv() from the next-lowest rank.  All other ranks start with a MPI\_Recv() from the next-lowest rank followed by an MPI\_Send() to the next-highest rank.  A round is complete once the "ball" returns to the root rank.
  - broadcast:  The root rank initiates an MPI\_Bcast() of the "ball" to all other ranks.  Each subsequent rank takes a turn acting as the MPI\_Bcast() root.  A round is complete once the root rank becomes the MPI\_Bcast() root again.

## Building

CMake 3.9 or higher is required as well as an MPI compiler and runtime environment.

```
$ mkdir build
$ cd build
$ CC=mpicc cmake -DCMAKE_BUILD_TYPE=Release ..
-- The C compiler identification is Intel 19.1.3.20200925
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /opt/shared/openmpi/4.1.0-intel-2020/bin/mpicc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Found MPI_C: /opt/shared/openmpi/4.1.0-intel-2020/bin/mpicc (found version "3.1") 
-- Found MPI: TRUE (found version "3.1")  
-- Configuring done
-- Generating done
-- Build files have been written to: /home/1001/sw/mpibounce/build
$ make 
Scanning dependencies of target mpibounce
[ 50%] Building C object CMakeFiles/mpibounce.dir/mpibounce.c.o
[100%] Linking C executable mpibounce
[100%] Built target mpibounce
```
