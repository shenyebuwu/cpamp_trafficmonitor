# Sources and notices

- Reference behavior: https://github.com/shenyebuwu/cpamp_monitor at `e8f00a99859f6d57ab4b0021fba806ac891f9003`.
  The upstream repository did not contain a license file at this revision. This port implements the API behavior in native C++; it does not distribute upstream Python source. Obtain permission from its author before redistribution if required.
- `include/PluginInterface.h`: official TrafficMonitor V1.85.1 header (API 6), https://github.com/zhongyang219/TrafficMonitor, Copyright Zhong Yang, upstream Anti 996 License 1.0 (Draft). Full unchanged license and notice in `include/TrafficMonitor-LICENSE`. Kept at the installed host's API version; later hosts support this ABI.
- `third_party/nlohmann/json.hpp`: nlohmann/json 3.11.3, MIT license; full license in `third_party/nlohmann/LICENSE.MIT`.

No project-wide open-source license has been selected for the original code in this repository. Third-party files remain under their respective licenses. This repository does not grant an additional license to third-party work.
