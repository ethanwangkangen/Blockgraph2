## Acknowledgements

This repository is based on the Blockgraph/Consensus4Mesh(C4M) protocol designed by David Mordova. [https://gitlab.lip6.fr/cordova/b4mesh] and [https://gitlab.lip6.fr/cordova/c4m].

My contribution has been simply been to integrate the c4m consensus algorithm into the blockgraph protocol - ie coupling the 2 aspects of the protocol into a whole to test feasibility and performance.

All credit goes to the original author, whose paper can be found here https://theses.hal.science/tel-03922843/document

Some of the following README.md has been modified from the original repositories to reflect the updated instructions.

--------------------------------------------------------------------------------
# B4Mesh
Development of a blockchain technology for mesh and ad hoc networks for `ns3` simulator

## Introduction

This code implements a network environment using the network simulator `ns3` framework version `3.44`. It features a full network stack that allows us to make an actual implementation of our blockgraph algorithm at the application layer.

This code needs to be seen as an application of the `ns3` framework. Knowing how to use it, it will help in the understanding of this code. However, the algorithms behind the blockgraph algorithm can be understood as is. Indeed, we took care to making a clear distinction between the protocols' implementations and the interfaces within the simulator.

## General Information

This code makes a heavy use of asynchronous programming. This means that implemanted applications react to interruption events and/or procedures schedule in an event loop. It is advised for the reader to be familiar with the principles of asynchronous programming (event loop, callback functions, ...).

Although we tried to make most of the serialization/deserialization process in the application_packet and data structure classes, some of these operations may be scattered around the code, mainly in procedure that process protocol messages. So it is also advised for the reader to be familiar with the concept of data serialization/deserialization and basic raw data processing in `C/C++`.

## ns3 configuration

**NB** : Before running this project make sure to have `ns3` version 3.44 installed. <br />
To install **ns3** follow this tutorial -> [https://www.nsnam.org/docs/tutorial/html/getting-started.html] 

### **Build the project**

To build the project with CMake:
Start from top level ns3.44 folder.
> cd ../ns-3.44

Configure the build
> cmake -B build

Or from within the b4mesh folder,
> ./build.sh

### **Compile and execute b4mesh simulation**

Then run the script with
> ./run.sh {arguments}

This script also generates the associated graphs, which can be found in the /results folder.

The following table resumes the available arguments and its default vaules. 

**Arguments** | **Description** | **Values** | **Type** | **Default Value**
-------------- | --------------- | --------- | -------- | -----------------
**nNodes**     | Nunmber of nodes | *nNodes* > 0 && *nNodes* < 51 | INTEGER | 10
**sTime**      | Time of simulation | *sTime* > 0 && *sTime* < 7200 | INTEGER <br /> *seconds* | 600
**txGen**      | Txs Generation Rate (λ) in one node | λ > 0 | DOUBLE | 0.5 txs/s
**mMobility**  | The mobility model use fot the simulation | *mMobility* > 0 && *mMobility* < 4 | INTEGER | 1 
**mLoss**      | The loss propagation model | *mLoss* > 0 && *mLoss* < 5 | INTEGER | 2
**nScen**      | Scenario number |  *nScen* ∈ {1,2,3,4} | INTEGER | 1
**speed**      | Speed of the nodes | *speed*  > 0 | DOUBLE <br /> *meters/seconds* | 2.0

A list of the available arguments can be seen by executing the following command:

> ./run.sh --help

As an example, you can run the following simulation that sets the second mobility scenario, the number of nodes at 12 and a simulation time of 1000 seconds. <br /> <span style="color:red"> Remember: If an argument is NOT specified, it will take it default value.</span>

> ./run.sh --nNodes=12 --sTime=1000 --nScen=2

### Transaction Generation Rate 
The transaction generation rate (λ) is entered for a singe node. <br />
Ex. If λ = 2 : For a 10 nodes network, each node in the network will generate 2 txs/s
having a global transaction generation rate of 20 txs/s

### Mobility Model
There are three mobility model availables for simulation.
1. **Constant Position Mobility Model** <br />
···When using this model you can choose between 4 controled mobility scenario **nScen** (see section *Mobility Scearios*)
2. **Random Walk2 Mobility Model** <br />
···When using this mobility model **nScen** is ignored.
3. **To be defined** <br />


### Loss Propagation Model

1. **Friis Propagation Loss Model**
2. **Range Propagation Loss Model**
3. **Log Distance Propagation Loss Model**
4. **Fixed RSS Propagation Loss Model**

*The loss propagation model can be use in any mobility model and in any mobility scenario*

### Mobility Scenarios 

Four mobility scenarios are implemented in this version of the blockgraph b4mesh project. The objective of those mobility scenarios is to validate the correct functioning of the blockgraph protocol in a context of mobility.

The description of each mobility scenario is as follow:

* **Scenario 1** : In this scenario all nodes are always moving together in a same connected network. Thus, no split or merge are produced in this model.
```
---------G0---------- 
```
* **Scenario 2** : In this scenario all nodes moves together on the `X` axis during the first third of the simulation time. After the first third of the simulation time, the first half of the nodes increases the value of their `Y` possition in order to move in opposite direction with respect of the other half of the nodes to create a `split`. The other half of nodes do the same by decreassing the value of their Y possition. This behavior continues until reaching a certain threshold and stop modifying their Y possition. After two thirds of the simulation time, the opposit behavior is held until all nodes meet again to create a `merge`. 
```
           /----G1---\
---G0-----<           >-----G0---- 
           \----G2---/
```
* **Scenario 3** : In this scenario, all nodes start by moving together on the `X` axis during the first third of the simulation time. After the first third of the simulation time, the nodes are divided in 3 groupes. The first group will increase the `Y` value of theire possition, the second group will have it unchange and the third group will dicrease the `Y` value of their possition. This movement will create a `split` of  the three groupes. This behavior will continue until reaching a certain threshold. After two thirds of the simulation time, the opposit behavior is held until all nodes meet again to create a `merge`.  
```
           /----G1----\
----G0----<-----G2----->----G0----
           \----G3----/
```
* **Scenario 4** : In this scenario, all nodes start by moving together on the `X` axis durig the first sixth of the simulation time. Afther the first sixth of the simulation time, the nodes are divided in two groups of equal size. The first group will start to increase the `Y` values of their possition, while the second group will de-increase the `Y` values of their possition. This behavior will continue until a certain threshold causing a `split` between the two groupes. At three sixth of the simulation time, the first group will divide again into two other groups of equal size. This new `split` will follow the same pattern as the first split. At four sixth of the simulation time, the two groups of the first group will move such as to create a `merge` and form the first group from the first split. At five sixth of the simulation time, the two groups will move such as to create a `merge`.  
```
                            /---G3---\
                /----G1----<          >---G1---\
               /            \---G4---/          \
-----G0------ <                                  > -----G0-----
               \----------------G2--------------/
```

#### Arguments values restrictions for each scenario

* For Scenario 1
> **nNodes** must be within range of `[3 - 50]` nodes
> **sTime** must be grather than `100 seconds`

* For Scenario 2
> **nNodes** must be within range of `[6 - 50]` nodes
> **sTime** must be grather than `300 seconds`

* For Scenario 3
> **nNodes** must be within range of `[9 - 50]` nodes
> **sTime** must be grather than `600 seconds`

* For Scenario 4
> **nNodes** must be within range of `[12 - 50]` nodes
> **sTime** must be grather than `1000 seconds`

<span style="color:red">**WARNING** :</span> 
* Setting a simulation time higher than `1000 seconds` may cause **long** execution time.
* When increasing the number of nodes, the execution simulation time increases as well.
* Setting a generation transaction rate (λ) to small will increase the execution simulation time.

### **Enable and disable traces**

By default traces of the simulation are disable. This means than any trace of the program is shown on the output of the terminal when executing a simulation. Traces here are used a log of the execution of a simulation to better analyse the behavior of our solution. 
There are **2** different traces that can be enable.

* Traces of the blockgraph protocol
* Traces of the mobility scenario

In order to enable traces, you have to uncomment code located in the following files according on the traces you would like to collect.
The files in question are:
* b4mesh/b4mesh.cc
* b4mesh/b4mesh-mobility.cc

**Enable traces for the blockgraph protocol**
1. Open the b4mesh.cc file located at ../ns-3.30/scratch/b4mesh/*b4mesh.cc*
1. Uncomment the content of the last function called `debug` 
1. Compile and execute the program

It should turn from this: 
```c++
void B4Mesh::debug(string suffix){
 /*
  std::cout << Simulator::Now().GetSeconds() << "s: B4Mesh : Node " << node->GetId() <<
      " : " << suffix << endl;
  debug_suffix.str("");
 */ 
}
```
to this:
```c++
void B4Mesh::debug(string suffix){ 
  std::cout << Simulator::Now().GetSeconds() << "s: B4Mesh : Node " << node->GetId() <<
      " : " << suffix << endl;
  debug_suffix.str("");
}
```
**NB**: to enable traces for b4mesh-mobility do the same as for b4mesh.

### **Save your traces in a file**

In order to save the traces of a simulation you only need to redirect the output of the program to a file.
> ./run.sh > trace.txt

### **See the results of a simulation**

At the end of each simulation several files are generated inside the ../b4mesh/Traces folder
most of this files are ment for scripts usage. However, a human redable file is also generated that 
summarizes the results of the simulation. 
For every node in the simulation a file *performace-`node_Number`.txt* is generated.

>  ../b4mesh/Traces/*performaces-%d.txt*

You can compare the performances of each node in the network by contrasting their information. For instance, if all nodes have same number of blocks and same number of transactions then all nodes have the same blockchain. Other information is also displayed like the mean size of a block or the transactions still in the mempool.

Example of performance file
```
Execution time : 268.717s
Mean block creation time : 0ms
Mean block treatment time : 44.4786ms
Mean tx treatment time : 38.2686ms
Size of the blockgraph : 475Kb
Packets lost due to messaging pb: 22
************** BLOCK RELATED PERFORMANCES **************
B4mesh: Total number of blocks : 11
B4Mesh: Mean size of a block in the blockgraph : 43Kb
B4Mesh: Number of dumped blocks (already in waiting list or in the blockgraph): 0
B4Mesh: Number of remaining blocks' references in the missing_block_list: 0
B4Mesh: Mean time that a blocks' reference spends in the missing_block_list: 7.02084s
B4Mesh: Number of remaining blocks in the waiting_list: 0
B4Mesh: Mean time that a block spends in the waiting_list: 12.5004s
************** TRANSACTIONS RELATED PERFORMANCES **************
B4mesh: Total number of transactions in the blockgraph: 990
B4Mesh: Number of transactions generated by this node: 192
B4mesh: Number of committed transactions per seconds: 3.68415
B4Mesh: Size of all transactions in the blockgraph : 474kB
B4Mesh: Mean number of transactions per block: 90
B4Mesh: Mean time that a transaction spendS in the mempool: 24.8753s
B4Mesh: Number of remaining transactions in the mempool: 30
B4Mesh: Number of re transaction: 201
B4Mesh: Transactions lost due to mempool space: 0
B4Mesh: Number of droped transactions (already in blockgraph or in mempool):  6423
B4Mesh: Transactions with multiple occurance in the blockgraph: 0
```
### **Visualize the blockgraph of each node**

In order to visualize the graph craeted by each node during the simulation.
you may use the script *../b4mesh/scripts/`creategraphs`*

To do so, place yourself at the scripts folder.

> cd scripts

Execute the script specifying the number of nodes use for the simulation.

>./creategraphs < numNodes >

**NB**: This process might take a while. Wait until you see the following legend:

> *Results availables in ../results/*

You can now see each graph generated by each node is the b4mesh/results folder. <br>
As an example, the graph generated by a node running scenario 1 could look like this: 

![Blockgraph image](https://gitlab.lip6.fr/cordova/b4mesh/-/raw/blockgraph_implementation/Resources/Images/graphScen1.png)

## Source files
In this section you will find a short description of the content of each source file of the b4mesh project.
<br />
* **application_packet.cc/h** : Implement the class to build network packets. It takes care of most of the serialization/deserialization processes.
* **b4m_traces.cc/h** : Implement the class that gather traces reported by the blockgraph protocol. Ideally, we should have one instance of this class per instanciated node in the simulation. But all nodes can also be linked to one instance of this class to centralise the reported traces.
* **b4mesh-mobility.cc/h** : An application to implement the mobility of nodes. In this application the topology changes are detected. Several mobility scenarios have been implemented in this class. 
* **b4mesh.cc/h** : An application implementing the blockgraph protocol. Is the main class of this project.
* **b4mesh-oracle.cc/h** : An application simulating a consensus protocol used for the blockgraph application. It allows to parameter any consensus algorithm in order to better study the blockgraph algorithm.
* **bloc.cc/h** : Implement the block data structure that compose the blockchain.
* **trasaction.cc/h** : Implementation of the transaction data structure that compose a block.
* **experiment.cc/h** : Implement and configure the simulation environment. This is where most of the code related to `ns3` is located.
* **configs.h** : A configuration file where most of the blockgraph parameters are set. 
* **utils.h** : Some useful function implemented such as binary dumping, random number generator, hashing function, etc...
* **xyz-helper.cc/h** : Helper classes are used by the `ns3` to automate the install process of applications in instanciated nodes.
