#include "bridge/Stats.h"
#include "concurrency/ScopedSpinLocker.h"
#include "utils/ScopedFileHandle.h"
#include "utils/Time.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/resource.h>
#endif

namespace nlohmann
{
template <class T, std::size_t N>
json to_json(const T (&data)[N])
{
    auto v = json::array();
    for (size_t i = 0; i < N; ++i)
    {
        v.push_back(data[i]);
    }
    return v;
}
} // namespace nlohmann

namespace bridge
{

namespace Stats
{

#ifdef __APPLE__
struct timeval operator-(struct timeval a, const struct timeval& b)
{
    a.tv_sec -= b.tv_sec;
    if (a.tv_usec < b.tv_usec)
    {
        a.tv_usec = a.tv_usec + 1000000 - b.tv_usec;
        --a.tv_sec;
    }
    else
    {
        a.tv_usec -= b.tv_usec;
    }

    return a;
}

struct timeval operator+(struct timeval a, const struct timeval& b)
{
    a.tv_sec += b.tv_sec;
    if (a.tv_usec + b.tv_usec >= 1000000)
    {
        a.tv_sec++;
        a.tv_usec = a.tv_usec + b.tv_usec - 1000000;
    }
    else
    {
        a.tv_usec += b.tv_usec;
    }

