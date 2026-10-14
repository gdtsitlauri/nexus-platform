#pragma once

#include <map>
#include <string>
#include <vector>

// The NEY705 commands of nexus-eda.
namespace nexus::vlsi {

std::string cli_usage();
bool cli_handles(const std::string& command);
int cli_run(const std::string& command, const std::vector<std::string>& positional,
            const std::map<std::string, std::string>& options);

}  // namespace nexus::vlsi
