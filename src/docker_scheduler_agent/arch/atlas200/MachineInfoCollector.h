#ifndef DOCKER_SCHEDULER_AGENT_ARCH_ATLAS200_MACHINEINFOCOLLECTOR_H
#define DOCKER_SCHEDULER_AGENT_ARCH_ATLAS200_MACHINEINFOCOLLECTOR_H

#include "MachineInfoCollectorBase.h"
#include <string_view>

class MachineInfoCollector : public MachineInfoCollectorBase {
public:
    using MachineInfoCollectorBase::MachineInfoCollectorBase;

    double GetNpuUsage();
};

#endif // DOCKER_SCHEDULER_AGENT_ARCH_ATLAS200_MACHINEINFOCOLLECTOR_H
