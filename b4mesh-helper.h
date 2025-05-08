#ifndef B4MESH_HELPER_H
#define B4MESH_HELPER_H

#include "ns3/node.h"
#include "ns3/object-factory.h"
#include "ns3/node-container.h"
#include "ns3/application-container.h"

#include "b4m_traces.h"
#include "b4mesh.h"
#include <vector>

using namespace std;
using namespace ns3;
class B4MeshHelper{
  public:
    B4MeshHelper();
    B4MeshHelper(B4MTraces* t);
    ~B4MeshHelper();

    ApplicationContainer Install(NodeContainer c, float timeBetweenTxn);

  private:
    ObjectFactory factory;
    B4MTraces* traces;
};

#endif
