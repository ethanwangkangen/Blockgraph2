#ifndef B4MESH_MOBILITY_HELPER_H
#define B4MESH_MOBILITY_HELPER_H

#include "ns3/node.h"
#include "ns3/object-factory.h"
#include "ns3/node-container.h"
#include "ns3/application-container.h"

#include "b4mesh-mobility.h"
#include <vector>

using namespace std;
using namespace ns3;

class B4MeshMobilityHelper{
  public:
    B4MeshMobilityHelper();
    ~B4MeshMobilityHelper();

    ApplicationContainer Install(NodeContainer c, int sTime, int nScen, int mMob, double speed); 

  private:
    ObjectFactory factory;
};

#endif
