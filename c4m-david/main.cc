#include <iostream>
#include "application_packet.h"
#include "utils.h"
#include "experiment.h"
#include "transaction.h"
#include "block.h"

using namespace std;

int main(int argc, char *argv[]){
#if 1
   int nNodes;
   int sTime;
   int nScen;
   int hBeat;
   int eTimeout;

   CommandLine cmd;
   cmd.AddValue("nNodes", "Number of nodes in the simulation", nNodes);
   cmd.AddValue("sTime", "Number of nodes in the simulation", sTime);
   cmd.AddValue("nScen", "The mobility scenario choose for the simulation", nScen);
   cmd.AddValue("hBeat", "The frequency of a hear beat in the simulation", hBeat);
   cmd.AddValue("eTimeout", "The interval of time of the election time out timer in the simulation", eTimeout);
   cmd.Parse (argc, argv);


  //Experiment e(nNodes, sTime);
  //Experiment e(nNodes, sTime, nScen);
  Experiment e(nNodes, sTime, nScen, hBeat, eTimeout);
  e.Run();
#else
  //bool test = false;
  return 0;
#endif
  return 0;
}
