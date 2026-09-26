/* =============================================================================
 * default-nr-scenario.cc
 *
 * Single static UE -> gNB -> EPC -> cloud, UDP CBR.
 * Configuration: scratch/default/dag.json (all experiment parameters).
 * Radio: scalar Friis, no shadowing/fading, fixed 1x1 isotropic antennas.
 * Output: results.json, or the path supplied with --outputFile.
 *
 * Organized like default-nr-scenario-1 while preserving the baseline model,
 * random streams, application timing and KPI definitions.
 * =============================================================================
 */

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;
using json = nlohmann::json;

static void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

static double Power(const json& value)
{
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}

// ---------------------------------------------------------------------------
// Node mobility and application services
// ---------------------------------------------------------------------------
static void InstallGnbNodes(const NodeContainer& nodes, const std::vector<double>& position)
{
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(position[0], position[1], position[2]));
}

static void InstallUeNodes(const NodeContainer& nodes, const Vector& position)
{
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(position);
}

static uint32_t getServicePort(const json& cloud, const json& serviceName)
{
    uint32_t port = 0;
    for (const auto& service : cloud.at("services"))
        if (service.at("id") == serviceName) port = service.at("port");
    Require(port > 0 && port <= 65535, "Cloud service/UDP port not found or invalid.");
    return port;
}

