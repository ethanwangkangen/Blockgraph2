#include "experiment.h"

Experiment::Experiment(int nNodes, int sTime, int nScen, int hBeat, int eTimeout){

	trace_dir = ".";
	phy_mode = "DsssRate11Mbps";
  duration = sTime;
	numNodes = nNodes;
	numScenario = nScen;
	heartbeat = hBeat;
	electiontimeout = eTimeout;
	cout << "RUN = "<< RngSeedManager::GetRun() << " ----  number of nodes = " << numNodes<<  endl;

	Init();
	nodes.Create(numNodes);

	CreateWifi("ns3::ConstantSpeedPropagationDelayModel",
		"ns3::FriisPropagationLossModel");

	CreateMobility("ns3::ConstantPositionMobilityModel");
	CreateAddresses();

  CreateConsensus(); // It's important that the the consensus is the first
                     //application installed on nodes

  CreateMobilityApplication();
}

Experiment::~Experiment(){
}

void Experiment::Init(){
	Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
		StringValue(phy_mode));
}

void Experiment::CreateWifi(string delayModel, string lossModel){
  wifiPhy = YansWifiPhyHelper::Default();
  wifiPhy.Set("RxGain", DoubleValue(-10));
  wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);

  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay(delayModel);
  if (lossModel == "ns3::FriisPropagationLossModel") {
    wifiChannel.AddPropagationLoss(lossModel); // min success
  }
  if (lossModel == "ns3::LogDistancePropagationLossModel") {
    wifiChannel.AddPropagationLoss(lossModel, "Exponent", DoubleValue(2.5));
  }
  if (lossModel == "ns3::RangePropagationLossModel") {
    wifiChannel.AddPropagationLoss(lossModel, "MaxRange",
        DoubleValue(1000)); // min success
  }
  if (lossModel == "ns3::FixedRssLossModel") {
    wifiChannel.AddPropagationLoss(lossModel, "Rss", DoubleValue(-80)); // min success
  }
  wifiPhy.SetChannel(wifiChannel.Create());

  wifi.SetStandard(WIFI_PHY_STANDARD_80211g);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode",
      StringValue(phy_mode), "ControlMode", StringValue(phy_mode));

  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");
  devices = wifi.Install (wifiPhy, wifiMac, nodes);

}

void Experiment::CreateAddresses(){
  Ipv4StaticRoutingHelper staticRouting;

  // Central routing
  Ipv4ListRoutingHelper list;
  list.Add(olsr, 10);

  InternetStackHelper internet;
  internet.SetRoutingHelper(list);
  internet.Install(nodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.0.0", "255.255.0.0");
  interfaces = ipv4.Assign(devices);
}

void Experiment::CreateMobility(string mobilityModel){
  MobilityHelper mobility;

  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
      "MinX", DoubleValue(0.0),
      "MinY", DoubleValue(0.0),
      "DeltaX", DoubleValue(10),
      "DeltaY", DoubleValue(10),
      "GridWidth", UintegerValue(3),
      "LayoutType", StringValue("RowFirst"));

  if (mobilityModel == "ns3::RandomWalk2dMobilityModel"){
    mobility.SetMobilityModel (mobilityModel,
        "Mode", StringValue ("Time"),
        "Time", StringValue("2s"),
        "Speed", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
        "Bounds", StringValue("0|200|0|200"));
  }
  else{
    mobility.SetMobilityModel(mobilityModel);
  }
  mobility.Install(nodes);
}

void Experiment::CreateConsensus(){
  //  RaftHelper raftHelper;
#if 0
  RaftHelper consensusHelper(&consensus_traces);
#else
  consensus_traces = vector<B4MTraces>(nodes.GetN());
  B4MeshRaftHelper consensusHelper(&consensus_traces);
#endif
  consensus_apps = consensusHelper.Install(nodes, heartbeat, electiontimeout);
  consensus_apps.Start(Seconds(5));
  consensus_apps.Stop(Seconds(duration));

}

void Experiment::CreateMobilityApplication(){
    B4MeshMobilityHelper b4meshMobility;
    mobility_apps = b4meshMobility.Install(nodes, numScenario, duration);
    mobility_apps.Start(Seconds(5));
    mobility_apps.Stop(Seconds(duration));
}
void Experiment::Run(){

  Simulator::Stop(Seconds(duration + 30));
  Simulator::Run();
  Simulator::Destroy();

//  ostringstream filename;
//  filename << "results/param_4/seed_" << RANDOM_SEED;
//  filename << "results/scenario_4/seed_" << RANDOM_SEED;
  CompileRaftTraces();
}