    return a;
}

uint64_t toMicroSeconds(const struct timeval& a)
{
    return static_cast<uint64_t>(a.tv_sec) * 1000000 + a.tv_usec;
}
#endif

SystemStats::SystemStats() {}

std::string MixerManagerStats::describe()
{
    nlohmann::json result;
    result["current_timestamp"] = utils::Time::getAbsoluteTime() / 1000000ULL;
    result["conferences"] = conferences;
    result["largestConference"] = largestConference;
    result["participants"] = std::max({videoStreams, audioStreams, dataStreams});
    result["audiochannels"] = audioStreams;
    result["videochannels"] = videoStreams;
    result["threads"] = systemStats.totalNumberOfThreads;
    result["cpu_usage"] = systemStats.processCPU;
    result["cpu_engine"] = systemStats.engineCpu;
    result["cpu_rtce"] = systemStats.rtceCpu;
    result["cpu_workers"] = systemStats.workerCpu;
    result["cpu_manager"] = systemStats.managerCpu;

    result["total_memory"] = systemStats.processMemory;
    result["used_memory"] = systemStats.processMemory;
    result["packet_rate_download"] = engineStats.activeMixers.inbound.total().packetsPerSecond;
    result["bit_rate_download"] = engineStats.activeMixers.inbound.total().bitrateKbps;
    result["packet_rate_upload"] = engineStats.activeMixers.outbound.total().packetsPerSecond;
    result["bit_rate_upload"] = engineStats.activeMixers.outbound.total().bitrateKbps;
    result["total_udp_connections"] = systemStats.connections.udpTotol();
    result["total_tcp_connections"] = systemStats.connections.tcpTotal();
    result["rtc_tcp4_connections"] = systemStats.connections.tcp4.rtp;
    result["rtc_tcp6_connections"] = systemStats.connections.tcp6.rtp;

    result["http_tcp_connections"] = systemStats.connections.tcp4.http;

    result["inbound_audio_streams"] = engineStats.activeMixers.inbound.audio.activeStreamCount;
    result["outbound_audio_streams"] = engineStats.activeMixers.outbound.audio.activeStreamCount;
    result["inbound_video_streams"] = engineStats.activeMixers.inbound.video.activeStreamCount;
    result["outbound_video_streams"] = engineStats.activeMixers.outbound.video.activeStreamCount;

    result["job_queue"] = jobQueueLength;
    result["loss_upload"] = engineStats.activeMixers.outbound.total().getSendLossRatio();
    result["loss_download"] = engineStats.activeMixers.inbound.total().getReceiveLossRatio();

    result["pacing_queue"] = engineStats.activeMixers.pacingQueue;
    result["rtx_pacing_queue"] = engineStats.activeMixers.rtxPacingQueue;

    result["shared_udp_send_queue"] = udpSharedEndpointsSendQueue;
    result["shared_udp_receive_rate"] = udpSharedEndpointsReceiveKbps;
    result["shared_udp_send_rate"] = udpSharedEndpointsSendKbps;

    result["send_pool"] = sendPoolSize;
    result["receive_pool"] = receivePoolSize;

    result["loss_upload_hist"] = nlohmann::to_json(engineStats.activeMixers.outbound.transport.lossGroup);
    result["loss_download_hist"] = nlohmann::to_json(engineStats.activeMixers.inbound.transport.lossGroup);
    result["bwe_download_hist"] = nlohmann::to_json(engineStats.activeMixers.inbound.transport.bandwidthEstimateGroup);
    result["rtt_download_hist"] = nlohmann::to_json(engineStats.activeMixers.inbound.transport.rttGroup);

    result["engine_slips"] = engineStats.timeSlipCount;

    return result.dump(4);
}

SystemStatsCollector::ProcStat operator-(SystemStatsCollector::ProcStat a, const SystemStatsCollector::ProcStat& b)
{
    a.cstime -= b.cstime;
    a.cutime -= b.cutime;
    a.stime -= b.stime;
    a.utime -= b.utime;

    return a;
}

SystemStatsCollector::SystemCpu operator-(SystemStatsCollector::SystemCpu a, const SystemStatsCollector::SystemCpu& b)
{
    a.idle -= b.idle;
    a.iowait -= b.iowait;
    a.irq -= b.irq;
    a.nicetime -= b.nicetime;
    a.softirq -= b.softirq;
    a.stime -= b.stime;
    a.utime -= b.utime;

    return a;
}

bool SystemStatsCollector::readProcStat(FILE* file, ProcStat& stat) const
{
    if (!file)
    {
        return false;
    }

    ProcStat sample;

    auto procInfoRead = (11 ==
        fscanf(file,
            "%d %28s %*c %*lu %*lu %*lu %*lu %*lu"
            " %*lu %*lu %*lu %*lu %*lu %lu %lu %ld"
            " %ld %ld %ld %ld %*lu %*llu %lu %ld",
            &sample.pid,
            static_cast<char*>(sample.name),
            &sample.utime,
            &sample.stime,
            &sample.cutime,
            &sample.cstime,
            &sample.priority,
            &sample.nice,
            &sample.threads,
            &sample.virtualmem,
            &sample.pagedmem));

    if (procInfoRead)
    {
        stat = sample;
    }
    return procInfoRead;
}

bool SystemStatsCollector::readSystemStat(FILE* h, SystemCpu& stat) const
{
    char cpuName[50];
    SystemCpu sample;
    auto infoRead = (8 ==
        fscanf(h,
            "%48s %lu %lu %lu %lu %lu %lu %lu",
            static_cast<char*>(cpuName),
            &sample.utime,
            &sample.stime,
            &sample.nicetime,
            &sample.idle,
            &sample.iowait,
            &sample.irq,
            &sample.softirq));

    if (infoRead)
    {
        stat = sample;
    }
    return infoRead;
}

SystemStats SystemStatsCollector::collect(uint16_t httpPort, uint16_t tcpRtpPort)
{
    concurrency::ScopedSpinLocker lock(_collectingStats, std::chrono::nanoseconds(0));
    SystemStats result;
    if (!lock.hasLock())
    {
        _stats.read(result);
        return result;
    }

    SystemStats prevStats;
    _stats.read(prevStats);
    int64_t statsAgeNs = utils::Time::getAbsoluteTime() - prevStats.timestamp;
    if (statsAgeNs < static_cast<int64_t>(utils::Time::sec) && statsAgeNs >= 0)
    {
        return prevStats;
    }

    SystemStats stats;

#ifdef __APPLE__
    auto sample0 = collectMacCpuSample();
    auto netStat = collectNetStats(0, 0);

    auto toSleep = utils::Time::sec - (utils::Time::getAbsoluteTime() - sample0.timestamp);
    utils::Time::nanoSleep(toSleep);

    auto sample1 = collectMacCpuSample();

    stats.processCPU =
        static_cast<double>(toMicroSeconds((sample1.utime - sample0.utime) + (sample1.stime - sample0.stime)) * 1000) /
        (1 + sample1.timestamp - sample0.timestamp);
    stats.processCPU /= std::thread::hardware_concurrency();
    stats.systemCpu = 0;
    stats.processMemory = sample1.pagedmem;
#else
    const auto cpuCount = std::thread::hardware_concurrency();
    const auto taskIds = getTaskIds();
    auto start = utils::Time::getAbsoluteTime();
    auto sample0 = collectLinuxCpuSample(taskIds);
    auto netStat = collectNetStats(httpPort, tcpRtpPort);

    auto toSleep = 1000000000UL - (utils::Time::getAbsoluteTime() - start);
    utils::Time::nanoSleep(toSleep);

    auto sample1 = collectLinuxCpuSample(taskIds);

    auto diffProc = sample1.procSample - sample0.procSample;
    auto systemDiff = sample1.systemSample - sample0.systemSample;

    size_t workerCount = 0;
    double workerCpu = 0;
    for (size_t i = 0; i < sample1.threadSamples.size(); ++i)
    {
        const auto taskSample = sample1.threadSamples[i] - sample0.threadSamples[i];
        if (taskSample.empty())
        {
            break;
        }

        if (!std::strcmp(taskSample.name, "(Worker)"))
        {
            workerCpu += static_cast<double>(taskSample.utime + taskSample.stime);
            ++workerCount;
        }
        else if (!std::strcmp(taskSample.name, "(Rtce)"))
        {
            stats.rtceCpu =
                cpuCount * static_cast<double>(taskSample.utime + taskSample.stime) / (1 + systemDiff.totalJiffies());
        }
        else if (!std::strcmp(taskSample.name, "(Engine)"))
        {
            stats.engineCpu =
                cpuCount * static_cast<double>(taskSample.utime + taskSample.stime) / (1 + systemDiff.totalJiffies());
        }
        else if (!std::strcmp(taskSample.name, "(MixerManager)"))
        {
            stats.managerCpu =
                cpuCount * static_cast<double>(taskSample.utime + taskSample.stime) / (1 + systemDiff.totalJiffies());
        }
    }

    if (workerCount > 0)
    {
        stats.workerCpu = workerCpu * cpuCount / (workerCount * (1 + systemDiff.totalJiffies()));
    }

    stats.processCPU = static_cast<double>(diffProc.utime + diffProc.stime) / (1 + systemDiff.totalJiffies());
    stats.systemCpu = 1.0 - systemDiff.idleRatio();
    stats.totalNumberOfThreads = sample1.procSample.threads;
    stats.processMemory = sample1.procSample.pagedmem * getpagesize() / 1024;
#endif
    stats.timestamp = utils::Time::getAbsoluteTime();
    stats.connections = netStat;
    _stats.write(stats);
    return stats;
}

#ifdef __APPLE__
SystemStatsCollector::MacCpuSample SystemStatsCollector::collectMacCpuSample() const
{
    MacCpuSample sample;
    sample.timestamp = utils::Time::getAbsoluteTime();
    struct rusage procInfo;
    if (!getrusage(RUSAGE_SELF, &procInfo))
    {
        sample.utime = procInfo.ru_utime;
        sample.stime = procInfo.ru_stime;
        sample.pagedmem = procInfo.ru_maxrss / 1024;
    }
    return sample;
}

#else
SystemStatsCollector::LinuxCpuSample SystemStatsCollector::collectLinuxCpuSample(const std::vector<int>& taskIds) const
{
    LinuxCpuSample sample;

    utils::ScopedFileHandle hProcStat(fopen("/proc/self/stat", "r"));
    utils::ScopedFileHandle hCpuStat(fopen("/proc/stat", "r"));

    if (hProcStat.get() && hCpuStat.get() && readProcStat(hProcStat.get(), sample.procSample) &&
        readSystemStat(hCpuStat.get(), sample.systemSample))
    {
    }

    size_t threadCount = 0;
    for (int taskId : taskIds)
    {
        char fileName[250];
        std::sprintf(fileName, "/proc/self/task/%d/stat", taskId);
        utils::ScopedFileHandle hTaskStat(fopen(fileName, "r"));
        if (threadCount == sample.threadSamples.size() ||
            !(hTaskStat.get() && readProcStat(hTaskStat.get(), sample.threadSamples[threadCount++])))
        {
            break;
        }
    }
    return sample;
}

std::vector<int> SystemStatsCollector::getTaskIds() const
{
    std::vector<int> result;
    auto folderHandle = opendir("/proc/self/task");
    for (auto entry = readdir(folderHandle); entry != nullptr; entry = readdir(folderHandle))
    {
        if (std::isdigit(entry->d_name[0]))
        {
            result.push_back(std::stoi(entry->d_name));
        }
    }
    closedir(folderHandle);
    return result;
}
#endif
ConnectionsStats SystemStatsCollector::collectNetStats(uint16_t httpPort, uint16_t tcpRtpPort)
{
#ifdef __APPLE__
    ConnectionsStats result;
    return result;
#else
    return collectLinuxNetStat(httpPort, tcpRtpPort);
#endif
}

namespace
{
template <class Predicate>
void readSocketInfo(FILE* file, uid_t myUid, Predicate predicate, uint32_t& count)
{
    if (!file)
    {
        count = 0;
        return;
    }

    ::rewind(file);
    uint32_t port = 0;
    uint32_t remotePort = 0;
    char ignore[513];
    uid_t uid;
    const char* formatString = "%*d: %*32[^:]:%x %*32[^:]:%x %*x %*8[^:]:%*8s %*x:%*x %*x %u";
    fgets(ignore, sizeof(ignore), file);
    for (int i = 0; i < 500; ++i)
    {
        int items = fscanf(file, formatString, &port, &remotePort, &uid);
        fgets(ignore, sizeof(ignore), file);
        if (items >= 3 && uid == myUid && predicate(port, remotePort))
        {
            ++count;
        }
        else if (items < 3)
        {
            break;
        }
    }
}

} // namespace

ConnectionsStats SystemStatsCollector::collectLinuxNetStat(uint16_t httpPort, uint16_t tcpRtpPort)
{
    utils::ScopedFileHandle hTcp4Stat(fopen("/proc/self/net/tcp", "r"));
    utils::ScopedFileHandle hTcp6Stat(fopen("/proc/self/net/tcp6", "r"));
    utils::ScopedFileHandle hUdp4Stat(fopen("/proc/self/net/udp", "r"));
    utils::ScopedFileHandle hUdp6Stat(fopen("/proc/self/net/udp6", "r"));

    const auto myUid = getuid();

    ConnectionsStats result;
    readSocketInfo(
        hTcp4Stat.get(),
        myUid,
        [httpPort](uint32_t localPort, uint32_t remotePort) { return localPort == httpPort && remotePort != 0; },
        result.tcp4.http);

    readSocketInfo(
        hTcp4Stat.get(),
        myUid,
        [tcpRtpPort](uint32_t localPort, uint32_t remotePort) { return localPort == tcpRtpPort && remotePort != 0; },
        result.tcp4.rtp);

    readSocketInfo(
        hUdp4Stat.get(),
        myUid,
        [](uint32_t localPort, uint32_t remotePort) { return true; },
        result.udp4);

    readSocketInfo(
        hTcp6Stat.get(),
        myUid,
        [httpPort](uint32_t localPort, uint32_t remotePort) { return localPort == httpPort && remotePort != 0; },
        result.tcp6.http);

    readSocketInfo(
        hTcp6Stat.get(),
        myUid,
        [tcpRtpPort](uint32_t localPort, uint32_t remotePort) { return localPort == tcpRtpPort && remotePort != 0; },
        result.tcp6.rtp);

    readSocketInfo(
        hUdp6Stat.get(),
        myUid,
        [](uint32_t localPort, uint32_t remotePort) { return true; },
        result.udp6);

    return result;
}

} // namespace Stats

} // namespace bridge
