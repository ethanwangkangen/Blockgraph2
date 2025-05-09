#include <iostream>
#include "application_packet.h"
#include "utils.h"
#include "experiment.h"
#include "transaction.h"
#include "block.h"
#include "blockgraph.h"

using namespace std;

int main(int argc, char *argv[]) {

// Default values
  int nNodes = 10;  // Number of nodes
  int sTime = 600;  // Time of simulation
  double txGen = 2.5;  // Mean time in seconds between the generation of two txs
  int mMobility = 1;  // Mobility model 
  int mLoss = 2;  // Loss propagation model 
  int nScen = 1;    // Number of scenario
  double speed = 2;  // Speed of the mobility model 


  CommandLine cmd;
  cmd.AddValue("nNodes", "Number of nodes in the simulation - default (10)", nNodes);
  cmd.AddValue("sTime", "Time of the simulation expressed in Seconds  - default (600s)", sTime);
  cmd.AddValue("txGen", "The mean time for a node to generate a transaction in seconds - default (0.5tx/s)", txGen);
  cmd.AddValue("mMobility", "The mobility model use for this simulation\n 1 = Constant Position Mobility Model (default)\n 2 = Random Walk2 Mobility Model\n 3 = NaN ", mMobility);
  cmd.AddValue("mLoss", "The propagation loss model use for this simulation\n1 = Friss Loss model\n2 = Range Loss model (default at 100m)\n3 = Log Distance Loss model\n4 = Fixed Loss model", mLoss);
  cmd.AddValue("nScen", "The mobility scenario choosen for this simulation\nwhen choosing a Constant Position Mobility model", nScen);
  cmd.AddValue("speed", "The velocity of the nodes in m/s", speed);
  cmd.Parse (argc, argv);

  Experiment e(nNodes, sTime, txGen, mMobility, mLoss,  nScen, speed);
  e.Run();

  return 0;
}