void Experiment::CompileRaftTraces(){
/*
	if (filename == "")
    filename = "results/RaftTraces";
*/
  map<float, int> received_bytes;
  map<float, int> sent_bytes;
  map<float, int> received_messages;
  map<float, int> sent_messages;
  map<float, int> dropped_messages;

  map<float, float> election_delay;
  int election_count = 0;
  map<float, float> config_change_delay;
  int config_count = 0;

  // Retrieve data
  int id = 0;
  for (auto n : consensus_traces){
    // Get received bytes
    for (auto b : n.received_bytes){
      if (received_bytes.count(b.first) >= 0)
        received_bytes[b.first] += b.second;
      else
        received_bytes[b.first] = b.second;
    }
    // Get sent bytes
    for (auto b : n.sent_bytes){
      if (sent_bytes.count(b.first) >= 0)
        sent_bytes[b.first] += b.second;
      else
        sent_bytes[b.first] = b.second;
    }
    // Get received messages
    for (auto b : n.received_messages){
      if (received_messages.count(b.first) >= 0)
        received_messages[b.first] += b.second;
      else
        received_messages[b.first] = b.second;
    }
    // Get sent messages
    for (auto b : n.sent_messages){
      if (sent_messages.count(b.first) >= 0)
        sent_messages[b.first] += b.second;
      else
        sent_messages[b.first] = b.second;
    }

    for (auto b : n.dropped_messages){
      if (dropped_messages.count(b.first) >= 0)
        dropped_messages[b.first] += b.second;
      else
        dropped_messages[b.first] = b.second;
    }

    // Get election delays
    for (auto b : n.election_delay){
      election_count++;
      if (election_delay.count(b.first) >= 0)
        election_delay[b.first] += b.second;
      else
        election_delay[b.first] = b.second;
    }
    // Get config delays
    cerr <<"config change numbers node(" << id++ << ") : " << n.config_change_delay.size() << endl;
    for (auto b : n.config_change_delay){
      config_count++;
      cout << "Config delay : " << b.second << endl;
      if (config_change_delay.count(b.first) >= 0)
        config_change_delay[b.first] += b.second;
      else
        config_change_delay[b.first] = b.second;
    }
  }
  // Compute total bytes sent and received
  int total_received_bytes = 0;
  int total_sent_bytes = 0;
  int total_received_messages = 0;
  int total_sent_messages = 0;
  int total_dropped_messages = 0;
  for (auto b : received_bytes)
    total_received_bytes += b.second;

  for (auto b : sent_bytes)
    total_sent_bytes += b.second;

  for (auto m : received_messages)
    total_received_messages += m.second;

  for (auto m : sent_messages)
    total_sent_messages += m.second;

  for (auto m : dropped_messages)
    total_dropped_messages += m.second;

  // Compute average delays
  float average_election_delay = 0;
  int extreme_election_delay = 0;
  int count = 0;
  for (auto d : election_delay){
    count++;
    if (d.second > (1 + (average_election_delay / count)) * 100)
      extreme_election_delay++;
    else {
      average_election_delay += d.second;
      cerr << "election delays : " << d.second << endl;
    }
  }
  average_election_delay = average_election_delay / (election_count -
      extreme_election_delay);

  float average_config_delay = 0;
  int extreme_config_delay = 0;
  count = 0;
  for (auto d : config_change_delay){
    count++;
    average_config_delay += d.second;
//    if (d.second > (1 + (average_config_delay / (config_count -
//              extreme_config_delay)) * 100)){
//      extreme_config_delay++;
//    }
//    else {
//      average_config_delay += d.second;
//    }
  }
  average_config_delay = average_config_delay / (config_count);

//  return; // Uncomment to avoid writing results in a file
/*
  ofstream file(filename);
  if (!file.is_open()){
    cerr << "Can't open file " << filename << endl;
    return;
  }
	*/

	ofstream file;
  char filename[50];
  sprintf(filename, "scratch/c4m/Traces/C4MTraces.txt");
  file.open(filename, ios::out);

  cout << "-----------------" << endl;
  cout << "Print raft traces" << endl;
  cout << "-----------------" << endl;
  file << "Total bytes received : " << total_received_bytes << endl;
  file << "Total bytes sent : " << total_sent_bytes << endl;
  file << "Total messages received : " << total_received_messages << endl;
  file << "Total messages sent : " << total_sent_messages << endl;
  file << "Total messages dropped : " << total_dropped_messages << endl;
  file << "Number of leader elections : " << election_count << endl;
  file << "Number of configuration changes : " << config_count /
    (float)nodes.GetN() << endl;
  file << "Average election delay (number of extreme values removed): " <<
    average_election_delay << " " << extreme_election_delay << endl;
  file << "Average configuration change delay (number of extreme values removed): "
    << average_config_delay << " " << extreme_config_delay << endl;
/*
  file << "Total bytes received over time " << endl;
  int total = 0;
  vector<float> keys;
  for (auto b : received_bytes)
    keys.push_back(b.first);
  sort(keys.begin(), keys.end());
  for (auto k : keys){
    total += received_bytes[k];
//    file << k << " " << total << endl;
    file << k << " " << received_bytes[k] << endl;
  }
	*/
  file.close();
}
