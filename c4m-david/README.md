# c4m

Test de protocol de consensus c4m

1 - Introduction

This code implements a network environment using the network simulator 3
framework version 3.30. It features a full network stack that allows us to make
an actual implementation of our consensus algorithms (C4M) at the
application layer.

This code needs to be seen as an application of the NS3 framework. Knowing how
to use it, it will help in the understanding of this code. However,
the algorithms behind the consensus protocol can be understood as is.
Indeed, we took care to making a clear distinction between the protocols'
implementations and the interfaces within the simulator.

2 - Sources

application_packet.cc/h: Implement the class to build network packets used by
                         C4M protocol. It takes care of most of the
                         serialization/deserialization processes.

b4m_traces.cc/h: Implement the class that gather traces reported by the
                 C4M protocol. Ideally, we should have one instance of this
                 class per instanciated node in the simulation.
                 But all nodes can also be linked to one instance of this class
                to centralise the reported traces.

b4mesh-mobility.cc/h: An application to implement the mobility of nodes.
                      In this application the topology changes are detected.
                      Several mobility model have been implemented in this class.

b4mesh-raft.cc/h: An application implementing the consensus protocol used for
                  the b4mesh project (C4M). This is an improved version of the
                  raft consensus protocol.
                  Read the soon to be published,  related paper for more
                  information.

bloc.cc/h: Implement the block data structure that compose a blockchain.

trasaction.cc/h: Implementation of the transaction data structure that compose a
                 block.

experiment.cc/h: Implement and configure the simulation environment. This is
                 where most of the code related to NS3 is located.

utils.h: Some useful function implemented such as binary dumping, random number
         generator, hashing function, etc...

xyz-helper.cc/h: Helper classes are used by the NS3 to automate the install
                process of applications in instanciated nodes.


3 - General Information

The code makes a heavy use of asynchrone programming. This means that
implemanted applications react to interruption events and/or procedures schedule
in an event loop. It is advised for the reader to be familiar with the
principles of asynchrone programming (event loop, callback functions, ...).

Although we tried to make most of the serialization/deserialization process in
the application_packet and data structure classes, some of these operations may
be scattered around the code, mainly in procedure that process protocol
messages. So it is also advised for the reader to be familiar with the concept
of data serialization/deserialization and basic raw data processing in C/C++.

Be careful of the Makefile provided in the archive. It was made for personnal use in
order to bypass the compilation process that is specific to NS3. However, the archive
should compatible with the standard NS3 compilation scripts.

4 - Mobility Models descriptions

WARNING: All models are configure to work in a 900s simulation. Changing the
time of simulation will need an adjustment of the timing for all models.

Mobility model 1:
In this model all nodes are always moving together in a same connected network
Thus, no split or merge are produced in this model.

( ------------------- )

Mobility model 2:
In this model a split is forced at 300s creating 2 independent networks of equal
size. At 600s a merge is forced creating a fully connected network once again.
               /---------\
( ----------- <           > -------- )
               \---------/

Mobility model 3:
In this model, a first split is forced at 200s creating independent network 1
and 2 both made of 6 nodes. At 400s, independent network 2 is splitted again
creating independent network 3 and 4 both made of 3 nodes. At 600s independent
network 3 and 4 are merged again into independent network 2. At 800s independent
network 1 and 2 are merged to create a fully connected network once again.

                          /-------\
                /------- <         >-----\
               /          \-------/       \
( ----------- <                            > ---------- )
               \--------------------------/

Mobility model 4:
In this model, all nodes moves together creating a fully connected network.
At 300s a split is forced, creating 3 independent networks of equal size.
At 600s a merge is forced of all 3 independent network creating a fully connected
network once again.
                /----------\
( ----------- < ----------- > -------- )
                \----------/


5 - Set a mobility model for simulation

There are 4 mobility models implemented in the b4mesh-mobility.cc file. To change
the mobility model, it is needed to uncomment the desired model and comment
the previous one being used. Mobility models are implemented in the
UpdateLeaderPos() function (line 202) and mobility model 2 is set by default.

Model 1 and 2 are recommended to be executed with the following network size value:
NumNodes = 10 (This value can be modified inside the experiment.cc file at
line 8)

- For model 1 the value "leaderIdMod" inside the b4mesh-mobility.cc file
  must be set at 10 (leaderIdMod = 10) This value is found in line 27
- For model 2 the value "leaderIdMod" inside the b4mesh-mobility.cc file
  must be set at 5 (leaderIdMod = 5) This value is found in line 27

Model 3 and 4 are recommended to be executed with the following network size value:
NumNodes = 12 (This value can be modified inside the experiment.cc file at
line 8)

- For model 3 the value "leaderIdMod" inside the b4mesh-mobility.cc file
  must be set at 3 (leaderIdMod = 3) This value is found in line 27
- For model 4 the value "leaderIdMod" inside the b4mesh-mobility.cc file
  must be set at 4 (leaderIdMod = 4) This value is found in line 27
