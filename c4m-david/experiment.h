#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/aodv-helper.h"
#include "ns3/simulator.h"

#include "b4mesh-raft-helper.h"
#include "b4mesh-mobility-helper.h"
#include "b4m_traces.h"

using namespace ns3;
using namespace std;

class Experiment{
  public:
    // Constructors and destructor
    Experiment(int nNodes, int sTime);
    Experiment(int nNodes, int sTime, int nScen);
    Experiment(int nNodes, int sTime, int nScen, int hBeat, int eTimeout);
    ~Experiment();

  public:
    // Getters and setters

  public:
    /**
     * Initialize the environment of the simulator.
     */
    void Init();

    /**
     * Deploy the nodes over an area in a grid topology.
     */
    void CreateMobility(string mobilityModel);

    /**
     * Start the simulation.
     */
    void Run();

    /**
     * Setup the wifi interface of nodes (layer 2).
     */
    void CreateWifi(string delayModel, string lossModel);

    /**
     * Setup the Ipv4 address of nodes (layer 3) and routing protocol.
     */
    void CreateAddresses();

    /**
     * Setup the consensus application into the nodes.
     */
    void CreateConsensus();

    /**
     * Setup the mobility model for b4mesh as an application that changes nodes
     * cartesian position.
     */
    void CreateMobilityApplication();

    /**
     * Compile nodes' raft traces
     */
    //void CompileRaftTraces(string filename = "");
    void CompileRaftTraces();

  private:
    // Attributes
    NodeContainer nodes;
    WifiHelper wifi;
    YansWifiPhyHelper wifiPhy;
    NetDeviceContainer devices;
    Ipv4InterfaceContainer interfaces;
    OlsrHelper olsr;
    AodvHelper aodv;
    ApplicationContainer b4mesh_apps;
    ApplicationContainer consensus_apps;
    ApplicationContainer mobility_apps;

    vector<B4MTraces> consensus_traces;
    B4MTraces b4mesh_traces;

    string trace_dir;
    string phy_mode;

    int duration;
    int numNodes;
    int numScenario;
    int heartbeat;
    int electiontimeout;

  public:
    /*
     * Testing and try
     */
    static void CourseChange (std::string foo, Ptr<const MobilityModel> mobility)
    {
      Vector pos = mobility->GetPosition ();
      Vector vel = mobility->GetVelocity ();
      std::cout << Simulator::Now () << ", model=" << mobility << ", POS: x=" << pos.x << ", y=" << pos.y <<
        ", z=" << pos.z << "; VEL:" << vel.x << ", y=" << vel.y << ", z=" << vel.z << std::endl;
    }
};
#endif
