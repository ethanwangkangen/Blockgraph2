#ifndef B4M_TRACES_H
#define B4M_TRACES_H

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>

using namespace std;
class B4MTraces{
  public:
    B4MTraces();
  public:

    // Register election start times and delay
    void StartElection(float timestamp);
    void EndElection(float timestamp);
    void ResetStartElection();

    void StartConfigChange(float timestamp);
    void EndConfigChange(float timestamp);
    void ResetStartConfigChange();

    /*
     * Traces for B4Mesh
     */
    void ReceivedBlockInfo(pair<pair<int, int>, pair<int, double>> block_creation_rate);
  

    // Register sent and received bytes/messages
    void ReceivedBytes(pair<float, int> new_value);
    void SentBytes(pair<float, int> new_value);
    void ReceivedMessages(pair<float, int> new_value);
    void SentMessages(pair<float, int> new_value);
    void DroppedMessages(pair<float, int> new_value);

  public:

    /*
     * Print a summary of all traces registered.
     */
    string PrintSummary();

    /*
     * Print a summary of raft specific traces.
     */
    string PrintRaftSummary();

    /*
    * Export TxRate values to a file
    */
    void ExportResults();

  public:

    /*
     * The keys of maps are the timestamps and the values are data traced at
     * that timestamp.
     */
    // Network traces
    map<float, int> received_bytes;
    map<float, int> sent_bytes;
    map<float, int> received_messages;
    map<float, int> sent_messages;
    map<float, int> dropped_messages;

    // Raft specific traces
    map<float, float> election_delay;
    float start_election; // initialize to -1
    map<float, float> config_change_delay;
    float start_config_change;

    // B4mesh specific traces
    vector<pair<pair<int, int>, pair<int, double>>> block_creation;
    //vector<pair<int,pair<int,int>>> blockgraph_file;
    vector<pair<double,pair<string,int>>> txs_per_block;


};
#endif
