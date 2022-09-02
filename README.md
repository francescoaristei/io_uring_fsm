# Finite State Machine using the Linux kernel interface IO_URING

The module is the final result of **Advanced Operating Systems** course held in the **Master's Degree in Computer Science & Engineering at Politecnico di Milano**, Italy.

- Project supervisor: Vittorio Zaccaria
- Course head professor: Vittorio Zaccaria
- Student developer: Francesco Aristei

Specifically, it consists in developing the FSM server implemented in the [aos_playground repository](https://github.com/vzaccaria/aos-playground/tree/master/code/th-c-async), using the `io_uring interface`.
The performances of the server are then evaluated against the other concurrency models already provided.

## FSM

The finite state machine implemented by the server is a simple one. 
One state machine is dynamically created for each client.
Each client can send an action represented by a char (0 .. 3) to change a client-specific state
(a ... d) in the server.
This structure allows to model several interactions. 
Any web application parses actions from the client to change its own state.

## IO_URING

io_uring is a **Linux kernel** interface to efficiently allow to send and receive data asynchronously. 
It was originally designed to target block devices and files but has since gained the ability to work with things like network sockets, like in our case.

## Project Report

In the Report folder, there is the official and complete documentation of the project. 
The .tex file is the LaTeX documentation, which can be compiled on a local machine. 
The pdf file is the already compiled LaTeX document.
There is a thorough high-level description of the code.

## Usage

Even though the purpose of the project is to implement the server only using the io_uring interface, all the servers already present in the [aos_playground repository](https://github.com/vzaccaria/aos-playground/tree/master/code/th-c-async), have been used in this repository, in order to implement the performance benchmarking.

### Simple Usage

To play with the servers implemented, first is necessary to open a terminal.
From here, starting from the `root` directory of the project, is required to move inside the `src/server` dir.
Here are located the files containing the code of the different servers.
Now, to run a specific implementation of the finite state machine, execute the following instructions contained in the `makefile`.

- `make utils.o` to generate the object file from the utils.c
- `make state.o` to generate the object file from the state.c
- `make <name_of_the _server>` to obtain the executable of the server

After that, the server is run with the command `./<name_of_executable>`.
Now the server will be listening on the localhost, on the port 9090.
To interact with it, open a second terminal and connect to the server using for example the telnet protocol, with the following command:
- `telnet localhost 9090`

From now on, this terminal can be used to send messages to the server and read in the stdout the corresponding state of the machine changing, accordingly to the action requested.

### Testing the models

Part of the project is to evaluate the goodness of one concurrency model against the other.
In order to do so, a simple benchmark application has been written.
Starting from the root directory of the project, open a terminal, and produce the executable from the `benchmark.c` with the following command:

- `gcc benchmark.c -o benchmark`

Then, run the server you want to benchmark, using the steps described in the **simple usage** paragraph.
Finally, test the server, executing the benchmark with the following command:

- `./benchmark <number_of_clients> <duration_of_the_interaction_in_seconds>`

The results of the testing are written in the stdout.

## Development Environment

The module has been developed in Ubuntu 22.04 LTS distro with development tools on a Virtual Machine.

Kernel version: v5.15.

## Tools & Reference Material

- [Efficient IO with io_uring](https://kernel.dk/io_uring.pdf) - The main reference article explaining the io_uring interface.  
- [Lord of the io_uring](https://unixism.net/loti/) - A very useful blog describing the io_uring technology with lots of tutorial and practical examples. A very detailed explanation of the liburing API is also provided.