// ---------------------------------------------------------------------------
// Results collection: preserve the baseline flow filter and KPI denominator.
// ---------------------------------------------------------------------------
static json CollectFlowMonitorStats(FlowMonitorHelper& flowHelper,
                                    Ptr<FlowMonitor> monitor,
                                    Ipv4Address ueIp,
                                    Ipv4Address cloudIp,
                                    uint32_t port,
                                    double start,
                                    double stop)
{
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    json flows = json::array();
    for (const auto& [id, stats] : monitor->GetFlowStats())
    {
        const auto tuple = classifier->FindFlow(id);
        if (tuple.protocol != 17 || tuple.sourceAddress != ueIp ||
            tuple.destinationAddress != cloudIp || tuple.destinationPort != port) continue;
        json f = {{"tx_packets", stats.txPackets}, {"rx_packets", stats.rxPackets},
                  {"ip_rx_bytes", stats.rxBytes},
                  {"ip_throughput_over_app_window_mbps", stats.rxBytes * 8.0 / (stop - start) / 1e6},
                  {"unreceived_packets_after_drain", stats.txPackets - stats.rxPackets},
                  {"mean_delay_ms", nullptr}, {"mean_jitter_ms", nullptr}, {"loss_fraction_after_drain", nullptr}};
        if (stats.rxPackets > 0) f["mean_delay_ms"] = 1000 * stats.delaySum.GetSeconds() / stats.rxPackets;
        if (stats.rxPackets > 1) f["mean_jitter_ms"] = 1000 * stats.jitterSum.GetSeconds() / (stats.rxPackets - 1);
        if (stats.txPackets > 0) f["loss_fraction_after_drain"] = 1.0 - double(stats.rxPackets) / stats.txPackets;
        flows.push_back(f);
    }
    return flows;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    try
    {
        // =========================================================================
        // Command-line arguments
        // =========================================================================
        std::string topologyFile = "scratch/default/dag.json";
        std::string outputFile = "results.json";
        CommandLine cmd(__FILE__);
        cmd.AddValue("topologyFile", "Single-flow JSON configuration", topologyFile);
        cmd.AddValue("outputFile", "Results JSON filename", outputFile);
        cmd.Parse(argc, argv);
        // =========================================================================
        // Topology loading and parameter validation
        // =========================================================================
        std::ifstream input(topologyFile);
        Require(input.good(), "Cannot open topologyFile: " + topologyFile);
        const json topology = json::parse(input);
        Require(topology.at("gNB").size() == 1 && topology.at("ue").size() == 1 &&
                topology.at("connections").size() == 1, "This scenario requires 1 gNB, 1 UE and 1 flow.");
        const auto& sim = topology.at("simulation");
        const auto& ch = topology.at("channel");
        const auto& core = topology.at("core");
        const auto& gnb = topology.at("gNB").at(0);
        const auto& ue = topology.at("ue").at(0);
        const auto& cloud = topology.at("cloud");
        const auto& conn = topology.at("connections").at(0);
        const auto& appCfg = conn.at("characteristics").at("application");
        const auto& gp = gnb.at("characteristics").at("phy");
        const auto& up = ue.at("characteristics").at("phy");
        const auto& mac = gnb.at("characteristics").at("mac");
        const auto& rlc = gnb.at("characteristics").at("rlc");
        Require(conn.at("from") == ue.at("id") && conn.at("to") == cloud.at("id"),
                "The only connection must be UE -> cloud.");
        Require(appCfg.at("protocol") == "UDP" && appCfg.at("traffic_model") == "CBR",
                "Only UDP CBR is implemented.");
        Require(ch.at("pathloss_model") == "Friis" &&
                !ch.at("shadowing_enabled").get<bool>() && !ch.at("fading_enabled").get<bool>(),
                "This baseline requires Friis without shadowing or fading.");
        Require(rlc.at("mode") == "UM", "This baseline implements RLC UM.");
        const double start = sim.at("app_start_s");
        const double stop = sim.at("app_stop_s");
        const double drain = sim.at("drain_time_s");
        Require(start > 0 && stop > start && drain > 0, "Invalid application/drain times.");
        const double end = stop + drain;
        const double freq = ch.at("carrier_frequency_hz");
        const double bw = gp.at("channel_bandwidth");
        Require(freq > 0 && bw > 0 && freq > bw / 2, "Invalid frequency/bandwidth.");
        const uint32_t mu = gp.at("numerology");
        Require(mu <= 4, "Numerology must be in [0,4] for this baseline.");
        const std::string pattern = gp.at("pattern");
        std::istringstream tokens(pattern);
        std::string token;
        bool hasDl = false, hasUl = false;
        while (std::getline(tokens, token, '|'))
        {
            Require(token == "DL" || token == "UL" || token == "F" || token == "S",
                    "Invalid TDD slot token.");
            hasDl |= token == "DL" || token == "F" || token == "S";
            hasUl |= token == "UL" || token == "F";
        }
        Require(hasDl && hasUl, "TDD requires DL control and UL data opportunities.");
        const DataRate offered(appCfg.at("dataRate").get<std::string>());
        const uint32_t bytes = appCfg.at("packetSize");
        const uint32_t mtu = core.at("mtu_bytes");
        Require(offered.GetBitRate() > 0 && bytes > 0 && bytes <= 1400 && mtu >= 1500,
                "Use packetSize 1..1400 bytes and core MTU >=1500 to avoid fragmentation.");
        const uint32_t port = getServicePort(cloud, appCfg.at("point_to_service"));
        const auto pos = gnb.at("position").get<std::vector<double>>();
        Require(pos.size() == 3, "gNB position requires [x,y,z] in metres.");
        const auto& placement = ue.at("placement");
        const double distance2d = placement.at("distance_2d_m");
        const double height = placement.at("height_m");
        const double az = placement.at("azimuth_deg").get<double>() * std::acos(-1.0) / 180;
        const double distance3d = std::hypot(distance2d, height - pos[2]);
        Require(distance2d > 0 && distance3d > 3 * 299792458.0 / freq,
                "Distance must be positive and in the Friis far-field region.");
        Require(core.at("s1u_delay_ms").get<double>() >= 0 &&
                core.at("cloud_delay_ms").get<double>() >= 0, "Negative link delay.");
        // =========================================================================
        // Simulation randomness
        // =========================================================================
        RngSeedManager::SetSeed(sim.at("seed").get<uint32_t>());
        RngSeedManager::SetRun(sim.at("run").get<uint64_t>());

        // =========================================================================
        // RLC configuration
        // =========================================================================
        Config::SetDefault("ns3::NrGnbRrc::QosFlowToRlcMapping", StringValue("RlcUmAlways"));
        Config::SetDefault("ns3::NrRlcUm::MaxTxBufferSize",
                           UintegerValue(rlc.at("buffer_size_bytes").get<uint32_t>()));
        // =========================================================================
        // Create nodes and install mobility
        // =========================================================================
        NodeContainer gnbNodes, ueNodes, cloudNodes;
        gnbNodes.Create(1); ueNodes.Create(1); cloudNodes.Create(1);
        InstallGnbNodes(gnbNodes, pos);
        InstallUeNodes(ueNodes,
                       Vector(pos[0] + distance2d * std::cos(az),
                              pos[1] + distance2d * std::sin(az), height));

        // =========================================================================
        // EPC and NR module setup
        // =========================================================================
        auto nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();
        nrEpcHelper->SetAttribute("S1uLinkDataRate", DataRateValue(DataRate(core.at("s1u_data_rate").get<std::string>())));
        nrEpcHelper->SetAttribute("S1uLinkDelay", TimeValue(Seconds(core.at("s1u_delay_ms").get<double>() / 1000)));
        nrEpcHelper->SetAttribute("S1uLinkMtu", UintegerValue(mtu));
        auto nrHelper = CreateObject<NrHelper>();
        nrHelper->SetEpcHelper(nrEpcHelper);

        // Only scalar Friis pathloss is installed. No 3GPP spectrum/fading model,
        // no buildings, no shadowing and no channel-matrix beamforming helper.
        auto channel = CreateObject<MultiModelSpectrumChannel>();
        auto loss = CreateObject<FriisPropagationLossModel>();
        loss->SetAttribute("Frequency", DoubleValue(freq));
        channel->AddPropagationLossModel(loss);
        // Match NrChannelHelper's slot-synchronous channel: no separate radio
        // propagation-delay model. Distance still controls Friis attenuation.
        // =========================================================================
        // Spectrum / BWP configuration
        // =========================================================================
        CcBwpCreator ccBwpCreator;
        auto bandInfo = ccBwpCreator.CreateOperationBandContiguousCc(CcBwpCreator::SimpleOperationBandConf(freq, bw, 1));
        auto allBwps = CcBwpCreator::GetAllBwps({bandInfo});
        for (auto& bwp : allBwps) bwp.get()->SetChannel(channel);
        // =========================================================================
        // MAC, PHY and antenna parameters
        // =========================================================================
        nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacScheduler" + mac.at("scheduler_type").get<std::string>()));
        nrHelper->SetSchedulerAttribute("EnableHarqReTx", BooleanValue(mac.at("harq_enabled").get<bool>()));
        nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(mu));
        nrHelper->SetGnbPhyAttribute("Pattern", StringValue(pattern));
        nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(Power(gp.at("tx_power"))));
        nrHelper->SetUePhyAttribute("TxPower", DoubleValue(Power(up.at("tx_power"))));
        nrHelper->SetGnbPhyAttribute("NoiseFigure", DoubleValue(gp.at("noise_figure_db")));
        nrHelper->SetUePhyAttribute("NoiseFigure", DoubleValue(up.at("noise_figure_db")));
        nrHelper->SetUePhyAttribute("EnableUplinkPowerControl", BooleanValue(up.at("uplink_power_control_enabled").get<bool>()));
        nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(1));
        nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(1));
        nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(false));
        nrHelper->SetGnbAntennaAttribute("AntennaElement", PointerValue(CreateObject<IsotropicAntennaModel>()));
        nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
        nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));
        nrHelper->SetUeAntennaAttribute("IsDualPolarized", BooleanValue(false));
        nrHelper->SetUeAntennaAttribute("AntennaElement", PointerValue(CreateObject<IsotropicAntennaModel>()));

        // =========================================================================
        // Internet stack, NR devices and random streams
        // =========================================================================
        InternetStackHelper internet;
        internet.Install(cloudNodes); internet.Install(ueNodes);
        auto gnbNetDev = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
        auto ueNetDev = nrHelper->InstallUeDevice(ueNodes, allBwps);
        int64_t stream = 1;
        stream += nrHelper->AssignStreams(gnbNetDev, stream);
        nrHelper->AssignStreams(ueNetDev, stream);
        // =========================================================================
        // Cloud link, IP addressing and routing
        // =========================================================================
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(core.at("cloud_data_rate").get<std::string>())));
        p2p.SetDeviceAttribute("Mtu", UintegerValue(mtu));
        p2p.SetChannelAttribute("Delay", TimeValue(Seconds(core.at("cloud_delay_ms").get<double>() / 1000)));
        auto backhaul = p2p.Install(nrEpcHelper->GetPgwNode(), cloudNodes.Get(0));
        Ipv4AddressHelper addr;
        addr.SetBase("1.0.0.0", "255.0.0.0");
        auto cloudIps = addr.Assign(backhaul);
        auto ueIps = nrEpcHelper->AssignUeIpv4Address(ueNetDev);
        Ipv4StaticRoutingHelper routing;
        routing.GetStaticRouting(cloudNodes.Get(0)->GetObject<Ipv4>())->AddNetworkRouteTo(
            Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
        routing.GetStaticRouting(ueNodes.Get(0)->GetObject<Ipv4>())->SetDefaultRoute(nrEpcHelper->GetUeDefaultGatewayAddress(), 1);
        // =========================================================================
        // Attach UE to the nearest gNB
        // =========================================================================
        nrHelper->AttachToClosestGnb(ueNetDev, gnbNetDev);
        // =========================================================================
        // Application layer: cloud sink and UE UDP CBR source
        // =========================================================================
        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        auto sinks = sinkHelper.Install(cloudNodes.Get(0));
        sinks.Start(Seconds(0)); sinks.Stop(Seconds(end));
        OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(cloudIps.GetAddress(1), port));
        source.SetConstantRate(offered, bytes);
        auto sources = source.Install(ueNodes.Get(0));
        sources.Start(Seconds(start)); sources.Stop(Seconds(stop));

        // =========================================================================
        // Flow monitor and simulation execution
        // =========================================================================
        FlowMonitorHelper flowHelper;
        NodeContainer endpoints; endpoints.Add(ueNodes); endpoints.Add(cloudNodes);
        auto monitor = flowHelper.Install(endpoints);
        Simulator::Stop(Seconds(end));
        std::cout << "[SETUP] 1 UE, 1 gNB, 1 UDP UL flow; Friis, no fading/shadowing\n"
                  << "[SETUP] distance2d=" << distance2d << " m; distance3d=" << distance3d
                  << " m; packet interval=" << 8.0 * bytes / offered.GetBitRate() << " s\n";
        Simulator::Run();
        // =========================================================================
        // Results collection and JSON output
        // =========================================================================
        monitor->CheckForLostPackets();
        const json flows = CollectFlowMonitorStats(flowHelper, monitor, ueIps.GetAddress(0),
                                                  cloudIps.GetAddress(1), port, start, stop);
        const uint64_t payloadRx = DynamicCast<PacketSink>(sinks.Get(0))->GetTotalRx();
        json result = {{"flow_count", flows.size()}, {"flows", flows},
            {"derived", {{"distance_3d_m", distance3d}, {"packet_interval_s", 8.0 * bytes / offered.GetBitRate()}}},
            {"application", {{"rx_payload_bytes", payloadRx},
                {"goodput_over_app_window_mbps", payloadRx * 8.0 / (stop - start) / 1e6}}},
            {"measurement_note", "Received counters include drain time; throughput/goodput denominator is app_stop_s-app_start_s. Unreceived packets may still be queued if drain is insufficient. Delay/jitter are IP-level for received packets. No UL SINR is reported from DL traces."}};
        // Report the effective parameters used above, with explicit units.
        result["parameters"] = {
            {"channel_bandwidth_hz", bw},
            {"ue_gnb_distance_2d_m", distance2d},
            {"ue_gnb_distance_3d_m", distance3d},
            {"numerology", mu},
            {"tdd_pattern", pattern},
            {"data_rate_bps", offered.GetBitRate()},
            {"packet_size_bytes", bytes},
            {"ue_tx_power_dbm", Power(up.at("tx_power"))},
            {"rlc_buffer_size_bytes", rlc.at("buffer_size_bytes")}
        };
        // These describe the model actually installed, not extra JSON options.
        result["scenario"] = {
            {"environment", "open_space"},
            {"open_space", true},
            {"obstacles", false},
            {"buildings", false},
            {"mobility", false},
            {"constant_nodes", true},
            {"mobility_model", "ConstantPositionMobilityModel"},
            {"link_condition", "LOS"},
            {"link_condition_basis", "Assumed unobstructed free-space link; no LOS/NLOS condition model is installed."},
            {"pathloss_model", ch.at("pathloss_model")},
            {"shadowing_enabled", ch.at("shadowing_enabled")},
            {"fading_enabled", ch.at("fading_enabled")},
            {"traffic_direction", "UE -> gNB -> EPC -> cloud"},
            {"protocol", appCfg.at("protocol")},
            {"traffic_model", appCfg.at("traffic_model")},
            {"ue_count", ueNodes.GetN()},
            {"gnb_count", gnbNodes.GetN()}
        };
        // Named fields replace the reference's positional mean array.
        // There is exactly one user flow, so its means are the scenario means.
        result["mean"] = {
            {"throughput_mbps", nullptr},
            {"end_to_end_delay_ms", nullptr},
            {"loss_fraction", nullptr},
            {"loss_percent", nullptr},
            {"jitter_ms", nullptr}
        };
        if (flows.size() == 1)
        {
            const auto& flow = flows.at(0);
            result["mean"]["throughput_mbps"] = flow.at("ip_throughput_over_app_window_mbps");
            result["mean"]["end_to_end_delay_ms"] = flow.at("mean_delay_ms");
            result["mean"]["loss_fraction"] = flow.at("loss_fraction_after_drain");
            if (!flow.at("loss_fraction_after_drain").is_null())
                result["mean"]["loss_percent"] = 100.0 * flow.at("loss_fraction_after_drain").get<double>();
            result["mean"]["jitter_ms"] = flow.at("mean_jitter_ms");
        }
        result["metric_definitions"] = {
            {"throughput_mbps", "Received IP bytes * 8 / application window / 1e6; includes IP/UDP headers and packets received during drain."},
            {"end_to_end_delay_ms", "Mean IP-level delay from UE transmission to cloud reception, for received packets."},
            {"loss_fraction", "(Transmitted packets - received packets) / transmitted packets after drain; may include packets still queued."},
            {"loss_percent", "100 * loss_fraction."},
            {"jitter_ms", "FlowMonitor sum of absolute consecutive packet delay differences / (received packets - 1), in ms."},
            {"undefined_metrics", "null when there are insufficient packets or no matching flow."}
        };
        std::ofstream output(outputFile);
        Require(output.good(), "Cannot open outputFile: " + outputFile);
        output << result.dump(2) << '\n';
        output.close();
        Require(!output.fail(), "Failed to write results.");
        std::cout << "[RESULT] flows=" << flows.size() << "; application goodput="
                  << result["application"]["goodput_over_app_window_mbps"] << " Mbps\n";
        Simulator::Destroy();
        return flows.size() == 1 ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Configuration/run error: " << error.what() << '\n';
        Simulator::Destroy();
        return 1;
    }
}
